#include "Session.hpp"

namespace myNet {

void myNet::Session::safeShutdown(const SocketException& ex) {
    std::weak_ptr<Session> weakThis = shared_from_this();
    async_first([weakThis, ex]() {
        auto sharedThis = weakThis.lock();
        if (sharedThis) {
            sharedThis->shutdown(ex);
        }
    });
}

std::string myNet::Session::getIdentifier() const {
    if (_id.empty()) {
        _id = std::to_string(++SessionIndex) + "-" + std::to_string(getSocket()->getFd());
    }
    return _id;
}

}  // namespace myNet