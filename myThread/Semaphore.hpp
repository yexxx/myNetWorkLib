#ifndef Semaphore_hpp
#define Semaphore_hpp

#include <semaphore.h>

// #include <condition_variable>
// #include <mutex>

// 对条件变量的封装

// 注释掉的部分是条件变量和互斥锁实现的条件变量，效率较慢

namespace myNet {

class Semaphore {
public:
    explicit Semaphore() {
        sem_init(&_sem, 0, 0);
    };

    ~Semaphore() {
        sem_destroy(&_sem);
    };

    void post(size_t n = 1) {
        while (n--) {
            sem_post(&_sem);
        }

        // LOCK_GUARD lck(_mtx);
        // _count += n;
        // // notify 不是条件变量控制的，是锁和_count 控制的，所以这个地方直接notify_all
        // n == 1 ? _condition.notify_one() : _condition.notify_all();
    }

    void wait() {
        sem_wait(&_sem);

        // LOCK_GUARD lck(_mtx);
        // while (_count == 0) {
        //     // 这个地方会对锁解锁, 必须使用unique_lock
        //     _condition.wait(lck);
        // }
        // --_count;
    }

private:
    sem_t _sem;

    // using LOCK_GUARD = std::unique_lock<std::recursive_mutex>;
    // size_t _count{0};
    // std::recursive_mutex _mtx;
    // std::condition_variable_any _condition;
};

}  // namespace myNet

#endif