#ifndef UDPServer_hpp
#define UDPServer_hpp

#include "Server.hpp"
#include "Session.hpp"

namespace myNet {
class UDPServer : public Server {
public:
    using Ptr = std::shared_ptr<UDPServer>;
    using onCreateSocketCB = std::function<Socket::Ptr(const toolkit::EventPollerPool::Ptr&, const Buffer::Ptr&, sockaddr*, int)>;

    explicit UDPServer(const toolkit::EventPollerPool::Ptr& poller = nullptr);
    ~UDPServer() override;

    // 开始监听服务器
    void start(uint16_t port, const std::string& host = "::");

    uint16_t getPort();

    void setOnCreateSocket(onCreateSocketCB cb);

protected:
    virtual Ptr onCreateServer(const toolkit::EventPoller::Ptr& poller);
    virtual void cloneFrom(const UDPServer& that);

private:
    // 定时管理 Session, UDP 会话超时处理
    void onManageSession();

    // 接收到消息时的处理
    void onRead(const Buffer::Ptr& buf, struct sockaddr* addr, int addr_len);

    // 根据peerId获取Session, 若无就创建一个
    const Session::Ptr& getSession(const std::string& peerId, const Buffer::Ptr& buf, sockaddr* addr, int addrLen, bool& isNew);

    // 创建Session
    const Session::Ptr& createSession(const std::string& id, const Buffer::Ptr& buf, sockaddr* addr, int addrLen);

    // 创建Socket
    Socket::Ptr createSocket(const toolkit::EventPollerPool::Ptr& poller, const Buffer::Ptr& buf = nullptr, sockaddr* addr = nullptr, int addrLen = 0);

    bool _cloned{false};
    Socket::Ptr _socket;
    std::shared_ptr<toolkit::Timer> _timer;
    onCreateSocketCB _onCreateSocketCB;

    // recursive_mutex 减小死锁可能性, 但效率不如mutex
    std::shared_ptr<std::recursive_mutex> _sessionMtx;
    // 非SessionMap 类
    std::shared_ptr<std::unordered_map<std::string, SessionHelper::Ptr>> _sessionMap;
    // 主server 持有cloned server 的引用
    std::unordered_map<toolkit::EventPoller*, Ptr> _clonedServer;
    // Session 构建器
    std::function<SessionHelper::Ptr(const UDPServer::Ptr&, const Socket::Ptr&)> _sessionBuilder;
};

}  // namespace myNet

#endif  // UDPServer_hpp