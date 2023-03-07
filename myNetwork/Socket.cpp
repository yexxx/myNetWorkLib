#include "Socket.hpp"

#include <type_traits>

#include "SocketUtil.hpp"
#include "Thread/WorkThreadPool.h"
#include "Thread/semaphore.h"
#include "Util/logger.h"
#include "uv_errno.hpp"

namespace myNet {

static SocketException
toSocketException(int error) {
    switch (error) {
        case 0:
        case UV_EAGAIN: return SocketException(Errcode::Err_sucess, "Success.");
        case UV_ECONNREFUSED: return SocketException(Errcode::Err_refused, uv_strerror(uv_translate_posix_error(error)));
        case UV_ETIMEDOUT: return SocketException(Errcode::Err_timeout, uv_strerror(uv_translate_posix_error(error)));
        default: return SocketException(Errcode::Err_other, uv_strerror(uv_translate_posix_error(error)));
    }
}

static SocketException getSocketErr(const SocketFD::Ptr &sock) {
    int error = 0, len = sizeof(int);
    getsockopt(sock->getFd(), SOL_SOCKET, SO_ERROR, (char *)&error, (socklen_t *)&len);
    error = uv_translate_posix_error(error);
    return toSocketException(error);
}

Socket::Socket(const toolkit::EventPoller::Ptr poller, bool enableMutex)
    : _mtxEvent(enableMutex), _mtxSocketFd(enableMutex), _mtxSendBufSending(enableMutex), _mtxSendBufWaiting(enableMutex), _poller(poller) {
    if (_poller == nullptr) _poller = toolkit::EventPollerPool::Instance().getPoller();

    setOnRead(nullptr);
    setOnErr(nullptr);
    setOnAccept(nullptr);
    setOnFlush(nullptr);
    setOnCreateSocket(nullptr);
    setOnSendResult(nullptr);
}

Socket::~Socket() {
    closeSocket();
    _sendBufSending.clear();  // buffer析构时操作时触发回调，手动触发避免析构顺序问题
}

Socket::Ptr Socket::createSocket(const toolkit::EventPoller::Ptr &poller, bool enable_mutex) {
    return std::make_shared<Socket>(poller, enable_mutex);
}

void Socket::setOnRead(onReadCB &&readCB) {
    std::lock_guard<MutexWrapper> lck(_mtxEvent);
    if (readCB != nullptr) {
        _onReadCB = readCB;
    } else {
        _onReadCB = [](const Buffer::Ptr &buf, sockaddr *addr, int addrLen) {
            WarnL << "Socket not set read callback, data: " << buf->size();
        };
    }
}
void Socket::setOnErr(onErrCB &&errCB) {
    std::lock_guard<MutexWrapper> lck(_mtxEvent);
    if (errCB != nullptr)
        _onErrCB = errCB;
    else
        _onErrCB = [](const SocketException &err) {
            WarnL << "Socket not set err callback, err: " << err.what();
        };
}
void Socket::setOnAccept(onAcceptCB &&acceptCB) {
    std::lock_guard<MutexWrapper> lck(_mtxEvent);
    if (acceptCB != nullptr)
        _onAcceptCB = acceptCB;
    else
        _onAcceptCB = [](Ptr &sock, std::shared_ptr<void> &complete) {
            WarnL << "Socket not set accept callback, peer fd: " << sock->getFd();
        };
}
void Socket::setOnFlush(onFlushCB &&flushCB) {
    std::lock_guard<MutexWrapper> lck(_mtxEvent);
    if (flushCB != nullptr)
        _onFlushCB = flushCB;
    else
        _onFlushCB = []() {
            return true;
        };
};
void Socket::setOnCreateSocket(onCreateSocketCB &&createSocketCB) {
    std::lock_guard<MutexWrapper> lck(_mtxEvent);
    if (createSocketCB != nullptr)
        _onCreateSocketCB = createSocketCB;
    else
        _onCreateSocketCB = [](const toolkit::EventPoller::Ptr &poller) {
            return nullptr;
        };
};
void Socket::setOnSendResult(onSendResultCB &&sendResultCB) {
    std::lock_guard<MutexWrapper> lck(_mtxEvent);
    _onSendResultCB = sendResultCB;
};

void Socket::connect(const std ::string &url, uint16_t port, const onErrCB &errCB,
                     float timeoutSec, const std::string &localIP, uint16_t localPort) {
    std::weak_ptr<Socket> weakThis = shared_from_this();
    // 异步执行
    _poller->async(
        [=]() /* 值传递this指针,lambda函数内可以修改类的属性值 */ {
            auto sharedThis = weakThis.lock();
            if (sharedThis == nullptr) return;

            // 重置当前socket
            closeSocket();

            // connect callback
            auto connectCB = [errCB, weakThis](const SocketException &err) {
                auto sharedThis = weakThis.lock();
                if (!sharedThis) return;
                sharedThis->_asyncConnectCB = nullptr;
                sharedThis->_conTime = nullptr;
                if (err) {
                    std::lock_guard<MutexWrapper> lck(sharedThis->_mtxSocketFd);
                    sharedThis->_socketFd = nullptr;
                }
                errCB(err);
            };

            // asyncConnectCB
            auto asyncConnectCB = std::make_shared<std::function<void(int)>>(
                [weakThis, connectCB](int sockfd) {
                    auto sharedThis = weakThis.lock();

                    // 错误处理
                    if (sharedThis == nullptr) {
                        if (sockfd != -1) close(sockfd);
                        return;
                    }
                    if (sockfd == -1) {
                        connectCB(SocketException(Errcode::Err_dns, "Sockfd = -1."));
                        return;
                    }

                    std::weak_ptr<SocketFD> weakSocketFd = sharedThis->makeSocketFD(sockfd, SocketType::Socket_TCP);

                    // 监听socket是否可写
                    if (-1 ==
                        sharedThis->_poller->addEvent(sockfd, toolkit::EventPoller::Event_Write,
                                                      [weakThis, weakSocketFd, connectCB](int event) {
                                                          auto sharedThis = weakThis.lock();
                                                          auto sharedSockedFd = weakSocketFd.lock();
                                                          if (sharedThis && sharedSockedFd) sharedThis->onConnected(sharedSockedFd, connectCB);
                                                      })) {
                        connectCB(SocketException(Errcode::Err_other, "Add event to poller failed when start connect."));
                        return;
                    }

                    // set fd
                    std::lock_guard<MutexWrapper> lck(sharedThis->_mtxSocketFd);
                    sharedThis->_socketFd = sharedThis->makeSocketFD(sockfd, SocketType::Socket_TCP);
                });

            // 如果url是ip就在该线程执行，否则异步解析dns
            if (toolkit::isIP(url.data())) {
                (*asyncConnectCB)(SocketUtil::connect(url.data(), port, true, localIP.data(), localPort));
            } else {
                std::weak_ptr<std::function<void(int)>> weakTask = asyncConnectCB;
                toolkit::WorkThreadPool::Instance().getExecutor()->async(
                    [url, port, localIP, localPort, weakTask, poller = _poller]() {
                        int tsocketFd = SocketUtil::connect(url.data(), port, true, localIP.data(), localPort);
                        poller->async(
                            [weakTask, tsocketFd]() {
                                auto sharedTask = weakTask.lock();
                                if (sharedTask) {
                                    (*sharedTask)(tsocketFd);
                                } else {
                                    if (tsocketFd != -1)
                                        close(tsocketFd);
                                }
                            });
                    });
                sharedThis->_asyncConnectCB = asyncConnectCB;
            }

            // 定时器
            sharedThis->_conTime = std::make_shared<toolkit::Timer>(
                timeoutSec, [connectCB]() {
                    connectCB(SocketException(Errcode::Err_timeout, "Connect timeout."));
                    return false;
                },
                sharedThis->_poller);
        });
}

void Socket::onConnected(const SocketFD::Ptr &sock, const onErrCB &errcb) {
    auto err = getSocketErr(sock);
    if (err) {
        errcb(err);
        return;
    }

    _poller->delEvent(sock->getFd());
    if (!attachEvent(sock)) {
        errcb(SocketException(Errcode::Err_other, "Add event to poller faild when connected."));
        return;
    }

    errcb(err);
};

// 转toolkit::Buffer::Ptr -> myNet::BufferRaw::Ptr
BufferRaw::Ptr translateToolkitBufferRaw(toolkit::BufferRaw::Ptr that) {
    auto t = std::make_shared<BufferRaw>();
    t->assign(that->data(), that->size());
    t->setCapacity(that->getCapacity());
    return t;
}

// 注册事件
bool Socket::attachEvent(const SocketFD::Ptr &sock) {
    std::weak_ptr<Socket> weakThis = shared_from_this();
    std::weak_ptr<SocketFD> weakSock = sock;
    _enableRecv = true;
    _readBuffer = translateToolkitBufferRaw(_poller->getSharedBuffer());
    auto isUDP = (sock->getType() == SocketType::Socket_UDP);

    return 0 ==
           _poller->addEvent(sock->getFd(), toolkit::EventPoller::Event_Read | toolkit::EventPoller::Event_Write | toolkit::EventPoller::Event_Error,
                             [weakThis, weakSock, isUDP](int event) {
                                 auto sharedThis = weakThis.lock();
                                 auto sharedSock = weakSock.lock();
                                 if (sharedThis && sharedSock) {
                                     if (event & toolkit::EventPoller::Event_Read) sharedThis->onRead(sharedSock, isUDP);
                                     if (event & toolkit::EventPoller::Event_Write) sharedThis->onWriteable(sharedSock);
                                     if (event & toolkit::EventPoller::Event_Error) sharedThis->emitErr(getSocketErr(sharedSock));
                                 }
                             });
}

int Socket::onAccept(const SocketFD::Ptr &sock, int event) noexcept {
    int fd;
    while (true) {
        if (event & toolkit::EventPoller::Event_Read) {
            do {
                fd = static_cast<int>(accept(sock->getFd(), nullptr, nullptr));
            } while (-1 == fd && UV_EINTR == uv_translate_posix_error(errno));
            if (fd == -1) {
                if (UV_EAGAIN == uv_translate_posix_error(errno)) { return 0; }  // 没有新连接
                emitErr(toSocketException(errno));
                // 错误信息：Accept socket faild
                return -1;
            }

            // SocketUtil::setNoSigpipe(fd);
            SocketUtil::setNoBlocked(fd);
            SocketUtil::setNoDelay(fd);
            SocketUtil::setSendBuf(fd);
            SocketUtil::setRecvBuf(fd);
            SocketUtil::setCloseWait(fd);
            SocketUtil::setCloExec(fd);

            Socket::Ptr peerSock;
            try {
                // 为什么捕获异常？
                std::lock_guard<MutexWrapper> lck(_mtxEvent);
                peerSock = _onCreateSocketCB(_poller);
            } catch (std::exception &e) {
                ErrorL << "Exception occurred when emit on_before_accept: " << e.what();
                close(fd);
                continue;
            }

            if (!peerSock) {
                // 共用poller并关闭互斥锁
                peerSock = createSocket(_poller, false);
            }

            auto peerSockFd = peerSock->setPeerSock(fd);

            std::shared_ptr<void> completed(nullptr,
                                            [peerSock, peerSockFd](void *) {
                                                try {
                                                    if (!peerSock->attachEvent(peerSockFd)) {
                                                        peerSock->emitErr(SocketException(Errcode::Err_eof, "Add event to poller failed when accept a socket."));
                                                    }
                                                } catch (std::exception &e) {
                                                    ErrorL << "Exception occurred: " << e.what();
                                                }
                                            });

            try {
                std::lock_guard<MutexWrapper> lck(_mtxEvent);
                _onAcceptCB(peerSock, completed);
            } catch (std::exception &e) {
                ErrorL << "Exception occurred when emit on_accept: " << e.what();
                continue;
            }
        }

        if (event & toolkit::EventPoller::Event_Error) {
            auto ex = getSocketErr(sock);
            emitErr(ex);
            ErrorL << "TCP listener occurred a err: " << ex.what();
            return -1;
        }
    }
}

ssize_t Socket::onRead(const SocketFD::Ptr &sock, bool isUdp) noexcept {
    ssize_t accum = 0, nread = 0;
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    auto buf = _readBuffer->data();
    auto capacity = _readBuffer->getCapacity() - 1;

    while (_enableRecv) {
        do {
            nread = recvfrom(sock->getFd(), buf, capacity, 0, (sockaddr *)&addr, &len);
        } while (-1 == nread && UV_EINTR == uv_translate_posix_error(errno));  // 4: Interrupted system call

        if (nread == 0) {  // 连接中断或eof时会返回0
            if (isUdp) {
                WarnL << "Recv eof on udp socket[" << sock->getFd() << "]";
            } else {
                emitErr(SocketException(Errcode::Err_eof, "Eof."));
            }
            return accum;
        }
        if (nread == -1) {
            auto err = uv_translate_posix_error(errno);
            if (UV_EAGAIN != err) {
                if (isUdp) {
                    WarnL << "Recv err on udp socket[" << sock->getFd() << "]: " << uv_strerror(uv_translate_posix_error(errno));
                } else {
                    emitErr(toSocketException(err));
                }
            }
            return accum;
        }

        if (_enableSpeed) _recvSpeed += nread;

        accum += nread;
        buf[nread] = '\0';
        _readBuffer->setSize(nread);

        // 触发回调,处理buf
        std::lock_guard<MutexWrapper> lck(_mtxEvent);
        _onReadCB(_readBuffer, (sockaddr *)&addr, len);  // 异常处理？
    }
    return 0;
};

// private方法
bool Socket::listen(const SocketFD::Ptr &sock) {
    closeSocket();
    std::weak_ptr<SocketFD> weakSock = sock;
    std::weak_ptr<Socket> weakThis = shared_from_this();
    _enableRecv = true;

    if (-1 ==
        _poller->addEvent(sock->getFd(), toolkit::EventPoller::Event_Read | toolkit::EventPoller::Event_Error,
                          [weakThis, weakSock](int event) {
                              auto sharedThis = weakThis.lock();
                              auto sharedSock = weakSock.lock();
                              if (sharedThis && sharedSock) sharedThis->onAccept(sharedSock, event);
                          })) return false;

    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    _socketFd = std::move(sock);  // 要不要右值？原代码没有-----------注意shared_ptr的右值构造函数。这个地方sock是const的，有没有move没关系，否则sock会被swap析构
    return true;
}

bool Socket::listen(uint16_t port, const std::string &localIP, int backLog) {
    int sock = SocketUtil::listen(port, localIP.data(), backLog);  // 实现？
    if (sock == -1) return false;
    return listen(makeSocketFD(sock, SocketType::Socket_TCP));
}

bool Socket::bindUdpSocket(uint16_t port, const std::string &localIP, bool enableReuse) {
    closeSocket();

    // int fd = toolkit::SockUtil::bindUdpSock(port, localIP.data(), enableReuse);
    int fd = SocketUtil::bindUdpSocket(port, localIP.data(), enableReuse);
    if (-1 == fd) return false;
    auto sock = makeSocketFD(fd, SocketType::Socket_UDP);
    if (!attachEvent(sock)) return false;
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    _socketFd = std::move(sock);

    return true;
}

ssize_t Socket::send(const char *buf, size_t size, sockaddr *addr, socklen_t addrLen, bool tryFlush) {
    auto bufPtr = BufferRaw::create();
    bufPtr->assign(buf, strlen(buf));
    return send(bufPtr, addr, addrLen, tryFlush);
};
ssize_t Socket::send(const std::string buf, sockaddr *addr, socklen_t addrLen, bool tryFlush) {
    return send(std::make_shared<BufferString>(buf), addr, addrLen, tryFlush);
};
ssize_t Socket::send(const Buffer::Ptr buf, sockaddr *addr, socklen_t addrLen, bool tryFlush) {
    if (!buf || buf->size() == 0) return 0;
    auto size = buf->size();

    {
        std::lock_guard<MutexWrapper> lck(_mtxSendBufWaiting);
        if (addr) {
            _sendBufWaiting.emplace_back(std::make_shared<BufferSock>(buf, addr, addrLen));
        } else {
            _sendBufWaiting.emplace_back(buf);
        }
    }
    if (tryFlush && flushAll()) return -1;

    return size;
};

int Socket::flushAll() {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return -1;
    if (_sendable) {
        return flushData(_socketFd, false) ? 0 : -1;
    } else {
        if (_sendFlushTicker.elapsedTime() > _maxSendBufferMs) {
            emitErr(SocketException(Errcode::Err_other, "Socket send timeout."));
            return -1;
        }
    }
    return 0;
}

bool Socket::emitErr(const SocketException &err) noexcept {
    {
        std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
        if (!_socketFd) return false;  // 执行emitErr时会关闭套接字，_socketFd将被置空
    }

    closeSocket();

    std::weak_ptr<Socket> weakThis = shared_from_this();
    _poller->async(
        [weakThis, err]() {
            auto sharedThis = weakThis.lock();
            if (!sharedThis) return;

            std::lock_guard<MutexWrapper> lck(sharedThis->_mtxEvent);
            sharedThis->_onErrCB(err);  // 异常处理？
        });
    return true;
}

void Socket::enableRecv(bool enabled) {
    if (_enableRecv == enabled) return;
    _enableRecv = enabled;
    _poller->modifyEvent(getFd(),
                         (_enableRecv ? toolkit::EventPoller::Event_Read : 0) |
                             (_sendable ? 0 : toolkit::EventPoller::Event_Write) |  // 可发送时不可写
                             toolkit::EventPoller::Event_Error);
};

void Socket::setSendTimeOutSec(uint32_t sec) {
    _maxSendBufferMs = sec * 1000;
};

bool Socket::isSocketBusy() const {
    return !_sendable.load();
};

// 涉及到fd要加锁
int Socket::getFd() const {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return -1;
    return _socketFd->getFd();
};
SocketType Socket::getType() const {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return SocketType::Socket_Invalid;
    return _socketFd->getType();
};
const toolkit::EventPoller::Ptr &Socket::getPoller() const { return _poller; };

bool Socket::cloneFromListenSocket(const Socket &socket) {
    auto sock = cloneSocketFd(socket);
    if (sock) {
        return listen(sock);
    }
    return false;
};

// ？
bool Socket::cloneFromPeerSocket(const Socket &socket) {
    auto sock = cloneSocketFd(socket);
    if (sock && attachEvent(sock)) {
        std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
        _socketFd = sock;
        return true;
    }
    return false;
};

void Socket::closeSocket() {
    _conTime = nullptr;
    _asyncConnectCB = nullptr;
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    _socketFd = nullptr;
};

bool Socket::bindPeerAddr(const sockaddr *addr, socklen_t addrLen) {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return false;
    if (_socketFd->getType() != SocketType::Socket_UDP) return false;

    if (-1 ==
        ::connect(_socketFd->getFd(), addr, addrLen ? addrLen : SocketUtil::getSockLen(addr))) {
        // 错误信息输出
        return false;
    }
    return true;
};

void Socket::setSendFlag(int flags) { _sockFlags = flags; };
size_t Socket::getSendBufferCount() const {
    size_t ret = 0;
    {
        std::lock_guard<MutexWrapper> lck(_mtxSendBufWaiting);
        ret += _sendBufWaiting.size();
    }
    {
        std::lock_guard<MutexWrapper> lck(_mtxSendBufSending);
        for (auto &b : _sendBufSending) ret += b->count();
    }

    return ret;
};
uint64_t Socket::elapsedTimeAfterFlushed() const { return _sendFlushTicker.elapsedTime(); };

int Socket::getRecvSpeed() {
    _enableSpeed = true;
    return _recvSpeed.getSpeed();
};
int Socket::getSendSpeed() {
    _enableSpeed = true;
    return _sendSpeed.getSpeed();
};

std::string Socket::get_localIP() {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return "";
    return SocketUtil::getLocalIp(_socketFd->getFd());
};
uint16_t Socket::get_localPort() {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return 0;
    return SocketUtil::getLocalPort(_socketFd->getFd());
};
std::string Socket::get_peerIP() {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return "";
    return SocketUtil::getPeerIp(_socketFd->getFd());
};
uint16_t Socket::get_peerPort() {
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    if (!_socketFd) return 0;
    return SocketUtil::getPeerPort(_socketFd->getFd());
};
std::string Socket::getIdentifier() const {
    return "Socket" + std::to_string(reinterpret_cast<uint64_t>(this));
};

SocketFD::Ptr Socket::cloneSocketFd(const Socket &sock) {
    SocketFD::Ptr socketFD;
    std::lock_guard<MutexWrapper> lck(sock._mtxSocketFd);
    if (sock._socketFd) {
        socketFD = std::make_shared<SocketFD>(*(sock._socketFd), _poller);
    }
    return socketFD;
};

SocketFD::Ptr Socket::setPeerSock(int fd) {
    closeSocket();
    auto sock = makeSocketFD(fd, SocketType::Socket_TCP);
    std::lock_guard<MutexWrapper> lck(_mtxSocketFd);
    _socketFd = sock;
    return sock;
};

SocketFD::Ptr Socket::makeSocketFD(int fd, SocketType type) {
    return std::make_shared<SocketFD>(fd, type, _poller);
};

void Socket::onWriteable(const SocketFD::Ptr &sock) {
    bool sendEmpty{false}, waitEmpty{false};
    {
        std::lock_guard<MutexWrapper> lck(_mtxSendBufWaiting);
        sendEmpty = _sendBufWaiting.empty();
    }
    {
        std::lock_guard<MutexWrapper> lck(_mtxSendBufSending);
        sendEmpty = _sendBufSending.empty();
    }
    if (sendEmpty && waitEmpty) {
        stopWriteableEvent(sock);
    } else {
        flushData(sock, true);
    }
};

void Socket::onFlush() {
    bool flag{false};
    {
        std::lock_guard<MutexWrapper> lck(_mtxEvent);
        flag = _onFlushCB();
    }
    if (!flag) {
        setOnFlush(nullptr);
    }
};

void Socket::startWriteableEvent(const SocketFD::Ptr &sock) {
    _sendable = false;
    _poller->modifyEvent(sock->getFd(),
                         (_enableRecv ? toolkit::EventPoller::Event_Read : 0) | toolkit::EventPoller::Event_Write | toolkit::EventPoller::Event_Error);
};

void Socket::stopWriteableEvent(const SocketFD::Ptr &sock) {
    _sendable = true;
    _poller->modifyEvent(sock->getFd(),
                         (_enableRecv ? toolkit::EventPoller::Event_Read : 0) | toolkit::EventPoller::Event_Error);
};
bool Socket::flushData(const SocketFD::Ptr &sock, bool pollerThread) {
    std::list<BufferList::Ptr> sendBufSending;
    {
        std::lock_guard<MutexWrapper> lck(_mtxSendBufSending);
        sendBufSending.swap(_sendBufSending);
    }

    if (sendBufSending.empty()) {
        _sendFlushTicker.resetTime();
        {
            std::lock_guard<MutexWrapper> lck(_mtxSendBufWaiting);
            // 若无数据可flush
            if (_sendBufWaiting.empty()) {
                if (pollerThread) {
                    stopWriteableEvent(sock);
                    onFlush();
                }
                return true;
            }

            std::lock_guard<MutexWrapper> lck1(_mtxEvent);
            onSendResultCB sendResultCB;
            if (_enableSpeed) {
                sendResultCB = [this](const Buffer::Ptr &buffer, bool sendSuccess) {
                    if (sendSuccess) {
                        _sendSpeed += buffer->size();
                    }
                    std::lock_guard<MutexWrapper> lck(_mtxEvent);
                    if (_onSendResultCB) {
                        _onSendResultCB(buffer, sendSuccess);
                    }
                };
            } else {
                sendResultCB = _onSendResultCB;
            }
            sendBufSending.emplace_back(
                BufferList::create(std::move(_sendBufWaiting), std::move(sendResultCB), sock->getType() == SocketType::Socket_UDP));
        }
    }
    while (!sendBufSending.empty()) {
        auto n = sendBufSending.front()
                     ->send(sock->getFd(), _sockFlags);
        if (n > 0) {
            if (sendBufSending.front()->empty()) {
                sendBufSending.pop_front();
                continue;
            } else {
                if (!pollerThread) {
                    startWriteableEvent(sock);
                }
                break;
            }
        }

        if (UV_EINTR == uv_translate_posix_error(errno)) {
            if (!pollerThread) {
                startWriteableEvent(sock);
            }
            break;
        }
        if (sock->getType() == SocketType::Socket_UDP) {
            sendBufSending.pop_front();
            WarnL << "Send udp socket[" << sock->getFd() << "] failed, data ignored: " << uv_strerror(uv_translate_posix_error(errno));
            continue;
        }

        emitErr(toSocketException(uv_translate_posix_error(errno)));
        return false;
    }
    if (pollerThread) {
        return flushData(sock, pollerThread);
    } else {
        return true;
    }
};

SocketSender &SocketSender::operator<<(const char *buf) {
    send(buf);
    return *this;
};
SocketSender &SocketSender::operator<<(std::string buf) {
    send(std::move(buf));
    return *this;
};
SocketSender &SocketSender::operator<<(Buffer::Ptr buf) {
    send(std::move(buf));
    return *this;
};

ssize_t SocketSender::send(const char *buf, size_t size) {
    auto buffer = BufferRaw::create();
    buffer->assign(buf, size);
    return send(std::move(buffer));
};
ssize_t SocketSender::send(const std::string buf) {
    return send(std::make_shared<BufferString>(buf));
};

SocketHelper::SocketHelper(const Socket::Ptr &sock) {
    setSock(sock);
    setOnCreateSocket(nullptr);
}

void SocketHelper::setSendFlushFlag(bool tryFlush) {
    _tryFlush = tryFlush;
};
void SocketHelper::setSendFlag(int flags) {
    if (!_sock) return;
    _sock->setSendFlag(flags);
};

bool SocketHelper::isSocketBusy() const {
    if (!_sock) return true;
    return _sock->isSocketBusy();
};

void SocketHelper::setOnCreateSocket(Socket::onCreateSocketCB createSocketCB) {
    if (createSocketCB) {
        _createSocketCB = createSocketCB;
    } else {
        _createSocketCB = [](const toolkit::EventPoller::Ptr &poller) {
            return Socket::createSocket(poller, false);
        };
    }
};

Socket::Ptr SocketHelper::createSocket() {
    return _createSocketCB(_poller);
};

int SocketHelper::flashAll() {
    if (!_sock) return -1;
    return _sock->flushAll();
};

const Socket::Ptr &SocketHelper::getSocket() const {
    return _sock;
};
const toolkit::EventPoller::Ptr &SocketHelper::getPoller() const {
    // assert(_poller); // 这儿去掉会怎么样
    return _poller;
};
std::string SocketHelper::get_localIP() {
    if (_sock && _localIP.empty()) _localIP = _sock->get_localIP();
    return _localIP;
};
uint16_t SocketHelper::get_localPort() {
    if (_sock && _localPort == 0) _localPort = _sock->get_localPort();
    return _localPort;
};
std::string SocketHelper::get_peerIP() {
    if (_sock && _peerIP.empty()) _peerIP = _sock->get_peerIP();
    return _peerIP;
};
uint16_t SocketHelper::get_peerPort() {
    if (_sock && _peerPort == 0) _peerPort = _sock->get_peerPort();
    return _peerPort;
};

toolkit::Task::Ptr SocketHelper::async(toolkit::TaskIn task, bool maySync) {
    return _poller->async(std::move(task), maySync);
};
toolkit::Task::Ptr SocketHelper::async_first(toolkit::TaskIn task, bool maySync) {
    return _poller->async_first(std::move(task), maySync);
};

ssize_t SocketHelper::send(Buffer::Ptr buf) {
    if (!_sock) return -1;
    return _sock->send(buf, nullptr, 0, _tryFlush);
};
void SocketHelper::shutdown(const SocketException &socketException) {
    if (_sock) {
        _sock->emitErr(socketException);
    }
};

void SocketHelper::setPoller(const toolkit::EventPoller::Ptr &poller) {
    _poller = poller;
};
void SocketHelper::setSock(const Socket::Ptr &sock) {
    _peerPort = 0;
    _localPort = 0;
    _peerIP.clear();
    _localIP.clear();
    _sock = sock;
    if (_sock) {
        setPoller(_sock->getPoller());
    }
}

}  // namespace myNet