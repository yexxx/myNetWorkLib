#ifndef EventPoller_hpp
#define EventPoller_hpp

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "../myNetwork/Buffer.hpp"
#include "../myThread/TaskExecutor.hpp"
#include "../myThread/ThreadPool.hpp"
#include "Pipe.hpp"

namespace myNet {

class EventPoller : public TaskExecutor, public std::enable_shared_from_this<EventPoller> {
  public:
    friend class TaskExecutorGetter;

    using Ptr = std::shared_ptr<EventPoller>;

    using PollEventCB = std::function<void(int event)>;
    using PollDelCB = std::function<void(bool success)>;
    using DelayTask = TaskCancelable<uint64_t(void)>;

    enum PollEvent {
        Event_Read = 1 << 0,  // 读事件
        Event_Write = 1 << 1, // 写事件
        Event_Error = 1 << 2, // 错误事件
        Event_LT = 1 << 3,    // 水平触发
    };

    ~EventPoller();

    static EventPoller& Instance();

    int addEvent(int fd, int event, PollEventCB cb);

    int delEvent(int fd, PollDelCB cb = nullptr);

    int modifyEvent(int fd, int event);

    Task::Ptr async(TaskIn task, bool maySync = true) override;

    Task::Ptr async_first(TaskIn task, bool maySync = true) override;

    // 让后续任务延时delayMs 执行
    EventPoller::DelayTask::Ptr doDelayTask(uint64_t delayMs, std::function<uint64_t()> task);

    bool isCurrentThread();

    static EventPoller::Ptr getCurrentPoller();

    BufferRaw::Ptr getSharedBuffer();

    const std::thread::id& getThreadId() const;

    const std::string& getThreadName() const;

  private:
    using LOCK_GUARD = std::lock_guard<std::mutex>;

    EventPoller(std::string name, ThreadPool::Priority priority = ThreadPool::PRIORITY_HIGHEST);

    // 是否用执行该接口的线程执行轮询， 是记录本对象到thread local 变量
    // blocked 是否利用本线程执行轮询
    void runLoop(bool blocked, bool refSelf);

    // 内部管道事件，用于唤醒轮询线程
    void onPipeEvent();

    // 结束轮询
    void shutdown();
    // 结束信号
    class ExitException : public std::exception {};

    // 刷新延时任务：更新或删除延时任务
    uint64_t flushDelayTask(uint64_t now);

    // 获取将要设置的epoll 休眠时间，主要是为了延时任务
    uint64_t getMinDelay();

    // loop 线程是否退出
    bool _exitFlag;
    // 当前线程所有Socket 共享的读缓存
    std::weak_ptr<BufferRaw> _sharedBuffer;
    ThreadPool::Priority _priority;

    // 运行循环事件的锁
    std::mutex _mtxRunning;
    std::string _loopThreadName;
    std::thread* _loopThread{nullptr};
    std::thread::id _loopThreadId;
    Semaphore _semLoop;

    // 内部事件管道
    Pipe _pipe;

    // 其它线程切换过来的任务
    std::mutex _mtxTask;
    std::list<Task::Ptr> _listTask;

    toolkit::Logger::Ptr _logger;

    int _epollFd{-1};
    std::unordered_map<int, std::shared_ptr<PollEventCB>> _eventMap;

    // 定时器
    std::multimap<uint64_t, DelayTask::Ptr> _delayTaskMap;
};

class EventPollerPool : public std::enable_shared_from_this<EventPollerPool>, public TaskExecutorGetter {
  public:
    using Ptr = std::shared_ptr<EventPollerPool>;
    ~EventPollerPool() = default;

    static EventPollerPool& Instance();

    EventPoller::Ptr getFirstPoller();

    EventPoller::Ptr getPoller(bool preferCurrentThread = true);

    void setPreferCurrentThread(bool flag = true) { _preferCurrentThread = flag; };

    static void setPoolSize(size_t size = 0);

    static void setEnableCpuAffinity(bool enable);

  private:
    EventPollerPool();
    bool _preferCurrentThread{true};
};

} // namespace myNet

#endif // EventPoller_hpp