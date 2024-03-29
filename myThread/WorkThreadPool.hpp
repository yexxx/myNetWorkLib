#ifndef WorkThredPool_hpp
#define WorkThredPool_hpp

#include <memory>

#include "../myPoller/EventPoller.hpp"
#include "TaskExecutor.hpp"
#include "ThreadPool.hpp"

namespace myNet {

static size_t poolSize = 0;
static bool enableCpuAffinity = true;

class WorkThreadPool : public std::enable_shared_from_this<WorkThreadPool>, public TaskExecutorGetter {
  public:
    using Ptr = std::shared_ptr<WorkThreadPool>;

    static WorkThreadPool& Instance() {
        static std::shared_ptr<WorkThreadPool> sharedRet(new WorkThreadPool());
        static auto& ret = *sharedRet;
        return ret;
    }

    static void setPoolSize(size_t size = 0) { poolSize = size; }

    static void setEnableCpuAffinity(bool enable) { enableCpuAffinity = enable; };

    EventPoller::Ptr getPoller() { return std::dynamic_pointer_cast<EventPoller>(getExecutor()); };

    EventPoller::Ptr getFirstPoller() { return std::dynamic_pointer_cast<EventPoller>(_threads.front()); };

  private:
    WorkThreadPool() { addPoller("WorkPoller", poolSize, ThreadPool::PRIORITY_LOWEST, false, enableCpuAffinity); };
};

} // namespace myNet

#endif // WorkThredPool_hpp