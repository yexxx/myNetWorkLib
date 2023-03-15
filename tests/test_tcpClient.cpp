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
    virtual void onRecv(const Buffer::Ptr &pBuf) override {
        //接收数据事件
        DebugL << pBuf->data() << " from port:" << get_peerPort();
    }
    virtual void onFlush() override {
        //发送阻塞后，缓存清空事件
        DebugL << "onFlush";
    }
    virtual void onManager() override {
        //定时发送数据到服务器
        auto buf = BufferRaw::create();
        buf->assign("[BufferRaw]\0");
        (*this) << to_string(_nTick++) << " " << (Buffer::Ptr &)buf;
    }

private:
    int _nTick = 0;
};

int main() {
    // 设置日志系统
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());
    toolkit::Logger::Instance().setWriter(std::make_shared<toolkit::AsyncLogWriter>());

    TestClient::Ptr client(new TestClient());
    client->connect("127.0.0.1", 9001);  //连接服务器

    while (1) {
        getchar();
        WarnL << client->get_localIP() << " " << client->get_localPort();
        WarnL << client->get_peerIP() << " " << client->get_peerPort();
    }

    // 退出程序事件处理
    return 0;
}