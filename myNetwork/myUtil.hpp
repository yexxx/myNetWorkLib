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

class onceToken : public noncopyable {
public:
    onceToken(std::function<void(void)> onConstructed, std::function<void(void)> onDestructed = nullptr) {
        onConstructed();
        _onDestructed = std::move(onDestructed);
    }

    // 只需要析构时
    onceToken(std::nullptr_t, std::function<void(void)> onDestructed = nullptr) {
        _onDestructed = std::move(onDestructed);
    }

    ~onceToken() {
        if (_onDestructed) {
            _onDestructed();
        }
    };

private:
    onceToken() = delete;
    std::function<void(void)> _onDestructed;
};

}  // namespace myNet

#endif