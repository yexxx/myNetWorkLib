#ifndef myUtil_hpp
#define myUtil_hpp

#include <mutex>

namespace myNet {

// 禁止拷贝基类
class noncopyable {
protected:
    noncopyable() {}
    ~noncopyable() {}

private:
    noncopyable(const noncopyable &that) = delete;
    noncopyable(noncopyable &&that) = delete;
    noncopyable &operator=(const noncopyable &that) = delete;
    noncopyable &operator=(noncopyable &&that) = delete;
};

// 锁
class MutexWrapper {
public:
    MutexWrapper(bool enable) { _enable = enable; }
    ~MutexWrapper() = default;

    void lock() {
        if (_enable) _Mtx.lock();
    }
    void unlock() {
        if (_enable) _Mtx.unlock();
    }

private:
    bool _enable;
    std::recursive_mutex _Mtx;
};

}  // namespace myNet

#endif