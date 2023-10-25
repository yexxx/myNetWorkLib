#ifndef SocketUtil_hpp
#define SocketUtil_hpp

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace myNet {

#define SOCKET_DEFAULT_BUF_SIZE (256 * 1024)
#define TCP_KEEPALIVE_INTERVAL 30
#define TCP_KEEPALIVE_PROBE_TIMES 9
#define TCP_KEEPALIVE_TIME 120

class SocketUtil {
  public:
    // 创建tcp客户端套接字并连接服务器
    static int connect(const char* host, uint16_t port, bool async = true, const char* localIp = "::", uint16_t localPort = 0);

    // 创建tcp监听套接字
    static int listen(const uint16_t port, const char* localIp = "::", int backLog = 1024);

    // 创建udp套接字
    static int bindUdpSocket(const uint16_t port, const char* localIp = "::", bool enableReuse = true);

    // 解除与sock的绑定
    static int dissolveUdpSocket(int sockfd);

    // dns 解析
    static bool getDomainIP(const char* host, uint16_t port, sockaddr_storage& addr, int family = AF_INET, int socketType = SOCK_STREAM, int protocol = IPPROTO_TCP, int expireSec = 60);

    // 获取socket 当前发生的错误
    static int getSocketError(int fd);

    // 获取网卡列表 {ip, name} 数组
    static std::vector<std::pair<std::string, std::string>> getNICList();

    // 获取本机默认网卡ip
    static std::string getLocalIp();

    // 通过sockfd获取本地socket地址
    static bool getSocketLocalAddr(int fd, sockaddr_storage& addr);

    // 通过sockfd获取远端socket地址
    static bool getSocketPeerAddr(int fd, sockaddr_storage& addr);

    // 获取socket绑定的本地IP
    static std::string getLocalIp(int sockfd);

    // 获取socket绑定的远端IP
    static std::string getPeerIp(int sockfd);

    // 获取socket 绑定的本地端口
    static uint16_t getLocalPort(int sockfd);

    // 获取socket 绑定的远端端口
    static uint16_t getPeerPort(int sockfd);

    static std::string inetNtoa(const in_addr& addr);
    static std::string inetNtoa(const in6_addr& addr);
    // 线程安全的in_addr转ip字符串(自带的inet_ntoa 线程不安全，因为其返回数据(char* )的位置是固定的，可能被覆盖)
    static std::string inetNtoa(const sockaddr* addr);

    // 获取addr 端口
    static uint16_t inetPort(const sockaddr* addr);

    // 获取socket长度
    static socklen_t getSockLen(const sockaddr* addr);

    // 给定addr和port，转换成sockaddr形式，使用时需捕获异常
    static sockaddr_storage makeSockaddr(const char* ip, uint16_t port);

    // 获取网卡IP
    static std::string getNICIP(const char* NICName);

    // 获取网卡名
    static std::string getNICName(const char* localIp);

    // 根据网卡名获取子网掩码
    static std::string getNICMask(const char* NICName);

    // 根据网卡名获取广播地址
    static std::string getNICBoardAddr(const char* NICName);

    // 判断两个IP 是否为同网段
    static bool isSanmeLan(const char* srcIp, const char* dstIp);

    // 判断是否ipv4 地址
    static bool isIpv4(const char* ip);

    // 判断是否ipv6 地址
    static bool isIpv6(const char* ip);

    // TCP_NODELAY, 降低tcp延时
    static int setNoDelay(int fd, bool on = true);

    // // 不触发sig pipe信号, 仅苹果需要，这里不实现
    // static int setNoSigpipe(int fd);

    // setsockopt: https://blog.csdn.net/JMW1407/article/details/107321853 里面的表

    // 设置读写socket是是否阻塞
    static int setNoBlocked(int fd, bool noBlock = true);

    // 设置接收缓存大小
    static int setRecvBuf(int fd, int size = SOCKET_DEFAULT_BUF_SIZE);

    // 设置发送缓存大小
    static int setSendBuf(int fd, int size = SOCKET_DEFAULT_BUF_SIZE);

    // 地址复用和端口复用: https://cloud.tencent.com/developer/article/1968846
    // https://cloud.tencent.com/developer/article/1844163
    // 设置可绑定复用端口
    static int setReuseable(int fd, bool on = true, bool reusePort = true);

    // 发送或接收udp广播信息
    static int setBroadcast(int fd, bool on = true);

    // tcp keep alive 特性
    // interval: 探测时间间隔
    // keepAliveTime: 空闲时间
    // times: 探测次数
    static int setKeepAlive(int fd, bool on = true, int interval = TCP_KEEPALIVE_INTERVAL, int keepAliveTime = TCP_KEEPALIVE_TIME, int times = TCP_KEEPALIVE_PROBE_TIMES);

    // FD_CLOEXEC 特性 (在执行exec 时，是否关闭fd)
    // 没看懂具体是怎么实现的
    static int setCloExec(int fd, bool on = true);

    // SO_LINGER 特性
    // SO_LINGER选项用来设置延迟关闭的时间，等待套接字发送缓冲区中的数据发送完成。 https://www.cnblogs.com/kex1n/p/7401042.html
    static int setCloseWait(int sockfd, int second = 0);

    /* 组播特性，暂时不实现

    // 设置组播ttl
    static int setMultiTTL(int sockfd, uint8_t ttl = 64);

    // 设置组播发送网卡
    static int setMultiNIC(int sockfd, const char* localIp);

    // 设置是否接收本地发送的组播包
    static int setMultiLoop(int fd, bool acc = false);

    // 加入组播
    static int joinMultiAddr(int fd, const char* addr, const char* localIp = "0.0.0.0");

    // 退出组播
    static int leaveMultiAddr(int fd, const char* addr, const char* local_ip = "0.0.0.0");

    // 加入组播并只接受该源端的组播数据
    static int joinMultiAddrFilter(int sock, const char* addr, const char* src_ip, const char* local_ip = "0.0.0.0");

    // 退出组播
    static int leaveMultiAddrFilter(int fd, const char* addr, const char* src_ip, const char* local_ip = "0.0.0.0");

    */

  private:
    // 将sockfd与本地地址绑定
    static int bindSock(int sockfd, const char* NICIp, uint16_t port, int family);
    static int bindSock4(int sockfd, const char* NICIp, uint16_t port);
    static int bindSock6(int sockfd, const char* NICIp, uint16_t port);

    // 是否支持ipv6, 通过创建v6 fd确定
    static bool supportIpv6();
};

class DNSCache {
  public:
    // 静态对象在内存中一般只有一份，方便共用
    static DNSCache& Instance() {
        static DNSCache instance;
        return instance;
    }

    bool getDomainIP(const char* host, sockaddr_storage& storage, int family = AF_INET, int sockType = SOCK_STREAM, int protocol = IPPROTO_TCP, int expireSec = 60);

  private:
    // 这个地方用shared_ptr 没看懂为什么

    std::shared_ptr<addrinfo> getCacheDomainIP(const char* host, int expireSec);

    // 通过系统解析dns
    std::shared_ptr<addrinfo> getSystemDomainIP(const char* host);

    void setCacheDomainIP(const char* host, std::shared_ptr<addrinfo> addr);

    // 获取首选地址
    addrinfo* getPerferredAddress(addrinfo* ret, int family, int socketType, int protocol);

    struct DNSItem {
        std::shared_ptr<addrinfo> _addrInfo;
        time_t createTime;
    };
    std::mutex _mtx;
    std::unordered_map<std::string, DNSItem> _dnsCache;
};

} // namespace myNet

#endif // SocketUtil_hpp
