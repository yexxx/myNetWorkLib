#ifndef Server_hpp
#define Server_hpp

#include <memory>
#include <mutex>
#include <unordered_map>

#include "Poller/EventPoller.h"
#include "Session.hpp"

namespace myNet {
// 全局的 Session 记录对象, 方便后面管理
class SessionMap : public std::enable_shared_from_this<SessionMap> {
public:
    friend class SessionHelper;
    using Ptr = std::shared_ptr<SessionMap>;

    static SessionMap &Instance() {
        static SessionMap sessionMap;
        return sessionMap;
    }

    Session::Ptr get(const std::string &tag);

    void for_each_session(const std::function<void(const std::string &id, const Session::Ptr &session)> &cb);

private:
    SessionMap() = default;

    bool del(const std::string &tag);

    bool add(const std::string &tag, const Session::Ptr &session);

    std::mutex _mtx;
    std::unordered_map<std::string, std::weak_ptr<Session>> _mapSession;
};

class SessionHelper {
public:
    using Ptr = std::shared_ptr<SessionHelper>;

    SessionHelper(const std::weak_ptr<Server> Server, Session::Ptr session);
    ~SessionHelper();

    const Session::Ptr getSession() const { return _session; };

private:
    std::string _id;
    Session::Ptr _session;
    std::weak_ptr<Server> _server;
    SessionMap::Ptr _sessionMap{SessionMap::Instance().shared_from_this()};
};

class Server : public std::enable_shared_from_this<Server> {
public:
    using Ptr = std::shared_ptr<Server>;

    explicit Server(toolkit::EventPoller::Ptr poller = nullptr) {
        _poller = (poller ? poller : toolkit::EventPollerPool::Instance().getPoller());
    };
    virtual ~Server() = default;

protected:
    toolkit::EventPoller::Ptr _poller;
};

}  // namespace myNet

#endif  // Server_hpp