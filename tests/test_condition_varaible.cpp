// #include <condition_variable>  // std::condition_variable
#include <iostream>  // std::cout
#include <mutex>     // std::mutex, std::unique_lock
#include <thread>    // std::thread

#include "../myThread/Semaphore.hpp"

using namespace myNet;

Semaphore cv;        // 全局条件变量.
bool ready = false;  // 全局标志位.

void do_print_id(int id) {
    while (!ready) {  // 如果标志位不为 true, 则等待...
        cv.wait();    // 当前线程被阻塞, 当全局标志位变为 true 之后,
    }
    // 线程被唤醒, 继续往下执行打印线程编号id.
    printf("thread %d\n", id);
}

void go() {
    ready = true;  // 设置全局标志位为 true.
    // cv.notifyOne();
    cv.notify(10);  // 唤醒所有线程.
}

int main() {
    std::thread threads[10];
    // spawn 10 threads:
    for (int i = 0; i < 10; ++i) {
        threads[i] = std::thread(do_print_id, i);
    }

    std::cout << "10 threads ready to race...\n";

    go();

    for (auto& th : threads) {
        th.detach();
    }

    getchar();

    return 0;
}