#include "../myNetwork/SocketUtil.hpp"
#include "../myNetwork/UDPServer.hpp"
#include "Network/Session.h"
#include "Network/Socket.h"
#include "Network/UdpServer.h"
#include "Util/logger.h"

using namespace std;
using namespace myNet;
/**
* 回显会话
*/
class tEchoSession : public toolkit::Session {
public:
    tEchoSession(const toolkit::Socket::Ptr &pSock) : toolkit::Session(pSock) {
        // DebugL;
    }
    virtual ~tEchoSession() {
        // DebugL;
    }

    void onRecv(const toolkit::Buffer::Ptr &buffer) override {
        WarnL << buffer->toString();
        send(buffer);
    }
    void onError(const toolkit::SockException &err) override {
        WarnL << err.what();
    }

    void onManager() override {}
};

class EchoSession : public Session {
public:
    EchoSession(const Socket::Ptr &pSock) : Session(pSock) {
        // DebugL;
    }
    virtual ~EchoSession() {
        // DebugL;
    }

    void onRecv(const Buffer::Ptr &buffer) override {
        WarnL << buffer->toString();
        send(buffer);
    }
    void onErr(const SocketException &err) override {
        WarnL << err.what();
    }

    void onManager() override {}
};

//赋值struct sockaddr
void makeAddr(struct sockaddr_storage *out, const char *ip, uint16_t port) {
    *out = SocketUtil::makeSockaddr(ip, port);
}

//获取struct sockaddr的IP字符串
string getIP(struct sockaddr *addr) {
    return SocketUtil::inetNtoa(addr);
}

uint16_t getPort(struct sockaddr *addr) {
    return SocketUtil::inetPort(addr);
}

int main() {
    //初始化环境
    toolkit::Logger::Instance().add(std::shared_ptr<toolkit::ConsoleChannel>(new toolkit::ConsoleChannel()));
    toolkit::Logger::Instance().setWriter(std::shared_ptr<toolkit::LogWriter>(new toolkit::AsyncLogWriter()));

    UDPServer::Ptr sv(new UDPServer);
    sv->start<EchoSession>(9001);

    // toolkit::UdpServer::Ptr sv(new toolkit::UdpServer);
    // sv->start<tEchoSession>(9001);

    // Socket::Ptr sockRecv = Socket::createSocket();  //创建一个UDP数据接收端口
    // sockRecv->bindUdpSocket(9001);                  //接收UDP绑定9001端口
    // sockRecv->setOnRead([](const Buffer::Ptr &buf, struct sockaddr *addr, int) {
    //     //接收到数据回调
    //     DebugL << "recv data form " << getIP(addr) << ":" << getPort(addr) << " :" << buf->data();
    // });

    // 使用netcat -u localhost 9001 测试
    getchar();

    // 自定义udpsock 测试
    Socket::Ptr udpsock = Socket::createSocket();
    udpsock->bindUdpSocket(0, "0.0.0.0");
    struct sockaddr_storage addrDst;
    makeAddr(&addrDst, "127.0.0.1", 9001);  //UDP数据发送地址
    udpsock->bindPeerAddr((sockaddr *)&addrDst);
    udpsock->setOnRead([](const Buffer::Ptr &buf, struct sockaddr *addr, int) {
        //接收到数据回调
        DebugL << "recv data form " << getIP(addr) << ":" << getPort(addr) << " :" << buf->data();
    });
    unsigned long long i = 0;
    while (1) {
        getchar();
        auto t = udpsock->send("test:" + to_string(i++));
        WarnL << t;
    }

    return 0;
}