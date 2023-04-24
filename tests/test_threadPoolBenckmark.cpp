/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <atomic>
#include <csignal>
#include <iostream>

#include "../myThread/ThreadPool.hpp"
#include "Util/TimeTicker.h"
#include "Util/logger.h"

using namespace std;
// using namespace toolkit;
using namespace myNet;

// pass
// 2023-03-19 20:03:23.261 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:54 main | 1000万任务入队耗时:5711ms
// 2023-03-19 20:03:24.262 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main | 每秒执行任务数:1554906
// 2023-03-19 20:03:25.262 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main | 每秒执行任务数:1527506
// 2023-03-19 20:03:26.262 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main | 每秒执行任务数:1538716
// 2023-03-19 20:03:27.262 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main | 每秒执行任务数:1560831
// 2023-03-19 20:03:28.262 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main | 每秒执行任务数:1563999
// 2023-03-19 20:03:29.263 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main | 每秒执行任务数:1567136
// 2023-03-19 20:03:29.708 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:50 operator() |
// 执行1000万任务总共耗时:6447ms 2023-03-19 20:03:30.263 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main |
// 每秒执行任务数:686906 2023-03-19 20:03:31.263 I [test_threadPoolBenckmark] [5870-test_threadPool] test_threadPoolBenckmark.cpp:62 main |
// 每秒执行任务数:0 2023-03-19 20:03:31.263 I [test_threadPoolBenckmark] [5870-test_threadPool] logger.cpp:86 ~Logger |

int main() {
    signal(SIGINT, [](int) { exit(0); });
    // 初始化日志系统
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());

    atomic_llong count(0);
    ThreadPool pool(1, ThreadPool::PRIORITY_HIGHEST, false);

    toolkit::Ticker ticker;
    for (int i = 0; i < 1000 * 10000; ++i) {
        pool.async([&]() {
            if (++count >= 1000 * 10000) {
                InfoL << "执行1000万任务总共耗时:" << ticker.elapsedTime() << "ms";
            }
        });
    }
    InfoL << "1000万任务入队耗时:" << ticker.elapsedTime() << "ms" << endl;
    uint64_t lastCount = 0, nowCount = 1;
    ticker.resetTime();
    // 此处才开始启动线程
    pool.start();
    while (true) {
        sleep(1);
        nowCount = count.load();
        InfoL << "每秒执行任务数:" << nowCount - lastCount;
        if (nowCount - lastCount == 0) {
            break;
        }
        lastCount = nowCount;
    }
    return 0;
}
