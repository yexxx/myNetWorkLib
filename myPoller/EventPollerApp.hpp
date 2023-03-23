#ifndef EventPollerApp_hpp
#define EventPollerApp_hpp

#include <functional>
#include <memory>

#include "EventPoller.hpp"
#include "Pipe.hpp"

namespace myNet {

class PipeEventPoller {
public:
    using Ptr = std::shared_ptr<PipeEventPoller>;

    PipeEventPoller(const EventPoller::Ptr& poller = nullptr, const std::function<void(const char* buf, int size)> cb = nullptr);

    ~PipeEventPoller();

    void send(const char* buf, int size = 0) { _pipe->write(buf, size); }

private:
    std::shared_ptr<Pipe> _pipe;
    EventPoller::Ptr _poller;
};

class Timer {
public:
    using Ptr = std::shared_ptr<Timer>;

    // cb 返回true 表示重复
    Timer(float second, const EventPoller::Ptr& poller, const std::function<bool()>& cb);

    ~Timer();

private:
    std::shared_ptr<EventPoller::DelayTask> _task;
    EventPoller::Ptr _poller;
};

}  // namespace myNet

#endif  // EventPollerApp_hpp