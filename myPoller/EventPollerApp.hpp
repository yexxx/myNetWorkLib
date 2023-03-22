#ifndef EventPollerApp_hpp
#define EventPollerApp_hpp

#include <memory>

#include "EventPoller.hpp"
#include "Pipe.hpp"

namespace myNet {

class PipeEventPoller {
private:
    std::shared_ptr<Pipe> _pipe;
};

class Timer {
};

}  // namespace myNet

#endif  // EventPollerApp_hpp