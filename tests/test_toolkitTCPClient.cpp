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

#include "Network/TcpClient.h"
#include "Util/logger.h"
using namespace std;
using namespace toolkit;

class TestClient : public TcpClient {
public:
    using Ptr = std::shared_ptr<TestClient>;
    TestClient() : TcpClient() {
        DebugL;
    }
    ~TestClient() {
        DebugL;
    }

protected:
    virtual void onConnect(const SockException &ex) override {
        //连接结果事件
        InfoL << (ex ? ex.what() : "success");
    }
    virtual void onRecv(const Buffer::Ptr &pBuf) override {
        //接收数据事件
        DebugL << pBuf->data() << " from port:" << get_peer_port();
    }
    virtual void onFlush() override {
        //发送阻塞后，缓存清空事件
        DebugL;
    }
    virtual void onErr(const SockException &ex) override {
        //断开连接事件，一般是EOF
        WarnL << ex.what();
    }
    virtual void onManager() override {
        //定时发送数据到服务器
        auto buf = BufferRaw::create();
        if (buf) {
            buf->assign("[BufferRaw]\0");
            (*this) << _nTick++ << " "
                    << 3.14 << " "
                    << string("string") << " "
                    << (Buffer::Ptr &)buf;
        }
    }

private:
    int _nTick = 0;
};

int main() {
    // 设置日志系统
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    TestClient::Ptr client(new TestClient());  //必须使用智能指针
    client->startConnect("127.0.0.1", 9001);   //连接服务器                              //连接服务器

    getchar();

    //退出程序事件处理
    return 0;
}