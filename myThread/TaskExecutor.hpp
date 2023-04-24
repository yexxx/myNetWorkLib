#ifndef TaskExecutor_hpp
#define TaskExecutor_hpp

// #include <functional>
#include <list>
#include <memory>
#include <mutex>

#include "Util/util.h"

namespace myNet {

// 时间管理暂时未实现，先使用ZLToolkit 实现的相关内容
// ticker
class ThreadLoadCounter {
public:
    // windowSize: 统计的时间窗口大小
    ThreadLoadCounter(uint64_t maxSize, uint64_t windowSize);
    ~ThreadLoadCounter() = default;

    // 线程进入休眠
    void startSleep();

    // 休眠结束
    void sleepWakeUp();

    // 获取当前线程负载(0-100)
    int getLoad();

private:
    using LOCK_GUDAR = std::lock_guard<std::mutex>;
    std::mutex _mtx;

    bool _sleeping{true};
    uint64_t _lastSleepTime;
    uint64_t _lastWakeTime;
    uint64_t _maxSize;
    uint64_t _windowSize;

    // <时间， 是否休眠>
    std::list<std::pair<uint64_t, bool>> _timeRecordList;
};

template <typename R, typename... ArgTypes>
class TaskCancelable;

// 接收任意类型的function
template <typename R, typename... ArgTypes>
class TaskCancelable<R(ArgTypes...)> {
public:
    using Ptr = std::shared_ptr<TaskCancelable>;
    using taskFunc = std::function<R(ArgTypes...)>;

    template <typename func>
    TaskCancelable(func&& task) {
        _sharedTask = std::make_shared<func>(std::forward<func>(task));
        _weakTask = _sharedTask;
    }

    ~TaskCancelable() = default;

    void cancel() { _sharedTask = nullptr; }

    operator bool() { return _sharedTask && *_sharedTask; }

    R operator()(ArgTypes... args) const {
        auto sharedTask = _weakTask.lock();
        if (sharedTask && *sharedTask) {
            return (*sharedTask)(std::forward<ArgTypes>(args)...);
        }

        return defaultValue<R>();
    }

    template <typename T>
    static typename std::enable_if<std::is_void<T>::value, void>::type defaultValue() {}

    template <typename T>
    static typename std::enable_if<std::is_pointer<T>::value, T>::type defaultValue() {
        return nullptr;
    }

    template <typename T>
    static typename std::enable_if<std::is_integral<T>::value, T>::type defaultValue() {
        return 0;
    }

private:
    // 禁止复制
    TaskCancelable(const TaskCancelable&) = delete;
    TaskCancelable(const TaskCancelable&&) = delete;
    TaskCancelable& operator=(const TaskCancelable&) = delete;
    TaskCancelable& operator=(const TaskCancelable&&) = delete;

    std::weak_ptr<taskFunc> _weakTask;
    std::shared_ptr<taskFunc> _sharedTask;
};

// 全局输入任务
using TaskIn = std::function<void()>;
// 全局任务管理
using Task = TaskCancelable<void()>;

class TaskExecutorInterface {
public:
    TaskExecutorInterface() = default;
    ~TaskExecutorInterface() = default;

    // maySync: 是否允许同步执行该任务
    virtual Task::Ptr async(TaskIn task, bool maySync = true) = 0;

    // 最高优先级异步执行，默认允许同步的方式异步执行
    virtual Task::Ptr async_first(TaskIn task, bool maySync = true) { return async(std::move(task), maySync); };

    // 同步，任务不完成不退出
    void sync(const TaskIn task);

    // 最高优先级同步执行
    void sync_first(const TaskIn task);
};

class TaskExecutor : public ThreadLoadCounter, public TaskExecutorInterface {
public:
    using Ptr = std::shared_ptr<TaskExecutor>;

    TaskExecutor(uint64_t maxSize = 32, uint64_t windowSize = 2 * 1000 * 1000) : ThreadLoadCounter(maxSize, windowSize) {}

    ~TaskExecutor() = default;
};

class TaskExecutorGetter {
public:
    using Ptr = std::shared_ptr<TaskExecutorGetter>;

    TaskExecutorGetter() = default;
    ~TaskExecutorGetter() = default;

    // 获取最闲的线程
    TaskExecutor::Ptr getExecutor();

    // 获取线程负载
    std::vector<int> getExecutorLoad() {
        std::vector<int> vec(_threads.size());
        int i = 0;
        for (auto& executor : _threads) {
            vec[i++] = executor->getLoad();
        }
        return vec;
    }

    // 获取所有线程任务执行延时，通过此函数也可以大概知道线程负载情况
    void getExecutorDelay(const std::function<void(const std::vector<int>&)>& callback);

    size_t getExecutorSize() const { return _threads.size(); };

    void for_each(const std::function<void(const TaskExecutor::Ptr&)>& cb) {
        for (auto& athread : _threads) {
            cb(athread);
        }
    };

protected:
    // registerThread: 是否记录该线程到thread_local 实例
    // enableCpuAffinity: CPU 亲和性，将线程绑定到CPU
    size_t addPoller(const std::string& name, size_t size, int priority, bool registerThread, bool enableCpuAffinity = true);

    std::vector<TaskExecutor::Ptr> _threads;
};

}  // namespace myNet

#endif  // TaskExecutor_hpp
