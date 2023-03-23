#include "EventPollerApp.hpp"

namespace myNet {
PipeEventPoller::PipeEventPoller(const EventPoller::Ptr& poller, const std::function<void(const char* buf, int size)> cb)
    : _poller(poller) {
    if (!_poller) {
        _poller = EventPollerPool::Instance().getPoller();
    }
    _pipe = std::make_shared<Pipe>();

    _poller->addEvent(_pipe->readFd(), EventPoller::Event_Read, [cb, pipe = _pipe](int) {
        int nread = 1024;
        ioctl(pipe->readFd(), FIONREAD, &nread);
        char buf[nread + 1];
        buf[nread] = '\0';
        nread = pipe->read(buf, sizeof(buf));
        if (cb) {
            cb(buf, nread);
        }
    });
}

PipeEventPoller::~PipeEventPoller() {
    if (_pipe) {
        _poller->delEvent(_pipe->readFd(), [pipe = _pipe](bool) {});
    }
}

Timer::Timer(float second, const EventPoller::Ptr& poller, const std::function<bool()>& cb)
    : _poller(poller) {
    if (!_poller) {
        _poller = EventPollerPool::Instance().getPoller();
    }
    _task = _poller->doDelayTask(static_cast<uint64_t>(second * 1000), [cb, second]() {
        try {
            if (cb()) {
                return static_cast<uint64_t>(second * 1000);
            }
            return static_cast<uint64_t>(0);
        } catch (std::exception& e) {
            ErrorL << "Exception occurred when do timer task: " << e.what();
            return (uint64_t)(1000 * second);
        }
    });
}

Timer::~Timer() {
    if (_task) _task->cancel();
}

}  // namespace myNet