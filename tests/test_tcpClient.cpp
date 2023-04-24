#include <csignal>
#include <iostream>

#include "../myNetwork/TCPClient.hpp"
#include "Util/logger.h"
using namespace std;
using namespace myNet;

class TestClient : public TCPClient {
public:
    using Ptr = std::shared_ptr<TestClient>;

    TestClient() {}
    ~TestClient() {}

protected:
    virtual void onRecv(const Buffer::Ptr &buf) override {
        // 接收数据事件
        // DebugL << buf->data() << " from port:" << get_peerPort();
    }
    virtual void onFlush() override {
        // 发送阻塞后，缓存清空事件
        //  DebugL << "onFlush";
    }
    virtual void onManager() override {
        // 定时发送数据到服务器
        // auto buf = BufferRaw::create();
        // buf->assign("[BufferRaw]\0");
        // (*this) << to_string(_nTick++) << " " << (Buffer::Ptr &)buf;
    }

private:
    int _nTick = 0;
};

int main() {
    // 设置日志系统
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());
    toolkit::Logger::Instance().setWriter(std::make_shared<toolkit::AsyncLogWriter>());

    TestClient::Ptr client(new TestClient());
    client->connect("localhost", 9001);  // 连接服务器

    if (getchar()) {
        WarnL << client->getSocket()->getFd() << " : " << client->get_localIP() << " " << client->get_localPort() << " " << client->get_peerIP()
              << " " << client->get_peerPort();
    }

    string oneKByteSting;
    for (auto i = 0; i < 1000; ++i) oneKByteSting.push_back('-');
    string _1000KbytesString;
    for (auto i = 0; i < 1000; ++i) _1000KbytesString += oneKByteSting;
    _1000KbytesString.push_back('\0');
    getchar();

    long long i = 0;
    while (true) {
        *client << _1000KbytesString;
        WarnL << "send " << ++i << " MBytes data" << endl;
        // getchar();
    }

    // 退出程序事件处理

    return 0;
}