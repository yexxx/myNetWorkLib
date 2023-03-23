#ifndef TCPClient_hpp
#define TCPClient_hpp

#include <functional>
#include <memory>

#include "Server.hpp"
#include "Socket.hpp"

namespace myNet {

// 感觉跟Session 差不多，应该可以写成Session 的子类，之后尝试
class TCPClient : public std::enable_shared_from_this<TCPClient>, public SocketHelper {
public:
    using Ptr = std::shared_ptr<TCPClient>;

    TCPClient(const EventPoller::Ptr& poller = nullptr);
    ~TCPClient() override{};

    // 不是Socket 的connect
    virtual void connect(const std::string& url, uint16_t port, float timeoutSec = 5, uint16_t localPort = 0);

    // 主动断开
    void shutdown(const SocketException& socketException = SocketException(Errcode::Err_shutdown, "shutdown")) override;

    // 连接是否有效
    virtual bool alive() const;

    virtual void setNetAdapter(const std::string& localIp) { _netAdapter = localIp; }

protected:
    // 连接成功与否
    virtual void onConnect(const SocketException& e) {
        if (e) {
            WarnL << e.what();
            return;
        }
        InfoL << "success";
    };

    virtual void onRecv(const Buffer::Ptr& buf) = 0;

    // 数据全部发送完成
    virtual void onFlush() {}

    virtual void onErr(const SocketException& e) { WarnL << e.what(); };

    // 自定义管理，每两秒执行一次
    virtual void onManager() {}

private:
    // Socket::connect 方法中的onErrCB 执行的内容
    // 无错误时设置onFlush 和onRead，否则处理错误
    void onSocketConnect(const SocketException& e);

    std::string _netAdapter{"::"};
    std::shared_ptr<Timer> _timer;
};

}  // namespace myNet

#endif  // TCPClient_hpp