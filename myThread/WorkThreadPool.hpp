#ifndef WorkThredPool_hpp
#define WorkThredPool_hpp

#include <memory>

#include "Poller/EventPoller.h"
#include "TaskExecutor.hpp"
#include "ThreadPool.hpp"

namespace myNet {

static size_t poolSize = 0;
static bool enableCpuAffinity = true;

class WorkThreadPool : public std::enable_shared_from_this<WorkThreadPool>, public TaskExecutorGetter {
public:
    using Ptr = std::shared_ptr<WorkThreadPool>;

    static WorkThreadPool& Instance() {
        static auto sharedRet = std::shared_ptr<WorkThreadPool>();
        static auto& ret = *sharedRet;
        return ret;
    }

    static void setPoolSize(size_t size = 0) { poolSize = size; }

    static void setEnableCpuAffinity(bool enable) { enableCpuAffinity = 0; };

    toolkit::EventPoller::Ptr getPoller() {
        // 类型转换会出错
        return std::dynamic_pointer_cast<toolkit::EventPoller>(getExecutor());
    };

    toolkit::EventPoller::Ptr getFirstPoller() {
        // 类型转换会出错
        return std::dynamic_pointer_cast<toolkit::EventPoller>(_threads.front());
    };

protected:
    WorkThreadPool() {
        addPoller("WorkPoller", poolSize, ThreadPool::PRIORITY_LOWEST, false, enableCpuAffinity);
    };
};

}  // namespace myNet

#endif  // WorkThredPool_hpp