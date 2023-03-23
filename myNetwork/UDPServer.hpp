#ifndef UDPServer_hpp
#define UDPServer_hpp

#include "Server.hpp"
#include "Session.hpp"

namespace myNet {
class UDPServer : public Server {
public:
    using Ptr = std::shared_ptr<UDPServer>;
    using onCreateSocketCB = std::function<Socket::Ptr(const EventPoller::Ptr&, const Buffer::Ptr&, sockaddr*, int)>;

    explicit UDPServer(const EventPoller::Ptr& poller = nullptr);
    ~UDPServer() override;

    // 开始监听服务器
    template <typename SessionType>
    void start(uint16_t port, const std::string& host = "::");

    uint16_t getPort() {
        if (_socket) return _socket->get_localPort();
        return 0;
    };

    // Socket::Ptr getSocket() { return _socket; } // For Debug

    void setOnCreateSocket(onCreateSocketCB cb);

protected:
    virtual void cloneFrom(const UDPServer& that);

private:
    // 定时管理 Session, UDP 会话超时处理
    void onManageSession();

    // 接收到消息时的处理
    void onRead(const Buffer::Ptr& buf, struct sockaddr* addr, int addrLen, bool isServerFd = true);

    static void emitSessionRecv(const Session::Ptr& session, const Buffer::Ptr& buf);

    // 根据peerId获取Session, 若无就创建一个
    const Session::Ptr& getSession(const std::string& peerId, const Buffer::Ptr& buf, sockaddr* addr, int addrLen, bool& isNew);

    // 创建Session
    const Session::Ptr& createSession(const std::string& id, const Buffer::Ptr& buf, sockaddr* addr, int addrLen);

    // 创建Socket
    Socket::Ptr createSocket(const EventPoller::Ptr& poller, const Buffer::Ptr& buf = nullptr, sockaddr* addr = nullptr, int addrLen = 0);

    bool _cloned{false};
    Socket::Ptr _socket;
    std::shared_ptr<Timer> _timer;
    onCreateSocketCB _onCreateSocketCB;

    // recursive_mutex 减小死锁可能性, 但效率不如mutex; 可能从其它Server复制,所以用shared_ptr
    std::shared_ptr<std::recursive_mutex> _sessionMtx;
    // key: socket hash id, value: sessionHelper
    std::shared_ptr<std::unordered_map<std::string, SessionHelper::Ptr>> _sessionMap;
    // 主server 持有cloned server 的引用
    std::unordered_map<EventPoller*, Ptr> _clonedServer;
    // Session 构建器, 用于构建不同类型的Session(针对不同应用场景实现不同的Session子类，利用多态实现不同功能)
    std::function<SessionHelper::Ptr(const UDPServer::Ptr&, const Socket::Ptr&)> _sessionBuilder;
};

template <typename SessionType>
inline void UDPServer::start(uint16_t port, const std::string& host) {
    _sessionBuilder = [](const UDPServer::Ptr& server, const Socket::Ptr sock) {
        auto session = std::make_shared<SessionType>(sock);
        auto tmpOnCreateSocketCB = server->_onCreateSocketCB;
        session->setOnCreateSocket([tmpOnCreateSocketCB](const EventPoller::Ptr& poller) {
            return tmpOnCreateSocketCB(poller, nullptr, nullptr, 0);
        });
        std::weak_ptr<Server> tserver = server;
        return std::make_shared<SessionHelper>(server, session);
    };

    // 主Server创建, 复制的Server 共享
    _sessionMtx = std::make_shared<std::recursive_mutex>();
    _sessionMap = std::make_shared<std::unordered_map<std::string, SessionHelper::Ptr>>();

    if (!_socket->bindUdpSocket(port, host)) {
        throw std::runtime_error("BindUdpSocket faild: " + host + ":" + std::to_string(port));
    }

    // 父类的enable_shared_from_this, 子类用要强制转换
    std::weak_ptr<UDPServer> weakThis = std::dynamic_pointer_cast<UDPServer>(shared_from_this());
    _timer = std::make_shared<Timer>(
        2.0f, _poller,
        [weakThis]() -> bool {
            auto strong_self = weakThis.lock();
            if (!strong_self) {
                return false;
            }
            strong_self->onManageSession();
            return true;
        });

    //clone server至不同线程，让udp server支持多线程
    EventPollerPool::Instance().for_each(
        [&](const TaskExecutor::Ptr& executor) {
            auto poller = std::dynamic_pointer_cast<EventPoller>(executor);
            if (poller == _poller || !poller) {
                return;
            }
            auto& serverRef = _clonedServer[poller.get()];
            if (!serverRef) {
                serverRef = std::make_shared<UDPServer>(poller);
            }
            if (serverRef) {
                serverRef->cloneFrom(*this);
            }
        });

    InfoL << "UDP server bind to [" << host << "]: " << port;
};

}  // namespace myNet

#endif  // UDPServer_hpp