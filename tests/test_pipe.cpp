/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <csignal>
#include <iostream>

#include "../myPoller/EventPollerApp.hpp"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
// using namespace toolkit;
using namespace myNet;

int main() {
    // 设置日志
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());

    // 获取父进程的PID
    auto parentPid = getpid();
    InfoL << "parent pid:" << parentPid << endl;

    // 定义一个管道，lambada类型的参数是管道收到数据的回调
    PipeEventPoller pipe(nullptr, [](const char* buf, int size) {
        // 该管道有数据可读了
        InfoL << getpid() << " recv:" << buf;
    });

    // 创建子进程
    auto pid = fork();

    if (pid == 0) {
        // 子进程
        int i = 10;
        while (i--) {
            // 在子进程每隔一秒把数据写入管道，共计发送10次
            sleep(1);
            string msg = StrPrinter << "message " << i << " form subprocess:" << getpid();
            DebugL << "子进程发送:" << msg << endl;
            pipe.send(msg.data(), msg.size());
        }
        DebugL << "子进程退出" << endl;
    } else {
        // 父进程设置退出信号处理函数
        static Semaphore sem;
        signal(SIGINT, [](int) { sem.post(); }); // 设置退出信号
        sem.wait();

        InfoL << "父进程退出" << endl;
    }

    return 0;
}
