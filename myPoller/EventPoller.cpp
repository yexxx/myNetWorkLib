#include "EventPoller.hpp"

#include <sys/epoll.h>

#include "../myNetwork/SocketUtil.hpp"
#include "../myNetwork/uv_errno.hpp"
#include "Util/TimeTicker.h"

#define EPOLL_SIZE 1024

// 转换poller 和epoll 所使用的事件标识符
#define toEpoll(event) (((event) & Event_Read) ? EPOLLIN : 0) | (((event) & Event_Write) ? EPOLLOUT : 0) | (((event) & Event_Error) ? (EPOLLHUP | EPOLLERR) : 0) | (((event) & Event_LT) ? 0 : EPOLLET)
#define toPoller(epoll_event)                                                                                                                                                                          \
    (((epoll_event) & EPOLLIN) ? Event_Read : 0) | (((epoll_event) & EPOLLOUT) ? Event_Write : 0) | (((epoll_event) & EPOLLHUP) ? Event_Error : 0) | (((epoll_event) & EPOLLERR) ? Event_Error : 0)

namespace myNet {

static thread_local std::weak_ptr<EventPoller> currentPoller;

static size_t poolSize = 0;
static bool enableCpuAffinity = true;

EventPoller::~EventPoller() {
    shutdown();
    LOCK_GUARD lck(_mtxRunning);

    if (-1 != _epollFd) {
        close(_epollFd);
        _epollFd = -1;
    }

    // 清理管道中的数据
    _loopThreadId = std::this_thread::get_id();
    onPipeEvent();
    InfoL << "ExitPoller: " << _loopThreadName;
}

int EventPoller::addEvent(int fd, int event, PollEventCB cb) {
    // 5s 定时
    toolkit::TimeTicker();
    if (!cb) {
        WarnL << "PollEventCB is empty.";
        return -1;
    }

    if (isCurrentThread()) {
        epoll_event epollEvent{0};
        // EPOLLEXCLUSIVE: 有这个标志位的fd，每次仅会唤醒队列头的一个，避免了惊群效应
        epollEvent.events = toEpoll(event) | EPOLLEXCLUSIVE;
        epollEvent.data.fd = fd;
        int ret = epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &epollEvent);
        if (0 == ret) {
            _eventMap.emplace(fd, std::make_shared<PollEventCB>(std::move(cb)));
        }
        return ret;
    }

    // 不是本线程的时间，异步到归属的线程处理
    async([this, fd, event, cb]() { addEvent(fd, event, std::move(const_cast<PollEventCB&>(cb))); });

    return 0;
}

int EventPoller::delEvent(int fd, PollDelCB cb) {
    toolkit::TimeTicker();
    if (!cb) {
        cb = [](bool success) {};
    }

    if (isCurrentThread()) {
        bool success = (0 == epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, nullptr) && _eventMap.erase(fd) > 0);
        cb(success);
        return success ? 0 : -1;
    }

    async([this, fd, cb]() { delEvent(fd, std::move(const_cast<PollDelCB&>(cb))); });

    return 0;
}

int EventPoller::modifyEvent(int fd, int event) {
    toolkit::TimeTicker();
    epoll_event epollEvent{0};
    epollEvent.events = toEpoll(event);
    epollEvent.data.fd = fd;
    return epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &epollEvent);
}

Task::Ptr EventPoller::async(TaskIn task, bool maySync) {
    toolkit::TimeTicker();
    if (maySync && isCurrentThread()) {
        task();
        return nullptr;
    }

    auto ret = std::make_shared<Task>(std::move(task));
    {
        LOCK_GUARD lck(_mtxTask);
        _listTask.emplace_back(ret);
    }

    // 写管道，是epoll_wait 可以读取到管道写线程，之后处理_listTask 中的任务
    _pipe.write("", 1);
    return ret;
}

Task::Ptr EventPoller::async_first(TaskIn task, bool maySync) {
    toolkit::TimeTicker();
    if (maySync && isCurrentThread()) {
        task();
        return nullptr;
    }

    auto ret = std::make_shared<Task>(std::move(task));
    {
        LOCK_GUARD lck(_mtxTask);
        _listTask.emplace_front(ret);
    }

    _pipe.write("", 1);
    return ret;
}

EventPoller::DelayTask::Ptr EventPoller::doDelayTask(uint64_t delayMs, std::function<uint64_t()> task) {
    DelayTask::Ptr ret = std::make_shared<DelayTask>(std::move(task));
    auto time = toolkit::getCurrentMillisecond() + delayMs;
    async_first([time, ret, this]() { _delayTaskMap.emplace(time, ret); });
    return ret;
}

bool EventPoller::isCurrentThread() { return _loopThreadId == std::this_thread::get_id(); }

EventPoller::Ptr EventPoller::getCurrentPoller() { return currentPoller.lock(); }

BufferRaw::Ptr EventPoller::getSharedBuffer() {
    auto ret = _sharedBuffer.lock();
    if (!ret) {
        ret = BufferRaw::create();
        ret->setCapacity(1 + SOCKET_DEFAULT_BUF_SIZE);
        _sharedBuffer = ret;
    }
    return ret;
}

const std::thread::id& EventPoller::getThreadId() const { return _loopThreadId; }

const std::string& EventPoller::getThreadName() const { return _loopThreadName; }

EventPoller::EventPoller(std::string name, ThreadPool::Priority priority) {
    _loopThreadName = name;
    _priority = priority;
    SocketUtil::setNoBlocked(_pipe.readFd());
    SocketUtil::setNoBlocked(_pipe.writeFd());

    _epollFd = epoll_create(EPOLL_SIZE);
    if (-1 == _epollFd) {
        throw std::runtime_error("Create epoll fd failed: " + std::string(uv_strerror(uv_translate_posix_error(errno))));
    }
    SocketUtil::setCloExec(_epollFd);

    _logger = toolkit::Logger::Instance().shared_from_this();
    _loopThreadId = std::this_thread::get_id();

    // 添加内部管道事件
    if (-1 == addEvent(_pipe.readFd(), Event_Read, [this](int event) { onPipeEvent(); })) {
        throw std::runtime_error("Add pipe fd to poller failed.");
    }
}

void EventPoller::runLoop(bool blocked, bool refSelf) {
    if (blocked) {
        ThreadPool::setPriority(_priority);
        LOCK_GUARD lck(_mtxRunning);
        _loopThreadId = std::this_thread::get_id();
        if (refSelf) {
            currentPoller = shared_from_this();
        }
        _semLoop.post();
        _exitFlag = false;

        uint64_t minDelay;
        epoll_event events[EPOLL_SIZE];
        while (!_exitFlag) {
            minDelay = getMinDelay();
            startSleep();
            int ret = epoll_wait(_epollFd, events, EPOLL_SIZE, minDelay > 0 ? minDelay : -1);
            sleepWakeUp();
            if (ret <= 0) {
                continue;
            }

            for (int i = 0; i < ret; ++i) {
                auto& ev = events[i];
                auto fd = ev.data.fd;
                auto it = _eventMap.find(fd);
                if (_eventMap.end() == it) {
                    epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, nullptr);
                    continue;
                }
                auto cb = it->second;
                try {
                    (*cb)(toPoller(ev.events));
                } catch (std::exception& e) {
                    ErrorL << "Exception occurred when do event task: " << e.what();
                }
            }
        }
    } else {
        _loopThread = new std::thread(&EventPoller::runLoop, this, true, refSelf);
        _semLoop.wait();
    }
}

void EventPoller::onPipeEvent() {
    char buf[1024];
    // ET 方式处理数据
    do {
        _pipe.read(buf, sizeof(buf));
    } while (uv_translate_posix_error(errno) != UV_EAGAIN);

    decltype(_listTask) listTaskSwap;
    {
        LOCK_GUARD lck(_mtxTask);
        listTaskSwap.swap(_listTask);
    }

    for (auto& task : listTaskSwap) {
        try {
            (*task)();
        } catch (ExitException&) {
            _exitFlag = true;
        } catch (std::exception& e) {
            ErrorL << "Exception occurred when do async task: " << e.what();
        }
    }
}

void EventPoller::shutdown() {
    async_first([]() { throw ExitException(); }, false);

    if (_loopThread) {
        try {
            _loopThread->join();
        } catch (...) {
        }
        delete _loopThread;
        _loopThread = nullptr;
    }
}

uint64_t EventPoller::flushDelayTask(uint64_t now) {
    decltype(_delayTaskMap) delayTaskMapCopy;
    delayTaskMapCopy.swap(_delayTaskMap);

    // it->first 和 now 很大概率相同， 这个地方必须要等号
    // 若无等号，只要休眠事件与预计相等时就会不正确退出循环导致_delayTaskMap 不能正确添加重复定时任务
    for (auto it = delayTaskMapCopy.begin(); it != delayTaskMapCopy.end() && it->first <= now; it = delayTaskMapCopy.erase(it)) {
        try {
            auto nxtDelayMs = (*it->second)();
            if (nxtDelayMs) {
                _delayTaskMap.emplace(now + nxtDelayMs, std::move(it->second));
            }
        } catch (std::exception& e) {
            ErrorL << "Exception occurred when do delay task: " << e.what();
        }
    }

    delayTaskMapCopy.insert(_delayTaskMap.begin(), _delayTaskMap.end());
    _delayTaskMap.swap(delayTaskMapCopy);

    if (_delayTaskMap.empty()) {
        return 0;
    }
    return _delayTaskMap.begin()->first - now;
}

uint64_t EventPoller::getMinDelay() {
    if (_delayTaskMap.empty()) {
        return 0;
    }
    auto now = toolkit::getCurrentMillisecond();
    if (_delayTaskMap.begin()->first > now) {
        return _delayTaskMap.begin()->first - now;
    }
    return flushDelayTask(now);
}

EventPollerPool& EventPollerPool::Instance() {
    // static auto sharedRet = std::make_shared<EventPollerPool>();
    // 必须这么定义，上面的报错
    static std::shared_ptr<EventPollerPool> sharedRet(new EventPollerPool());
    static auto& ret = *sharedRet;
    return ret;
}

EventPoller::Ptr EventPollerPool::getFirstPoller() { return std::dynamic_pointer_cast<EventPoller>(_threads.front()); }

EventPoller::Ptr EventPollerPool::getPoller(bool preferCurrentThread) {
    auto poller = EventPoller::getCurrentPoller();
    if (preferCurrentThread && _preferCurrentThread && poller) {
        return poller;
    }
    return std::dynamic_pointer_cast<EventPoller>(getExecutor());
}

void EventPollerPool::setPoolSize(size_t size) { poolSize = size; }

void EventPollerPool::setEnableCpuAffinity(bool enable) { enableCpuAffinity = enable; }

EventPollerPool::EventPollerPool() {
    auto size = addPoller("event poller", poolSize, ThreadPool::PRIORITY_HIGHEST, true, enableCpuAffinity);
    InfoL << "EventPoller created size: " << size;
}

} // namespace myNet