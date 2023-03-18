#include "TCPServer.hpp"

namespace myNet {
TCPServer::TCPServer(const toolkit::EventPoller::Ptr& poller) : Server(poller) {
    setOnCreateSocket(nullptr);
    _socket = _onCreateSocket(poller);
    // accept 之前，创建socket
    _socket->setOnCreateSocket(
        [this](const toolkit::EventPoller::Ptr& poller) {
            // 这个地方会报错，先注释掉
            assert(_poller->isCurrentThread());
            return _onCreateSocket(toolkit::EventPollerPool::Instance().getPoller(false));
        });
    _socket->setOnAccept(
        [this](Socket::Ptr& sock, std::shared_ptr<void>& complete) {
            auto sockPoller = sock->getPoller().get();
            auto server = getServer(sockPoller);
            sockPoller->async(
                [server, sock, complete]() {
                    server->onAcceptConnection(sock);
                });
        });
}

TCPServer::~TCPServer() {
    if (!_parent && _socket->getFd() != -1) {
        InfoL << "Close tcp server [" << _socket->get_localIP() << "]: " << _socket->get_localPort();
    }
    _timer.reset();
    _socket.reset();
    _sessionMap.clear();
    _clonedServer.clear();
}

void TCPServer::setOnCreateSocket(Socket::onCreateSocketCB cb) {
    if (cb) {
        _onCreateSocket = std::move(cb);
    } else {
        _onCreateSocket = [](const toolkit::EventPoller::Ptr& poller) {
            return Socket::createSocket(poller, false);
        };
    }
    for (auto& server : _clonedServer) {
        server.second->setOnCreateSocket(cb);
    }
}

void TCPServer::cloneFrom(const TCPServer& that) {
    if (!that._socket) {
        throw std::invalid_argument("TcpServer::cloneFrom other with null socket.");
    }
    _onCreateSocket = that._onCreateSocket;
    _sessionBuilder = that._sessionBuilder;
    _socket->cloneFromListenSocket(*(that._socket));

    std::weak_ptr<TCPServer> weakThis = std::dynamic_pointer_cast<TCPServer>(shared_from_this());
    _timer = std::make_shared<toolkit::Timer>(
        2.0f,
        [weakThis]() -> bool {
            auto strongThis = weakThis.lock();
            if (!strongThis) {
                return false;
            }
            strongThis->onManagerSession();
            return true;
        },
        _poller);

    _parent = &that;
}

Session::Ptr TCPServer::onAcceptConnection(const Socket::Ptr& sock) {
    // 这儿之前会出问题，不知道为什么，暂时未复现
    // 已复现，暂时注释掉以解决
    assert(_poller->isCurrentThread());
    std::weak_ptr<TCPServer> weakThis = std::dynamic_pointer_cast<TCPServer>(shared_from_this());

    auto sessionHelper = _sessionBuilder(std::dynamic_pointer_cast<TCPServer>(shared_from_this()), sock);
    auto session = sessionHelper->getSession();
    session->attachServer(*this);

    assert(true == _sessionMap.emplace(sessionHelper.get(), sessionHelper).second);

    std::weak_ptr<Session> weakSession = session;

    sock->setOnRead(
        [weakSession](const Buffer::Ptr& buf, sockaddr* addr, int) {
            auto strongSession = weakSession.lock();
            if (!strongSession) {
                return;
            }
            try {
                strongSession->onRecv(buf);
            } catch (SocketException& e) {
                strongSession->shutdown(e);
            } catch (std::exception& e) {
                strongSession->shutdown(SocketException(Errcode::Err_shutdown, e.what()));
            }
        });

    SessionHelper* helperPtr = sessionHelper.get();
    sock->setOnErr(
        [weakThis, weakSession, helperPtr](const SocketException& err) {
            // 出该作用域时从sessionMap 中删除session
            std::shared_ptr<void> deleter(
                nullptr,
                [weakThis, helperPtr](void*) {
                    auto strongThis = weakThis.lock();
                    if (!strongThis) {
                        return;
                    }
                    assert(strongThis->_poller->isCurrentThread());
                    // 管理时需在map 中遍历，不能直接删除
                    if (strongThis->_isOnManager) {
                        strongThis->_sessionMap.erase(helperPtr);
                    } else {
                        strongThis->_poller->async(
                            [weakThis, helperPtr]() {
                                auto strongThis = weakThis.lock();
                                if (!strongThis) {
                                    return;
                                }
                                strongThis->_sessionMap.erase(helperPtr);
                            },
                            false);
                    }
                });

            auto strongSession = weakSession.lock();
            if (strongSession) {
                strongSession->onErr(err);
            }
        });

    return session;
}

void TCPServer::onManagerSession() {
    assert(_poller->isCurrentThread());

    _isOnManager = true;
    std::shared_ptr<void> deleter(nullptr, [this](void*) { _isOnManager = false; });

    for (auto& [_, session] : _sessionMap) {
        try {
            session->getSession()->onManager();
        } catch (std::exception& e) {
            WarnL << e.what();
        }
    };
}

TCPServer::Ptr TCPServer::getServer(const toolkit::EventPoller* poller) const {
    auto parent = (_parent ? _parent : this);
    auto& cloneServer = parent->_clonedServer;

    auto iter = cloneServer.find(poller);
    if (iter != cloneServer.end()) {
        return iter->second;
    }

    return std::static_pointer_cast<TCPServer>(const_cast<TCPServer*>(parent)->shared_from_this());
}

}  // namespace myNet