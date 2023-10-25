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

    static SessionMap& Instance() {
        // 静态变量在程序结束时才会被析构
        // 这个地方使用shared_ptr 是因为之后(Server.cpp:42)要获取
        static auto s_instance = std::make_shared<SessionMap>();
        static SessionMap& s_instance_ref = *s_instance;
        return s_instance_ref;
    }

    SessionMap() = default;
    ~SessionMap() = default;

    Session::Ptr get(const std::string& tag);

    void for_each_session(const std::function<void(const std::string& id, const Session::Ptr& session)>& cb);

  private:
    bool del(const std::string& tag);

    bool add(const std::string& tag, const Session::Ptr& session);

    std::mutex _mtx;
    std::unordered_map<std::string, std::weak_ptr<Session>> _mapSession;
};

class Server;

class SessionHelper {
  public:
    using Ptr = std::shared_ptr<SessionHelper>;
    SessionHelper(){};
    SessionHelper(const std::weak_ptr<Server>& Server, Session::Ptr session);
    ~SessionHelper();

    const Session::Ptr getSession() const { return _session; };

  private:
    std::string _id;
    Session::Ptr _session;
    std::weak_ptr<Server> _server;
    SessionMap::Ptr _sessionMap;
};

class Server : public std::enable_shared_from_this<Server> {
  public:
    using Ptr = std::shared_ptr<Server>;

    explicit Server(EventPoller::Ptr poller = nullptr) { _poller = (poller ? poller : EventPollerPool::Instance().getPoller()); };
    virtual ~Server() = default;

  protected:
    EventPoller::Ptr _poller;
};

} // namespace myNet

#endif // Server_hpp