#ifndef TCPServer_h
#define TCPServer_h

// #include "Poller/Timer.h"
#include "../myPoller/EventPollerApp.hpp"
#include "Server.hpp"
#include "uv_errno.hpp"

namespace myNet {

class TCPServer : public Server {
public:
    using Ptr = std::shared_ptr<TCPServer>;

    explicit TCPServer(const EventPoller::Ptr& poller = nullptr);
    ~TCPServer() override;

    template <typename SessionType>
    void start(uint16_t port, const std::string& host = "::", uint32_t backlog = 32);

    uint16_t getPort() {
        if (!_socket) return 0;
        return _socket->get_localPort();
    };

    Socket::Ptr getSocket() const { return _socket; }

    void setOnCreateSocket(Socket::onCreateSocketCB cb);

protected:
    virtual void cloneFrom(const TCPServer& that);

    virtual Session::Ptr onAcceptConnection(const Socket::Ptr& sock);

private:
    void onManagerSession();

    TCPServer::Ptr getServer(const EventPoller*) const;

    bool _isOnManager{false};
    const TCPServer* _parent{nullptr};
    Socket::Ptr _socket;
    std::shared_ptr<Timer> _timer;
    Socket::onCreateSocketCB _onCreateSocket;
    std::unordered_map<SessionHelper*, SessionHelper::Ptr> _sessionMap;
    std::function<SessionHelper::Ptr(const TCPServer::Ptr&, const Socket::Ptr&)> _sessionBuilder;
    std::unordered_map<const EventPoller*, Ptr> _clonedServer;
};

template <typename SessionType>
inline void TCPServer::start(uint16_t port, const std::string& host, uint32_t backlog) {
    _sessionBuilder = [](const TCPServer::Ptr& server, const Socket::Ptr& sock) {
        auto session = std::make_shared<SessionType>(sock);
        session->setOnCreateSocket(server->_onCreateSocket);
        return std::make_shared<SessionHelper>(server, session);
    };

    if (!_socket->listen(port, host.c_str(), backlog)) {
        std::string err = (StrPrinter << "Listen on " << host << " " << port << " failed: " << uv_strerror(uv_translate_posix_error(errno)));
        throw std::runtime_error(err);
    }

    std::weak_ptr<TCPServer> weakThis = std::dynamic_pointer_cast<TCPServer>(shared_from_this());
    _timer = std::make_shared<Timer>(
        2.0f, _poller,
        [weakThis]() -> bool {
            auto strongThis = weakThis.lock();
            if (!strongThis) return false;
            strongThis->onManagerSession();
            return true;
        });

    EventPollerPool::Instance().for_each([&](const TaskExecutor::Ptr& excutor) {
        EventPoller::Ptr poller = std::dynamic_pointer_cast<EventPoller>(excutor);
        if (poller == _poller || !poller) return;
        auto& ref = _clonedServer[poller.get()];
        if (!ref) {
            ref = std::make_shared<TCPServer>(poller);
        }
        if (ref) {
            ref->cloneFrom(*this);
        }
    });

    InfoL << "TCP server listening on [" << host << "]: " << port;
}

}  // namespace myNet

#endif  // TCPServer_h