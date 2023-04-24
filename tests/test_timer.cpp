#include <csignal>
#include <iostream>

#include "../myPoller/EventPollerApp.hpp"
#include "Util/TimeTicker.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
// using namespace toolkit;
using namespace myNet;

int main() {
    // 设置日志
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());
    toolkit::Logger::Instance().setWriter(std::make_shared<toolkit::AsyncLogWriter>());

    toolkit::Ticker ticker0;
    Timer::Ptr timer0 = std::make_shared<Timer>(0.5f, nullptr, [&]() {
        TraceL << "timer0重复:" << ticker0.elapsedTime();
        ticker0.resetTime();
        return true;
    });

    Timer::Ptr timer1 = std::make_shared<Timer>(1.0f, nullptr, []() {
        DebugL << "timer1不再重复";
        return false;
    });

    toolkit::Ticker ticker2;
    Timer::Ptr timer2 = std::make_shared<Timer>(2.0f, nullptr, [&]() -> bool {
        InfoL << "timer2,测试任务中抛异常" << ticker2.elapsedTime();
        ticker2.resetTime();
        throw std::runtime_error("timer2,测试任务中抛异常");
    });

    // 退出程序事件处理
    static Semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });  // 设置退出信号
    sem.wait();
    return 0;
}
