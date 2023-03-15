#ifndef Socket_hpp
#define Socket_hpp

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <sstream>
#include <string>

#include "Buffer.hpp"
#include "Poller/EventPoller.h"
#include "Poller/Timer.h"
#include "Util/SpeedStatistic.h"

namespace myNet {

enum class Errcode {
    Err_sucess = 0,
    Err_eof,
    Err_timeout,
    Err_refused,
    Err_dns,
    Err_shutdown,
    Err_other = 0xFF
};

enum class SocketType {
    Socket_Invalid = -1,
    Socket_TCP = 0,
    Socket_UDP = 1
};

// 异常
class SocketException : public std::exception {
public:
    SocketException(Errcode code = Errcode::Err_sucess, const std::string &msg = "") {
        _code = code;
        _msg = msg;
    };

    const char *what() const noexcept override { return _msg.c_str(); };

    Errcode getErrcode() const { return _code; }

    operator bool() const { return _code != Errcode::Err_sucess; }

private:
    Errcode _code;
    std::string _msg;
};

// // os<<重载输出SocketExpection信息，但是-lZLTooKit会冲突，之后再加上
// std::ostream &operator<<(std::ostream &ost, const SocketException &err) {
//     ost << err.getErrcode() << "(" << err.what() << ")";
//     return ost;
// };

// socket的文件描述符及类型
class SocketNum {
public:
    using Ptr = std::shared_ptr<SocketNum>;
    SocketNum(int fd, SocketType type) {
        _fd = fd;
        _type = type;
    }

    ~SocketNum() {
        shutdown(_fd, SHUT_RDWR);
        close(_fd);
    }

    int getFd() const { return _fd; }
    SocketType getType() const { return _type; }

private:
    int _fd;
    SocketType _type;
};

// 管理socket的文件描述符
// 自定义创建/复制方法
// 析构时移除监听，关闭socket
class SocketFD : public noncopyable {
public:
    using Ptr = std::shared_ptr<SocketFD>;
    SocketFD(int fd, SocketType type, const toolkit::EventPoller::Ptr &poller) {
        _num = std::make_shared<SocketNum>(fd, type);
        _poller = poller;
    }
    SocketFD(const SocketFD &socketFD, const toolkit::EventPoller::Ptr &poller) {
        _num = socketFD._num;
        _poller = socketFD._poller;
        if (poller == _poller) { throw std::invalid_argument("Copy a SocketFD with a same poller."); }
    }

    ~SocketFD() {
        _poller->delEvent(_num->getFd(), [](bool) {});
    }

    int getFd() const { return _num->getFd(); }
    SocketType getType() const { return _num->getType(); }

private:
    SocketNum::Ptr _num;
    toolkit::EventPoller::Ptr _poller;
};

// socket信息
class SocketInfo {
public:
    SocketInfo() = default;
    virtual ~SocketInfo() = default;

    virtual std::string get_localIP() = 0;
    virtual uint16_t get_localPort() = 0;
    virtual std::string get_peerIP() = 0;
    virtual uint16_t get_peerPort() = 0;
    virtual std::string getIdentifier() const { return ""; }
};

// socket对象
class Socket : public std::enable_shared_from_this<Socket>, public noncopyable, public SocketInfo {
public:
    using Ptr = std::shared_ptr<Socket>;

    using onReadCB = std::function<void(const Buffer::Ptr &buf, sockaddr *addr, int addrLen)>;
    using onErrCB = std::function<void(const SocketException &err)>;
    using onAcceptCB = std::function<void(Ptr &sock, std::shared_ptr<void> &complete)>;
    using onFlushCB = std::function<bool()>;
    using onCreateSocketCB = std::function<Ptr(const toolkit::EventPoller::Ptr &poller)>;
    using onSendResultCB = std::function<void(const Buffer::Ptr &buffer, bool sendState)>;
    using asyncConnectCB = std::shared_ptr<std::function<void(int)>>;

    Socket(const toolkit::EventPoller::Ptr poller = nullptr, bool enableMutex = true);
    ~Socket() override;

    static Ptr createSocket(const toolkit::EventPoller::Ptr &poller = nullptr, bool enable_mutex = true);

    virtual void setOnRead(onReadCB &&readCB);
    virtual void setOnErr(onErrCB &&errCB);
    virtual void setOnAccept(onAcceptCB &&acceptCB);
    virtual void setOnFlush(onFlushCB &&flushCB);
    virtual void setOnCreateSocket(onCreateSocketCB &&createSocketCB);  // onBeforeAccept
    virtual void setOnSendResult(onSendResultCB &&sendResultCB);

    // 创建tcp客户端并异步连接服务器
    virtual void connect(const std ::string &url, uint16_t port, const onErrCB &errCB, float timeoutSec = 5, const std::string &localIP = "::", uint16_t localPort = 0);

    // 创建TCP监听服务器；backLog: tcp最大积压数量
    virtual bool listen(uint16_t port, const std::string &localIP = "::", int backLog = 1024);

    // 创建udp套接字（无连接，可作为服务器或客户端）
    virtual bool bindUdpSocket(uint16_t port, const std::string &localIP = "::", bool enableReuse = true);

    // 返回 -1 表示失败, ssize_t： long
    ssize_t send(const char *buf, size_t size = 0, sockaddr *addr = nullptr, socklen_t addrLen = 0, bool tryFlush = true);
    ssize_t send(const std::string buf, sockaddr *addr = nullptr, socklen_t addrLen = 0, bool tryFlush = true);
    virtual ssize_t send(const Buffer::Ptr buf, sockaddr *addr = nullptr, socklen_t addrLen = 0, bool tryFlush = true);

    // 将所有数据写入socket,清除缓存
    int flushAll();

    // 在poller中触发onErrCB,同时关闭socket
    virtual bool emitErr(const SocketException &socketException) noexcept;

    // 开关数据接收
    virtual void enableRecv(bool enabled);

    virtual void setSendTimeOutSec(uint32_t sec);

    virtual bool isSocketBusy() const;

    virtual int getFd() const;
    virtual SocketType getType() const;
    virtual const toolkit::EventPoller::Ptr &getPoller() const;

    // 从另一个socket克隆，实现一个socket被多个poller监听
    // 让this监听传递的socket
    virtual bool cloneFromListenSocket(const Socket &socket);

    // 实现socket切换poller线程
    // 让this的poller注册传递的socket事件
    virtual bool cloneFromPeerSocket(const Socket &socket);

    virtual void closeSocket();

    // 绑定udp目标地址
    virtual bool bindPeerAddr(const sockaddr *addr, socklen_t addrLen = 0);

    // 设置发送的flag，默认为不触发sigpipe，非阻塞发送
    virtual void setSendFlag(int flag = MSG_NOSIGNAL | MSG_DONTWAIT);

    // 获取缓存buffer个数(等待及发送中的数据的个数)
    virtual size_t getSendBufferCount() const;
    // 获取上次socket发送缓存清空至今的毫秒数
    virtual uint64_t elapsedTimeAfterFlushed() const;

    // 网速, bytes/s
    int getRecvSpeed();
    int getSendSpeed();

    std::string get_localIP() override;
    uint16_t get_localPort() override;
    std::string get_peerIP() override;
    uint16_t get_peerPort() override;
    std::string getIdentifier() const override;

private:
    SocketFD::Ptr cloneSocketFd(const Socket &sock);
    SocketFD::Ptr setPeerSock(int fd);
    SocketFD::Ptr makeSocketFD(int fd, SocketType type);
    int onAccept(const SocketFD::Ptr &sock, int event) noexcept;
    ssize_t onRead(const SocketFD::Ptr &sock, bool isUdp = false) noexcept;
    void onWriteable(const SocketFD::Ptr &sock);
    void onConnected(const SocketFD::Ptr &sock, const onErrCB &errcb);
    void onFlush();
    void startWriteableEvent(const SocketFD::Ptr &sock);
    void stopWriteableEvent(const SocketFD::Ptr &sock);
    bool listen(const SocketFD::Ptr &sock);
    bool flushData(const SocketFD::Ptr &sock, bool pollerThread);
    bool attachEvent(const SocketFD::Ptr &sock);

    int _sockFlags{MSG_NOSIGNAL | MSG_DONTWAIT};
    std::atomic<bool> _enableRecv{true};
    std::atomic<bool> _sendable{true};

    // tcp连接超时定时器
    toolkit::Timer::Ptr _conTime;
    // 异步tcp连接结果回调
    asyncConnectCB _asyncConnectCB;
    // 缓存清空(flush)计时器
    toolkit::Ticker _sendFlushTicker;

    BufferRaw::Ptr _readBuffer;
    SocketFD::Ptr _socketFd;
    toolkit::EventPoller::Ptr _poller;
    // 读socket文件描述符时上锁（跨线程）
    mutable MutexWrapper _mtxSocketFd;

    onErrCB _onErrCB;
    onReadCB _onReadCB;
    onFlushCB _onFlushCB;
    onAcceptCB _onAcceptCB;
    onCreateSocketCB _onCreateSocketCB;
    mutable MutexWrapper _mtxEvent;

    // buffer清空最长超时
    uint32_t _maxSendBufferMs{10 * 1000};
    std::list<Buffer::Ptr> _sendBufWaiting;
    std::list<BufferList::Ptr> _sendBufSending;
    mutable MutexWrapper _mtxSendBufWaiting;
    mutable MutexWrapper _mtxSendBufSending;
    onSendResultCB _onSendResultCB;
    // toolkit::ObjectStatistic<Socket> _statistic;

    bool _enableSpeed{false};
    toolkit::BytesSpeed _recvSpeed;
    toolkit::BytesSpeed _sendSpeed;
};

class SocketSender {
public:
    SocketSender() = default;
    ~SocketSender() = default;

    virtual void shutdown(const SocketException &socketException = SocketException(Errcode::Err_shutdown, "shutdown")) = 0;

    SocketSender &operator<<(const char *buf);
    SocketSender &operator<<(const std::string buf);
    SocketSender &operator<<(const Buffer::Ptr buf);

    ssize_t send(const char *buf, size_t size = 0);
    ssize_t send(const std::string buf);
    virtual ssize_t send(Buffer::Ptr buf) = 0;
};

class SocketHelper : public SocketSender, public SocketInfo, public toolkit::TaskExecutorInterface {
public:
    SocketHelper(const Socket::Ptr &sock);
    ~SocketHelper() = default;

    void setSendFlushFlag(bool tryFlush);  // 批量发送，提高性能
    void setSendFlag(int flags = MSG_NOSIGNAL | MSG_DONTWAIT);

    bool isSocketBusy() const;

    void setOnCreateSocket(Socket::onCreateSocketCB createSocketCB);

    Socket::Ptr createSocket();

    int flashAll();

    const Socket::Ptr &getSocket() const;
    const toolkit::EventPoller::Ptr &getPoller() const;
    std::string get_localIP() override;
    uint16_t get_localPort() override;
    std::string get_peerIP() override;
    uint16_t get_peerPort() override;

    toolkit::Task::Ptr async(toolkit::TaskIn task, bool maySync = true) override;
    toolkit::Task::Ptr async_first(toolkit::TaskIn task, bool maySync = true) override;

    ssize_t send(Buffer::Ptr buf) override;
    void shutdown(const SocketException &socketException = SocketException(Errcode::Err_shutdown, "shutdown")) override;

protected:
    void setPoller(const toolkit::EventPoller::Ptr &poller);
    void setSock(const Socket::Ptr &sock);

private:
    bool _tryFlush{false};
    uint16_t _localPort{0};
    uint16_t _peerPort{0};
    std::string _localIP{""};
    std::string _peerIP{""};
    Socket::Ptr _sock;
    toolkit::EventPoller::Ptr _poller;
    Socket::onCreateSocketCB _createSocketCB;
};

}  // namespace myNet

#endif  // Socket_hpp