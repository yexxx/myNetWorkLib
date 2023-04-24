#include "../myNetwork/SocketUtil.hpp"
#include "../myNetwork/TCPServer.hpp"
#include "Util/logger.h"

using namespace std;
using namespace myNet;

long long i = 0;
long long MB_lst = 0;

class EchoSession : public Session {
public:
    EchoSession(const Socket::Ptr &pSock) : Session(pSock) {
        // DebugL;
    }
    virtual ~EchoSession() {
        // DebugL;
    }

    void onRecv(const Buffer::Ptr &buffer) override {
        i += (buffer->toString()).size();
        // WarnL << i;
        if (i / (1000 * 1000) > MB_lst) {
            WarnL << "Recv " << i / (1000 * 1000) << " MBytes data";
            MB_lst = i / (1000 * 1000);
        }
        // WarnL << buffer->toString();
        send(buffer);
    }
    void onErr(const SocketException &err) override { WarnL << err.what(); }

    void onManager() override {}
};

// 赋值struct sockaddr
void makeAddr(struct sockaddr_storage *out, const char *ip, uint16_t port) {
    *out = SocketUtil::makeSockaddr(ip, port);
}

// 获取struct sockaddr的IP字符串
string getIP(struct sockaddr *addr) {
    return SocketUtil::inetNtoa(addr);
}

uint16_t getPort(struct sockaddr *addr) {
    return SocketUtil::inetPort(addr);
}

int main() {
    // 初始化环境
    toolkit::Logger::Instance().add(std::shared_ptr<toolkit::ConsoleChannel>(new toolkit::ConsoleChannel()));
    toolkit::Logger::Instance().setWriter(std::shared_ptr<toolkit::LogWriter>(new toolkit::AsyncLogWriter()));

    TCPServer::Ptr sv(new TCPServer);
    sv->start<EchoSession>(9001);

    // 使用telnet localhost 9001 测试
    getchar();

    return 0;
}