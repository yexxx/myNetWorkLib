#ifndef Semaphore_hpp
#define Semaphore_hpp

#include <condition_variable>
#include <mutex>

// 对条件变量的封装

namespace myNet {

class Semaphore {
public:
    using LOCK_GUARD = std::lock_guard<std::recursive_mutex>;

    explicit Semaphore() = default;
    ~Semaphore() = default;

    void notify(size_t n = 1) {
        LOCK_GUARD lck(_mtx);
        _count += n;
        // notify 不是条件变量控制的，是锁和_count 控制的，所以这个地方直接notify_all
        n == 1 ? _condition.notify_one() : _condition.notify_all();
    }

    void wait() {
        LOCK_GUARD lck(_mtx);
        while (_count == 0) {
            _condition.wait(_mtx);
        }
        --_count;
    }

private:
    size_t _count{0};
    std::recursive_mutex _mtx;
    std::condition_variable_any _condition;
};

}  // namespace myNet

#endif