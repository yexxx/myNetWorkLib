#include "UDPServer.hpp"

#include <assert.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <exception>

static const uint8_t s_in6_addr_maped[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00};

static std::string makeSockId(sockaddr* addr, int) {
    std::string ret;
    switch (addr->sa_family) {
        case AF_INET: {
            ret.resize(18);
            ret[0] = ((sockaddr_in*)addr)->sin_port >> 8;
            ret[1] = ((sockaddr_in*)addr)->sin_port & 0xFF;
            //ipv4地址统一转换为ipv6方式处理
            memcpy(&ret[2], &s_in6_addr_maped, 12);
            memcpy(&ret[14], &(((sockaddr_in*)addr)->sin_addr), 4);
            return ret;
        }
        case AF_INET6: {
            ret.resize(18);
            ret[0] = ((sockaddr_in6*)addr)->sin6_port >> 8;
            ret[1] = ((sockaddr_in6*)addr)->sin6_port & 0xFF;
            memcpy(&ret[2], &(((sockaddr_in6*)addr)->sin6_addr), 16);
            return ret;
        }
        default: assert(0); return "";
    }
}
namespace myNet {
UDPServer::UDPServer(const toolkit::EventPoller::Ptr& poller) : Server(poller) {
    setOnCreateSocket(nullptr);
    _socket = createSocket(_poller);
    _socket->setOnRead([this](const Buffer::Ptr& buf, sockaddr* addr, int addrLen) {
        onRead(buf, addr, addrLen);
    });
}

UDPServer::~UDPServer() {
    if (!_cloned && _socket->getFd() != -1) {
        InfoL << "Close udp server [" << _socket->get_localIP() << ":" << _socket->get_localPort() << "]";
    }
    _timer.reset();
    _socket.reset();
    _clonedServer.clear();
    if (!_cloned && _sessionMtx && _sessionMap) {
        std::lock_guard<std::recursive_mutex> lck(*_sessionMtx);
        _sessionMap->clear();
    }
}

void UDPServer::setOnCreateSocket(onCreateSocketCB cb) {
    if (cb) {
        _onCreateSocketCB = cb;
    } else {
        _onCreateSocketCB = [](const toolkit::EventPoller::Ptr& poller, const Buffer::Ptr& buf, sockaddr* addr, int addr_len) { return Socket::createSocket(poller, false); };
    }
    // 设置克隆服务器创建socket的方式
    if (!_cloned) {
        for (auto& [_, serverPtr] : _clonedServer) {
            serverPtr->setOnCreateSocket(cb);
        }
    }
}

void UDPServer::cloneFrom(const UDPServer& that) {
    if (!that._socket) {
        throw std::invalid_argument("UdpServer::cloneFrom other with null socket");
    }
    _onCreateSocketCB = that._onCreateSocketCB;
    _sessionBuilder = that._sessionBuilder;
    _sessionMtx = that._sessionMtx;
    _sessionMap = that._sessionMap;

    // clone udp socket
    _socket->bindUdpSocket(that._socket->get_localPort(), that._socket->get_localIP());

    _cloned = true;
}

void UDPServer::onManageSession() {
    // _sessionMap 副本, 防止在管理时_sessionMap 被操作
    std::shared_ptr<std::unordered_map<std::string, myNet::SessionHelper::Ptr>> tmpSessionMap;
    {
        std::lock_guard<std::recursive_mutex> lock(*_sessionMtx);
        tmpSessionMap = _sessionMap;
    }

    toolkit::EventPollerPool::Instance().for_each(
        [tmpSessionMap](const toolkit::TaskExecutor::Ptr& executor) {
            auto poller = std::dynamic_pointer_cast<toolkit::EventPoller>(executor);
            if (!poller) return;
            poller->async(
                [tmpSessionMap]() {
                    for (auto& [_, sessionHelperPtr] : *tmpSessionMap) {
                        // auto& ?
                        auto session = sessionHelperPtr->getSession();
                        if (!session->getPoller()->isCurrentThread()) {
                            continue;
                        }
                        try {
                            session->onManager();
                        } catch (std::exception& e) {
                            WarnL << "Exception occurred when emit onManager: " << e.what();
                        }
                    }
                });
        });
}

void UDPServer::onRead(const Buffer::Ptr& buf, sockaddr* addr, int addrLen, bool isServerFd) {
    if (addrLen == 0) addrLen = sizeof(addr);
    auto sockid = makeSockId(addr, addrLen);

    bool isNew{false};
    if (auto session = getSession(sockid, buf, addr, addrLen, isNew)) {
        if (session->getPoller()->isCurrentThread()) {
            emitSessionRecv(session, buf);
        } else {
            WarnL << "UDP packet incoming from other thread.";
            std::weak_ptr<Session> weakSession = session;

            auto tmpBuf = std::make_shared<BufferString>(buf->toString());
            session->async([weakSession, tmpBuf]() {
                if (auto sharedSession = weakSession.lock()) {
                    UDPServer::emitSessionRecv(sharedSession, tmpBuf);
                }
            });
        }
    }

    if (!isNew) {
        TraceL << "UDP packet incoming from " << (isServerFd ? "server fd" : "other peer fd");
    }
}

void UDPServer::emitSessionRecv(const Session::Ptr& session, const Buffer::Ptr& buf) {
    try {
        session->onRecv(buf);
    } catch (SocketException e) {
        session->shutdown(e);
    } catch (std::exception e) {
        session->shutdown(SocketException(Errcode::Err_shutdown, e.what()));
    }
}

const Session::Ptr& UDPServer::getSession(const std::string& peerId, const Buffer::Ptr& buf, sockaddr* addr, int addrLen, bool& isNew) {
    {
        std::lock_guard<std::recursive_mutex> lock(*_sessionMtx);
        auto iter = _sessionMap->find(peerId);
        if (iter != _sessionMap->end()) {
            // warning: returning reference to local temporary object
            return iter->second->getSession();
        }
    }

    isNew = true;
    return createSession(peerId, buf, addr, addrLen);
}

const Session::Ptr& UDPServer::createSession(const std::string& id, const Buffer::Ptr& buf, sockaddr* addr, int addrLen) {
    auto socket = createSocket(_poller, buf, addr, addrLen);
    if (!socket) {
        // UDP 直接丢弃数据
        return static_cast<Session::Ptr>(nullptr);
    }
    std::weak_ptr<UDPServer> weakThis = std::dynamic_pointer_cast<UDPServer>(shared_from_this());

    auto sessionCreater =
        [this, weakThis, socket, addr, addrLen, id]() -> Session::Ptr {
        auto server = weakThis.lock();
        if (!server) return static_cast<Session::Ptr>(nullptr);

        // 若已有id 对应的session, 找到并返回
        std::lock_guard<std::recursive_mutex> lck(*_sessionMtx);
        auto it = _sessionMap->find(id);
        if (it != _sessionMap->end()) {
            return it->second->getSession();
        }

        // 否则通过socket 创建session
        socket->bindUdpSocket(_socket->get_localPort(), _socket->get_localIP());
        socket->bindPeerAddr(addr, addrLen);
        auto helper = _sessionBuilder(server, socket);
        auto session = helper->getSession();
        session->attachServer(*this);

        std::weak_ptr<Session> weakSession = session;
        // 处理本次会话数据
        socket->setOnRead(
            [weakSession, weakThis, id](const Buffer::Ptr& buf, sockaddr* addr, int addrLen) {
                auto sharedThis = weakThis.lock();
                if (!sharedThis) return;

                // 若是本会话的数据，直接处理
                if (id == makeSockId(addr, addrLen)) {
                    if (auto sharedSession = weakSession.lock()) {
                        emitSessionRecv(sharedSession, buf);
                    }
                    return;
                }

                // 否则分配合适对象处理
                sharedThis->onRead(buf, addr, addrLen, false);
            });

        // 处理本次会话生命周期
        // 这个地方析构顺序可能有问题
        socket->setOnErr(
            [weakThis, weakSession, id](const SocketException& err) {
                std::shared_ptr<void> deleter(
                    nullptr,
                    [weakThis, id](void*) {
                        auto sharedThis = weakThis.lock();
                        if (!sharedThis) {
                            return;
                        }

                        std::lock_guard<std::recursive_mutex> lck(*sharedThis->_sessionMtx);
                        sharedThis->_sessionMap->erase(id);
                    });
                auto sharedSession = weakSession.lock();

                if (!sharedSession) {
                    return;
                }

                sharedSession->onErr(err);
            });

        auto iter = _sessionMap->emplace(id, std::move(helper));
        assert(iter.second);
        return iter.first->second->getSession();
    };

    // 若socket 在本线程，直接创建并返回Session
    if (socket->getPoller()->isCurrentThread()) {
        // warning: returning reference to local temporary object
        return sessionCreater();
    }

    // 否则在socket 所在线程创建并处理数据
    auto tmpBuf = std::make_shared<BufferString>(buf->toString());
    socket->getPoller()->async([sessionCreater, tmpBuf]() {
        auto session = sessionCreater();
        if (session) {
            emitSessionRecv(session, tmpBuf);
        }
    });

    return static_cast<Session::Ptr>(nullptr);
}

Socket::Ptr UDPServer::createSocket(const toolkit::EventPoller::Ptr& poller, const Buffer::Ptr& buf, sockaddr* addr, int addrLen) {
    return _onCreateSocketCB(poller, buf, addr, addrLen);
}

}  // namespace myNet