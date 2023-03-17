#ifndef ThreadGroup_hpp
#define ThreadGroup_hpp

#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace myNet {

class ThreadGroup {
public:
    ThreadGroup() = default;
    ~ThreadGroup() { _threadMap.clear(); }

    template <typename Func>
    std::thread *createThread(Func &&threadFunc) {
        auto threadNew = std::make_shared<std::thread>(threadFunc);
        _threadMap[threadNew->get_id()] = threadNew;
        return threadNew.get();
    }

    bool isThisThreadIn() {
        auto thisThreadId = std::this_thread::get_id();
        return (thisThreadId == _threadId || _threadMap.count(thisThreadId));
    }

    bool isThreadIn(std::thread *thrd) {
        if (!thrd) {
            return false;
        }

        return _threadMap.count(thrd->get_id());
    }

    void removeThread(std::thread *thrd) {
        if (_threadMap.count(thrd->get_id())) {
            _threadMap.erase(thrd->get_id());
        }
    }

    void joinAll() {
        if (isThisThreadIn()) {
            throw std::runtime_error("Trying joining itself in thread_group.");
        }
        for (auto &[id, thrd] : _threadMap) {
            if (thrd->joinable()) {
                thrd->join();
            }
        }
        _threadMap.clear();
    }

    size_t size() { return _threadMap.size(); }

private:
    std::thread::id _threadId;
    std::unordered_map<std::thread::id, std::shared_ptr<std::thread>> _threadMap;

    // 禁止复制
    ThreadGroup(const ThreadGroup &that) = delete;
    ThreadGroup(ThreadGroup &&that) = delete;
    ThreadGroup &operator=(const ThreadGroup &that) = delete;
    ThreadGroup &operator=(ThreadGroup &&that) = delete;
};

}  //namespace myNet

#endif  // ThreadGroup_hpp