#include <csignal>
#include <iostream>

#include "../myNetwork/Buffer.hpp"
#include "../myNetwork/Socket.hpp"
#include "../myNetwork/SocketUtil.hpp"
#include "Network/sockutil.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;

// 主线程退出标志
bool exitProgram = false;

// 赋值struct sockaddr
void makeAddr(struct sockaddr_storage* out, const char* ip, uint16_t port) {
    *out = myNet::SocketUtil::makeSockaddr(ip, port);
}

// 获取struct sockaddr的IP字符串
string getIP(struct sockaddr* addr) {
    return myNet::SocketUtil::inetNtoa(addr);
}

uint16_t getPort(struct sockaddr* addr) {
    return myNet::SocketUtil::inetPort(addr);
}

int main() {
    // 设置程序退出信号处理函数
    //  signal(SIGINT, [](int) { exitProgram = true; });
    // 设置日志系统
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>());
    toolkit::Logger::Instance().setWriter(std::make_shared<toolkit::AsyncLogWriter>());

    myNet::Socket::Ptr sockRecv = myNet::Socket::createSocket(); // 创建一个UDP数据接收端口
    myNet::Socket::Ptr sockSend = myNet::Socket::createSocket(); // 创建一个UDP数据发送端口
    sockRecv->bindUdpSocket(9001);                               // 接收UDP绑定9001端口
    sockSend->bindUdpSocket(0, "0.0.0.0");                       // 发送UDP随机端口

    sockRecv->setOnRead([](const myNet::Buffer::Ptr& buf, struct sockaddr* addr, int) {
        // 接收到数据回调
        DebugL << "recv data form " << getIP(addr) << ":" << getPort(addr) << " :" << buf->data();
    });

    struct sockaddr_storage addrDst;
    makeAddr(&addrDst, "127.0.0.1", 9001); // UDP数据发送地址
    sockSend->bindPeerAddr((sockaddr*)&addrDst);

    WarnL << "sockRecv: local: " << sockRecv->get_localIP() << ":" << sockRecv->get_localPort() << "   peer: " << sockRecv->get_peerIP() << ":" << sockRecv->get_peerPort();
    WarnL << "sockSend: local: " << sockSend->get_localIP() << ":" << sockSend->get_localPort() << "   peer: " << sockSend->get_peerIP() << ":" << sockSend->get_peerPort();

    long long i = 0;
    while (!exitProgram) {
        // 每隔一秒往对方发送数据
        sockSend->send(" i: " + to_string(i++));
        sleep(1);
    }
    return 0;
}
