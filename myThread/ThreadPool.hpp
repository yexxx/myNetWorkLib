#ifndef ThreadPool_hpp
#define ThreadPool_hpp

#include <assert.h>

#include "TaskExecutor.hpp"
#include "TaskQueue.hpp"
#include "ThreadGroup.hpp"
#include "Util/logger.h"

namespace myNet {

class ThreadPool : public TaskExecutor {
public:
    enum Priority { PRIORITY_LOWEST = 0, PRIORITY_LOW, PRIORITY_NORMAL, PRIORITY_HIGH, PRIORITY_HIGHEST };

    ThreadPool(int num = 1, Priority priority = PRIORITY_HIGH, bool autoStart = true) {
        _threadNum = num;
        _priority = priority;
        _logger = toolkit::Logger::Instance().shared_from_this();
        if (autoStart) {
            start();
        }
    };

    ~ThreadPool() {
        _taskQueue.pushExit(_threadNum);
        _threadGroup.joinAll();
    };

    Task::Ptr async(TaskIn task, bool maySync = true) override {
        if (maySync && _threadGroup.isThisThreadIn()) {
            task();
            return nullptr;
        }
        auto ret = std::make_shared<Task>(std::move(task));
        _taskQueue.pushTask(ret);
        return ret;
    };

    Task::Ptr async_first(TaskIn task, bool maySync = true) override {
        if (maySync && _threadGroup.isThisThreadIn()) {
            task();
            return nullptr;
        }
        auto ret = std::make_shared<Task>(std::move(task));
        _taskQueue.pushTaskFirst(ret);
        return ret;
    };

    size_t getTaskSize() { return _taskQueue.size(); };

    static bool setPriority(Priority priority = PRIORITY_NORMAL, std::thread::native_handle_type threadId = 0) {
        static int Min = sched_get_priority_min(SCHED_OTHER);
        if (Min == -1) {
            return false;
        }
        static int Max = sched_get_priority_max(SCHED_OTHER);
        if (Max == -1) {
            return false;
        }
        static int Priorities[] = {Min, Min + (Max - Min) / 4, Min + (Max - Min) / 2, Min + (Max - Min) * 3 / 4, Max};

        if (threadId == 0) {
            threadId = pthread_self();
        }
        sched_param params;
        params.sched_priority = Priorities[priority];
        return pthread_setschedparam(threadId, SCHED_OTHER, &params) == 0;
    };

    void start() {
        assert(_threadNum > 0);

        for (auto i = 0; i < _threadNum; ++i) {
            _threadGroup.createThread(std::bind(&ThreadPool::run, this));
        }
    };

private:
    void run() {
        ThreadPool::setPriority(_priority);

        Task::Ptr task;
        while (true) {
            startSleep();
            // 由于_taskQueue的sem 在wait，所以若非pushExit 的话不会在这停止
            if (!_taskQueue.getTask(task)) {
                break;
            }

            sleepWakeUp();
            try {
                (*task)();
                task = nullptr;
            } catch (std::exception &ex) {
                ErrorL << "ThreadPool catch a exception: " << ex.what();
            }
        }
    };

    size_t _threadNum;
    TaskQueue<Task::Ptr> _taskQueue;
    ThreadGroup _threadGroup;
    Priority _priority;
    toolkit::Logger::Ptr _logger;
};

}  // namespace myNet

#endif  // ThreadPool_hpp