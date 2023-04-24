#include "TCPClient.hpp"

namespace myNet {
TCPClient::TCPClient(const EventPoller::Ptr& poller) : SocketHelper(nullptr) {
    setPoller(poller ? poller : EventPollerPool::Instance().getPoller());
    setOnCreateSocket([](const EventPoller::Ptr& poller) { return Socket::createSocket(poller, true); });
}

void TCPClient::connect(const std::string& url, uint16_t port, float timeoutSec, uint16_t localPort) {
    auto weakThis = weak_from_this();

    _timer = std::make_shared<Timer>(2.0f, getPoller(), [weakThis]() {
        auto sharedThis = weakThis.lock();
        if (!sharedThis) {
            return false;
        }

        sharedThis->onManager();
        return true;
    });

    setSock(createSocket());

    // 原代码写得比较奇怪，SocketHelper 只有在setSock 时改变sock
    // 并且只有SocketHelper 的构造函数调用setSock，这么写应该不会出问题
    std::weak_ptr<Socket> weakSocket = getSocket();
    getSocket()->setOnErr([weakThis, weakSocket](const SocketException& e) {
        auto sharedThis = weakThis.lock();
        auto sharedSocket = weakSocket.lock();
        if (!sharedThis || !sharedSocket) {
            return;
        }

        sharedThis->_timer.reset();
        sharedThis->onErr(e);
    });
    getSocket()->connect(
        url, port,
        [weakThis](const SocketException& e) {
            auto sharedThis = weakThis.lock();
            if (!sharedThis) {
                return;
            }
            sharedThis->onSocketConnect(e);
        },
        timeoutSec, _netAdapter, localPort);
}

void TCPClient::shutdown(const SocketException& socketException) {
    _timer.reset();
    SocketHelper::shutdown(socketException);
}

bool TCPClient::alive() const {
    // 只有connect 之后_timer才会初始化
    if (_timer) {
        return true;
    }
    if (auto sock = getSocket()) {
        return sock->getFd() > 0;
    }
    return false;
}

void TCPClient::onSocketConnect(const SocketException& e) {
    if (e) {
        _timer.reset();
        onConnect(e);
        return;
    }
    std::weak_ptr<Socket> weakSocket = getSocket();
    auto weakThis = weak_from_this();

    getSocket()->setOnFlush([weakThis, weakSocket]() {
        auto sharedSocket = weakSocket.lock();
        auto sharedThis = weakThis.lock();
        if (!sharedThis || !sharedSocket) {
            return false;
        }
        sharedThis->onFlush();
        return true;
    });

    getSocket()->setOnRead([weakThis, weakSocket](const Buffer::Ptr& buf, sockaddr* addr, int) {
        auto sharedSocket = weakSocket.lock();
        auto sharedThis = weakThis.lock();
        if (!sharedThis || !sharedSocket) {
            return;
        }
        try {
            sharedThis->onRecv(buf);
        } catch (std::exception& ex) {
            sharedThis->shutdown(SocketException(Errcode::Err_other, ex.what()));
        }
    });

    onConnect(e);
}

}  // namespace myNet