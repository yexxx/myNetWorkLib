#include <csignal>
#include <iostream>

#include "Network/Socket.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
using namespace toolkit;

//主线程退出标志
bool exitProgram = false;

//赋值struct sockaddr
void makeAddr(struct sockaddr_storage *out, const char *ip, uint16_t port) {
    *out = SockUtil::make_sockaddr(ip, port);
}

//获取struct sockaddr的IP字符串
string getIP(struct sockaddr *addr) {
    return SockUtil::inet_ntoa(addr);
}

int main() {
    //设置程序退出信号处理函数
    signal(SIGINT, [](int) { exitProgram = true; });
    //设置日志系统
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    Socket::Ptr sockRecv = Socket::createSocket();  //创建一个UDP数据接收端口
    Socket::Ptr sockSend = Socket::createSocket();  //创建一个UDP数据发送端口
    sockRecv->bindUdpSock(9001);                    //接收UDP绑定9001端口
    WarnL << "sockRecv: local: " << sockRecv->get_local_ip() << ":" << sockRecv->get_local_port() << "   peer: " << sockRecv->get_peer_ip() << ":" << sockRecv->get_peer_port();
    sockSend->bindUdpSock(0, "0.0.0.0");  //发送UDP随机端口

    sockRecv->setOnRead([](const Buffer::Ptr &buf, struct sockaddr *addr, int) {
        //接收到数据回调
        DebugL << "recv data form " << getIP(addr) << ":" << buf->data();
    });

    struct sockaddr_storage addrDst;
    makeAddr(&addrDst, "127.0.0.1", 9001);  //UDP数据发送地址
    sockSend->bindPeerAddr((struct sockaddr *)&addrDst);

    WarnL << "sockRecv: local: " << sockRecv->get_local_ip() << ":" << sockRecv->get_local_port() << "   peer: " << sockRecv->get_peer_ip() << ":" << sockRecv->get_peer_port();
    WarnL << "sockSend: local: " << sockSend->get_local_ip() << ":" << sockSend->get_local_port() << "   peer: " << sockSend->get_peer_ip() << ":" << sockSend->get_peer_port();

    int i = 0;
    while (!exitProgram) {
        //每隔一秒往对方发送数据
        sockSend->send(to_string(i++), (struct sockaddr *)&addrDst);
        sleep(1);
    }
    return 0;
}