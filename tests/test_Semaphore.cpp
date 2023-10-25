#include <atomic>
#include <csignal>

#include "../myThread/Semaphore.hpp"
#include "../myThread/ThreadGroup.hpp"
#include "Util/TimeTicker.h"
#include "Util/logger.h"

using namespace std;
using namespace myNet;

#define MAX_TASK_SIZE (10000000)
Semaphore g_sem; // 信号量
atomic_llong g_produced(0);
atomic_llong g_consumed(0);
bool finish{false};

// 消费者线程
void onConsum() {
    while (!finish) {
        g_sem.wait();
        if (++g_consumed > g_produced && !finish) {
            // 如果打印这句log则表明有bug
            ErrorL << g_consumed << " > " << g_produced;
        }
    }
}

// 生产者线程
void onProduce() {
    while (true) {
        ++g_produced;
        g_sem.post();
        if (g_produced >= MAX_TASK_SIZE) {
            break;
        }
    }
}

int main() {
    // 初始化log
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());

    toolkit::Ticker ticker;
    ThreadGroup thread_producer;
    for (size_t i = 0; i < thread::hardware_concurrency(); ++i) {
        thread_producer.createThread([]() {
            // cpu逻辑核心个生产者线程
            onProduce();
        });
    }

    ThreadGroup thread_consumer;
    for (int i = 0; i < 4; ++i) {
        thread_consumer.createThread([i]() {
            // 4个消费者线程
            onConsum();
        });
    }

    // 等待所有生成者线程退出
    thread_producer.joinAll();
    DebugL << "生产者线程退出，耗时:" << ticker.elapsedTime() << "ms,"
           << "生产任务数:" << g_produced << ",消费任务数:" << g_consumed;

    int i = 5;
    while (i--) {
        DebugL << ",消费任务数:" << g_consumed;
        sleep(1);
    }

    finish = true;
    g_sem.post(4);

    thread_consumer.joinAll();

    return 0;
}
