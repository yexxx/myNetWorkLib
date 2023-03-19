#include <unistd.h>

#include <csignal>
#include <functional>
#include <iostream>
#include <memory>

#include "../myThread/Semaphore.hpp"

using namespace std;
using namespace myNet;

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

class testClass : public enable_shared_from_this<testClass> {
public:
    testClass(){};

    int get() { return _num; }
    void set(int num) { _num = num; }

private:
    int _num{10};
};

Semaphore sem;
int main() {
    {
        shared_ptr<void> sp(nullptr, [&](void *) {
            cout << "~~~~~~~~~~~~~~~~~\n";
            sem.post();
            return nullptr;
        });
    }

    // signal(SIGINT, [](int) {
    //     sem.post();
    // });

    sem.wait();
    printf("dasd\n");

    return 0;
}