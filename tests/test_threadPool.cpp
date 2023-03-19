#include <chrono>

#include "../myThread/ThreadPool.hpp"
#include "Util/TimeTicker.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
using namespace myNet;

int main() {
    //初始化日志系统
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());
    toolkit::Logger::Instance().setWriter(std::make_shared<toolkit::AsyncLogWriter>());

    ThreadPool pool(thread::hardware_concurrency(), ThreadPool::PRIORITY_HIGHEST, true);

    //每个任务耗时3秒
    auto task_second = 3;
    //每个线程平均执行4次任务，总耗时应该为12秒
    auto task_count = thread::hardware_concurrency() * 4;

    Semaphore sem;
    vector<int> vec;
    vec.resize(task_count);
    toolkit::Ticker ticker;
    {
        shared_ptr<void> token(nullptr, [&](void*) {
            sem.post();
        });

        for (auto i = 0; i < task_count; ++i) {
            pool.async([token, i, task_second, &vec]() {
                toolkit::setThreadName(("thread pool " + to_string(i)).data());
                std::this_thread::sleep_for(std::chrono::seconds(task_second));  //休眠三秒
                InfoL << "task " << i << " done!";
                vec[i] = i;
            });
        }
    }

    sem.wait();
    InfoL << "all task done, used milliseconds:" << ticker.elapsedTime();

    //打印执行结果
    for (auto i = 0; i < task_count; ++i) {
        InfoL << vec[i];
    }
    return 0;
}
