#ifndef TaskQueue_hpp
#define TaskQueue_hpp

#include <list>
#include <mutex>

#include "Semaphore.hpp"

namespace myNet {

template <typename TaskType>
class TaskQueue {
public:
    using LOCK_GUDAD = std::lock_guard<std::mutex>;

    template <typename TaskFunc>
    void pushTask(TaskFunc&& taskFunc) {
        {
            LOCK_GUDAD lck(_mtx);
            _queue.emplace_back(std::forward<TaskFunc>(taskFunc));
        }
        _sem.notify();
    }

    template <typename TaskFunc>
    void pushTaskFirst(TaskFunc& taskFunc) {
        {
            LOCK_GUDAD lck(_mtx);
            _queue.emplace_front(std::forward<TaskFunc>(taskFunc));
        }
        _sem.notify();
    }

    // 清空任务队列
    void pushExit(size_t n) { _sem.notify(n); }

    // 这个地方wait 位置可能有问题，原代码写在开头
    bool getTask(TaskType& task) {
        LOCK_GUDAD lck(_mtx);
        if (_queue.empty()) {
            return false;
        }
        _sem.wait();
        task = _queue.front();
        _queue.pop_front();
        return true;
    }

    size_t size() const {
        LOCK_GUDAD lck(_mtx);
        return _queue.size();
    }

private:
    std::list<TaskType> _queue;
    mutable std::mutex _mtx;
    Semaphore _sem;
};

}  // namespace myNet

#endif  // TaskQueue_hpp