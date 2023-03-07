#include "Server.hpp"

namespace myNet {

Session::Ptr SessionMap::get(const std::string& tag) {
    std::lock_guard<std::mutex> lck(_mtx);
    auto it = _mapSession.find(tag);
    if (it == _mapSession.end()) {
        return nullptr;
    }
    return it->second.lock();
}

void SessionMap::for_each_session(const std::function<void(const std::string& id, const Session::Ptr& session)>& cb) {
    std::lock_guard<std::mutex> lck(_mtx);

    for (auto iter = _mapSession.begin(); iter != _mapSession.end();) {
        auto session = iter->second.lock();
        if (!session) {
            iter = _mapSession.erase(iter);
            continue;
        }
        cb(iter->first, session);
        ++iter;
    }
}

bool SessionMap::del(const std::string& tag) {
    std::lock_guard<std::mutex> lck(_mtx);
    return _mapSession.erase(tag);
}

bool SessionMap::add(const std::string& tag, const Session::Ptr& session) {
    std::lock_guard<std::mutex> lck(_mtx);
    return _mapSession.emplace(tag, session).second;
}

SessionHelper::SessionHelper(const std::weak_ptr<Server>& server, Session::Ptr session)
    : _server(server), _session(std::move(session)) {
    _id = _session->getIdentifier();
    _sessionMap = SessionMap::Instance().shared_from_this();
    _sessionMap->add(_id, _session);
}

SessionHelper::~SessionHelper() {
    if (!_server.lock()) {
        _session->onErr(SocketException(Errcode::Err_other, "Server shutdown."));
    }
    _sessionMap->del(_id);
}

}  // namespace myNet