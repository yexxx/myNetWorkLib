#include "TaskExecutor.hpp"

#include <thread>

#include "../myPoller/EventPoller.hpp"
#include "Semaphore.hpp"
#include "Util/TimeTicker.h"
#include "Util/util.h"

namespace myNet {

ThreadLoadCounter::ThreadLoadCounter(uint64_t maxSize, uint64_t windowSize) {
    _lastSleepTime = _lastWakeTime = toolkit::getCurrentMicrosecond();
    _maxSize = maxSize;
    _windowSize = windowSize;
}

void ThreadLoadCounter::startSleep() {
    LOCK_GUDAR lck(_mtx);
    _sleeping = true;
    _lastSleepTime = toolkit::getCurrentMicrosecond();
    _timeRecordList.emplace_back(_lastSleepTime - _lastWakeTime, false);
    if (_timeRecordList.size() > _maxSize) {
        _timeRecordList.pop_front();
    }
}

void ThreadLoadCounter::sleepWakeUp() {
    LOCK_GUDAR lck(_mtx);
    _sleeping = false;
    _lastWakeTime = toolkit::getCurrentMicrosecond();
    _timeRecordList.emplace_back(_lastWakeTime - _lastSleepTime, true);
    if (_timeRecordList.size() > _maxSize) {
        _timeRecordList.pop_front();
    }
}

int ThreadLoadCounter::getLoad() {
    LOCK_GUDAR lck(_mtx);
    uint64_t totalSleepTime = 0;
    uint64_t totalRunTime = 0;

    for (auto& [tme, isSleeping] : _timeRecordList) {
        if (isSleeping) {
            totalSleepTime += tme;
        } else {
            totalRunTime += tme;
        }
    }

    if (_sleeping) {
        totalSleepTime += toolkit::getCurrentMicrosecond() - _lastSleepTime;
    } else {
        totalRunTime += toolkit::getCurrentMicrosecond() - _lastSleepTime;
    }

    // 判断是否大于时间窗
    uint64_t totalTime = totalRunTime + totalSleepTime;
    while (!_timeRecordList.empty() && totalTime > _windowSize) {
        auto [tme, isSleeping] = _timeRecordList.front();
        _timeRecordList.pop_front();
        if (isSleeping) {
            totalSleepTime -= tme;
        } else {
            totalRunTime -= tme;
        }
        totalTime -= tme;
    }

    if (totalTime != 0) {
        return totalRunTime * 100 / totalTime;
    }
    return 0;
}

void TaskExecutorInterface::sync(const TaskIn task) {
    Semaphore sem;
    auto ret = async([&]() {
        std::shared_ptr<void> deleter(nullptr, [&](void*) { sem.post(); });
        task();
    });

    if (ret && *ret) {
        sem.wait();
    }
}

void TaskExecutorInterface::sync_first(const TaskIn task) {
    Semaphore sem;
    auto ret = async_first([&]() {
        std::shared_ptr<void> deleter(nullptr, [&](void*) { sem.post(); });
        task();
    });

    if (ret && *ret) {
        sem.wait();
    }
}

TaskExecutor::Ptr TaskExecutorGetter::getExecutor() {
    size_t retPos = 0;
    int minLoad = 100;
    for (auto i = 0; i < _threads.size(); ++i) {
        auto tmpLoad = _threads[i]->getLoad();
        if (minLoad > tmpLoad) {
            minLoad = tmpLoad;
            retPos = i;
        }
    }
    return _threads[retPos];
}

void TaskExecutorGetter::getExecutorDelay(const std::function<void(const std::vector<int>&)>& callback) {
    auto delayVec = std::make_shared<std::vector<int>>(_threads.size());
    // deleter 拥有delayVec，所以deleter 一定先析构
    std::shared_ptr<void> deleter(nullptr, [delayVec, callback](void*) { callback(*delayVec); });
    for (auto i = 0; i < _threads.size(); ++i) {
        std::shared_ptr<toolkit::Ticker> ticker = std::make_shared<toolkit::Ticker>();
        // 这里捕获deleter 防止其提前析构
        _threads[i]->async([deleter, delayVec, i, ticker]() { (*delayVec)[i] = static_cast<int>(ticker->elapsedTime()); }, false);
    }
}

size_t TaskExecutorGetter::addPoller(const std::string& name, size_t size, int priority, bool registerThread, bool enableCpuAffinity) {
    size = size != 0 ? size : std::thread::hardware_concurrency();

    for (auto i = 0; i < size; ++i) {
        auto fullName = name + " " + std::to_string(i);
        // 上面的构造是错的，原因是make_shared 操作不能访问隐私方法
        // auto poller = std::make_shared<EventPoller>(fullName, (ThreadPool::Priority)priority);
        std::shared_ptr<EventPoller> poller(new EventPoller(fullName, (ThreadPool::Priority)priority));
        poller->runLoop(false, registerThread);
        poller->async([fullName, enableCpuAffinity, i]() {
            pthread_setname_np(pthread_self(), fullName.data());
            if (enableCpuAffinity) {
                toolkit::setThreadAffinity(i % std::thread::hardware_concurrency());
            }
        });

        _threads.emplace_back(std::move(poller));
    }

    return size;
}

} // namespace myNet