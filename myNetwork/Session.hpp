#ifndef Session_hpp
#define Session_hpp

#include <atomic>
#include <memory>

#include "Server.hpp"
#include "Socket.hpp"

namespace myNet {
// session 用于标识符
static std::atomic<uint64_t> SessionIndex;

class Session : std::enable_shared_from_this<Session>, public SocketHelper {
public:
    using Ptr = std::shared_ptr<Session>;

    Session(const Socket::Ptr &sock) : SocketHelper(sock){};
    ~Session() override = default;

    virtual void attachServer(const Server &server) {}

    // 数据接收
    virtual void onRecv(const Buffer::Ptr &buf) = 0;

    virtual void onErr(const SocketException &err) = 0;

    // 超时管理
    virtual void onManager() = 0;

    virtual void safeShutdown(const SocketException &ex = SocketException(Errcode::Err_shutdown, "Self shutdown."));

    // 标识符 格式: SessionIndex-sockfd
    std::string getIdentifier() const override;

private:
    mutable std::string _id;
};

class TCPSession : public Session {};
class UDPSession : public Session {};

}  // namespace myNet

#endif  // Session_hpp