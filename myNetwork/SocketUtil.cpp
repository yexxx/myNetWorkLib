#include "SocketUtil.hpp"

#include <assert.h>
#include <fcntl.h>

#include "Util/logger.h"
#include "uv_errno.hpp"

namespace myNet {

int SocketUtil::connect(const char* host, uint16_t port, bool async, const char* localIp, uint16_t localPort) {
    sockaddr_storage addr;
    if (!getDomainIP(host, port, addr, AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        WarnL << "DNS failed.";
        return -1;
    }

    int sockfd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        WarnL << "Create socket faild.";
        return -1;
    }

    setReuseable(sockfd);
    setNoBlocked(sockfd);
    setNoDelay(sockfd);
    setSendBuf(sockfd);
    setRecvBuf(sockfd);
    setCloseWait(sockfd);
    setCloExec(sockfd);

    if (-1 == bindSock(sockfd, localIp, localPort, addr.ss_family)) {
        close(sockfd);
        return -1;
    }

    if (0 == ::connect(sockfd, (sockaddr*)&addr, getSockLen((sockaddr*)&addr))) {
        return sockfd;
    }

    WarnL << "Connect to " << host << ":" << port << " faild: " << uv_strerror(uv_translate_posix_error(errno));
    close(sockfd);

    return -1;
}

int SocketUtil::listen(const uint16_t port, const char* localIp, int backLog) {
    int sockfd = -1;
    int family = supportIpv6() ? (isIpv4(localIp) ? AF_INET : AF_INET6) : AF_INET6;
    if (-1 ==
        (sockfd = socket(family, SOCK_STREAM, IPPROTO_TCP))) {
        WarnL << "Create socket failed" << uv_strerror(uv_translate_posix_error(errno));
        return -1;
    }

    setReuseable(sockfd, true, false);
    setNoBlocked(sockfd);
    setCloExec(sockfd);

    if (-1 == bindSock(sockfd, localIp, port, family)) {
        close(sockfd);
        return -1;
    }

    if (-1 == ::listen(sockfd, backLog)) {
        WarnL << "Listen socket failed: " << uv_strerror(uv_translate_posix_error(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int SocketUtil::bindUdpSocket(const uint16_t port, const char* localIp, bool enableReuse) {
    int sockfd = -1;
    int family = supportIpv6() ? (isIpv4(localIp) ? AF_INET : AF_INET6) : AF_INET;
    if (-1 ==
        (sockfd = socket(family, SOCK_DGRAM, IPPROTO_UDP))) {
        WarnL << "Create socket failed" << uv_strerror(uv_translate_posix_error(errno));
        return -1;
    }

    if (enableReuse) setReuseable(sockfd);
    setNoBlocked(sockfd);
    setSendBuf(sockfd);
    setRecvBuf(sockfd);
    setCloseWait(sockfd);
    setCloExec(sockfd);

    if (-1 == bindSock(sockfd, localIp, port, family)) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// 原代码将addr.ss_family 设置为AF_UNSPEC, 使用::connect()实现，不知道原理，暂时不实现
int SocketUtil::dissolveUdpSocket(int sockfd) {
    return -1;
}

bool SocketUtil::getDomainIP(const char* host, uint16_t port, sockaddr_storage& addr, int family, int socketType, int protocol, int expireSec) {
    int flag = DNSCache::Instance().getDomainIP(host, addr, family, socketType, protocol, expireSec);
    if (flag) {
        if (AF_INET == addr.ss_family) {
            // htons()--"Host to Network Short" 把unsigned short类型从主机序转换到网络序
            ((sockaddr_in*)&addr)->sin_port = htons(port);
        } else if (AF_INET6 == addr.ss_family) {
            ((sockaddr_in6*)&addr)->sin6_port = htons(port);
        } else {
            assert(0);
        }
    }
    return flag;
}

int SocketUtil::getSocketError(int fd) {
    int opt;
    socklen_t optLen = sizeof(opt);
    // getsockopt用法: http://blog.chinaunix.net/uid-26948816-id-3594911.html
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &opt, &optLen);
    return uv_translate_posix_error(opt);
}

std::vector<std::pair<std::string, std::string>> SocketUtil::getNICList() {
    std::vector<std::pair<std::string, std::string>> ret;
    ifconf mifconf;
    char buf[1024 * 10];
    mifconf.ifc_len = sizeof(buf);
    // # define ifc_buf	ifc_ifcu.ifcu_buf	/* Buffer address.  */
    mifconf.ifc_buf = buf;

    // 创建一个sockfd，利用ioctl函数运行系统命令获取网卡列表
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        WarnL << "Create socket faild: " << uv_strerror(uv_translate_posix_error(errno));
        return ret;
    }
    if (-1 == ioctl(sockfd, SIOCGIFCONF, &mifconf)) {
        WarnL << "ioctl SIOCGIFCONF failed: " << uv_strerror(uv_translate_posix_error(errno));
        return ret;
    };
    close(sockfd);

    ifreq* iter = (ifreq*)buf;
    for (int i = mifconf.ifc_len / sizeof(ifreq); i > 0; --i, ++iter) {
        ret.push_back({inetNtoa(&(iter->ifr_addr)), iter->ifr_name});
    }

    return ret;
}

std::string SocketUtil::getLocalIp() {
    auto NICList = getNICList();
    auto checkIP = [](const std::string& ip) {
        if (ip != "127.0.0.1" && ip != "0.0.0.0") {
            uint32_t addressInNetworkOrder = htonl(inet_addr(ip.data()));
            if (/*(addressInNetworkOrder >= 0x0A000000 && addressInNetworkOrder < 0x0E000000) ||*/  // A类地址
                (addressInNetworkOrder >= 0xAC100000 && addressInNetworkOrder < 0xAC200000) ||      // B类地址
                (addressInNetworkOrder >= 0xC0A80000 && addressInNetworkOrder < 0xC0A90000)) {      // C类地址
                return true;
            }
        }
        return false;
    };
    for (auto& i : NICList) {
        if (checkIP(i.first)) return i.first;
    }
    return "";
}

bool SocketUtil::getSocketLocalAddr(int fd, sockaddr_storage& addr) {
    socklen_t addrLen = sizeof(addr);
    if (-1 == getsockname(fd, (sockaddr*)&addr, &addrLen)) {
        return false;
    }
    return true;
}

bool SocketUtil::getSocketPeerAddr(int fd, sockaddr_storage& addr) {
    socklen_t addrLen = sizeof(addr);
    if (-1 == getpeername(fd, (sockaddr*)&addr, &addrLen)) {
        return false;
    }
    return true;
}

std::string SocketUtil::getLocalIp(int sockfd) {
    sockaddr_storage addr;
    if (!getSocketLocalAddr(sockfd, addr)) {
        return "";
    }
    return inetNtoa((sockaddr*)&addr);
}

std::string SocketUtil::getPeerIp(int sockfd) {
    sockaddr_storage addr;
    if (!getSocketPeerAddr(sockfd, addr)) {
        return "";
    }
    return inetNtoa((sockaddr*)&addr);
}

uint16_t SocketUtil::getLocalPort(int sockfd) {
    sockaddr_storage addr;
    if (!getSocketLocalAddr(sockfd, addr)) {
        return 0;
    }
    return inetPort((sockaddr*)&addr);
}

uint16_t SocketUtil::getPeerPort(int sockfd) {
    sockaddr_storage addr;
    if (!getSocketPeerAddr(sockfd, addr)) {
        return 0;
    }
    return inetPort((sockaddr*)&addr);
}

std::string SocketUtil::inetNtoa(const in_addr& addr) {
    std::string ret;
    ret.resize(128);
    if (!inet_ntop(AF_INET, const_cast<void*>((void*)&addr), (char*)ret.data(), ret.size())) {
        ret.clear();
    } else {
        ret.resize(strlen(ret.data()));
    }
    return ret;
}

std::string SocketUtil::inetNtoa(const in6_addr& addr) {
    std::string ret;
    ret.resize(128);
    if (!inet_ntop(AF_INET6, const_cast<void*>((void*)&addr), (char*)ret.data(), ret.size())) {
        ret.clear();
    } else {
        ret.resize(strlen(ret.data()));
    }
    return ret;
}

std::string SocketUtil::inetNtoa(const sockaddr* addr) {
    if (addr->sa_family == AF_INET) {
        return inetNtoa(((sockaddr_in*)addr)->sin_addr);
    } else if (addr->sa_family == AF_INET6) {
        // IN6_IS_ADDR_V4MAPPED: 是否是IPv4映射的IPv6地址
        if (IN6_IS_ADDR_V4MAPPED(&(((sockaddr_in6*)addr)->sin6_addr))) {
            in_addr addr4;
            memcpy(&addr4, 12 + (char*)&(((sockaddr_in6*)addr)->sin6_addr), 4);
            return inetNtoa(addr4);
        }
        // return inetNtoa(((sockaddr_in6*)&addr)->sin6_addr);  这个地方加个取地址不会报错，写顺手了找起bug来真的难顶
        return inetNtoa(((sockaddr_in6*)addr)->sin6_addr);
    }

    assert(0);
    return "";
}

uint16_t SocketUtil::inetPort(const sockaddr* addr) {
    switch (addr->sa_family) {
        // ntohs：网络字节序转换成主机字节序（涉及大小端转换）
        case AF_INET: return ntohs(((sockaddr_in*)addr)->sin_port);
        case AF_INET6: return ntohs(((sockaddr_in6*)addr)->sin6_port);
        default: assert(0); return 0;
    }
}

socklen_t SocketUtil::getSockLen(const sockaddr* addr) {
    socklen_t addrLen;
    if (addr->sa_family == AF_INET) {
        addrLen = sizeof(sockaddr_in);
    } else if (addr->sa_family == AF_INET6) {
        addrLen = sizeof(sockaddr_in6);
    } else {
        assert(0);
    }
    return addrLen;
}

sockaddr_storage SocketUtil::makeSockaddr(const char* ip, uint16_t port) {
    sockaddr_storage storage;
    bzero(&storage, sizeof(storage));

    in_addr addr;
    in6_addr addr6;

    // 这个地方要传入addr，所以不能用 isIpv4() 判断
    if (1 == inet_pton(AF_INET, ip, &addr)) {
        reinterpret_cast<sockaddr_in&>(storage).sin_addr = addr;
        reinterpret_cast<sockaddr_in&>(storage).sin_family = AF_INET;
        reinterpret_cast<sockaddr_in&>(storage).sin_port = htons(port);
        return storage;
    }

    if (1 == inet_pton(AF_INET, ip, &addr)) {
        reinterpret_cast<sockaddr_in6&>(storage).sin6_addr = addr6;
        reinterpret_cast<sockaddr_in6&>(storage).sin6_family = AF_INET6;
        reinterpret_cast<sockaddr_in6&>(storage).sin6_port = htons(port);
        return storage;
    }

    throw std::invalid_argument(std::string("Not ip address: ") + std::string(ip) + ":" + std::to_string(port));
}

std::string SocketUtil::getNICIP(const char* NICName) {
    auto NICList = getNICList();
    for (auto& i : NICList) {
        if (i.second.data() == NICName) {
            return i.first;
        }
    }

    return "";
}

std::string SocketUtil::getNICName(const char* localIp) {
    auto NICList = getNICList();
    for (auto& i : NICList) {
        if (i.first.data() == localIp) {
            return i.second;
        }
    }

    return "";
}

std::string SocketUtil::getNICMask(const char* NICName) {
    int fd;
    ifreq NICMask;
    if (-1 == socket(AF_INET, SOCK_STREAM, 0)) {
        WarnL << "Create socket failed: " << uv_strerror(uv_translate_posix_error(errno));
        return "";
    }
    bzero(&NICMask, sizeof(NICMask));
    strncpy(NICMask.ifr_name, NICName, sizeof(NICMask.ifr_name) - 1);

    if (0 > ioctl(fd, SIOCGIFNETMASK, &NICMask)) {
        WarnL << "ioctl SIOCGIFNETMASK on " << NICName << " failed: " << uv_strerror(uv_translate_posix_error(errno));
        close(fd);
        return "";
    }

    close(fd);
    return inetNtoa(&(NICMask.ifr_netmask));
}

std::string SocketUtil::getNICBoardAddr(const char* NICName) {
    int fd;
    ifreq NICBroadaddr;
    if (-1 == socket(AF_INET, SOCK_STREAM, 0)) {
        WarnL << "Create socket failed: " << uv_strerror(uv_translate_posix_error(errno));
        return "";
    }
    bzero(&NICBroadaddr, sizeof(NICBroadaddr));
    strncpy(NICBroadaddr.ifr_name, NICName, sizeof(NICBroadaddr.ifr_name) - 1);

    if (0 > ioctl(fd, SIOCGIFBRDADDR, &NICBroadaddr)) {
        WarnL << "ioctl SIOCGIFBRDADDR on " << NICName << " failed: " << uv_strerror(uv_translate_posix_error(errno));
        close(fd);
        return "";
    }

    close(fd);
    return inetNtoa(&(NICBroadaddr.ifr_broadaddr));
}

bool SocketUtil::isSanmeLan(const char* srcIp, const char* dstIp) {
    auto mask = getNICMask(getNICName(srcIp).data()).data();
    // inet_addr: 将ip 从点分制转为二进制
    return ((inet_addr(srcIp) & inet_addr(mask)) == (inet_addr(dstIp) & inet_addr(mask)));
}

bool SocketUtil::isIpv4(const char* ip) {
    in_addr addr;
    // inet_pton ip表达式形式转数值形式, 返回1表示成功
    // 若能转换，说明是ipv4
    return (1 == inet_pton(AF_INET, ip, &addr));
}

bool SocketUtil::isIpv6(const char* ip) {
    in6_addr addr6;
    return (1 == inet_pton(AF_INET6, ip, &addr6));
}

int SocketUtil::setNoDelay(int fd, bool on) {
    if (-1 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (int*)&on, sizeof(int))) {
        TraceL << "setsockopt TCP_NODELAY failed.";
        return -1;
    }
    return 0;
}

int SocketUtil::setNoBlocked(int fd, bool noBlock) {
    if (-1 == ioctl(fd, FIONBIO, &noBlock)) {
        TraceL << "ioctl FIONBIO failed.";
        return -1;
    }
    return 0;
}

int SocketUtil::setRecvBuf(int fd, int size) {
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size))) {
        TraceL << "setsockopt SO_RCVBUF failed.";
        return -1;
    }
    return 0;
}

int SocketUtil::setSendBuf(int fd, int size) {
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))) {
        TraceL << "setsockopt SO_SNDBUF failed.";
        return -1;
    }
    return 0;
}

int SocketUtil::setReuseable(int fd, bool on, bool reusePort) {
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (int*)&on, sizeof(int))) {
        TraceL << "setsockopt SO_REUSEADDR failed.";
        return -1;
    }
#ifdef SO_REUSEPORT
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (int*)&on, sizeof(int))) {
        TraceL << "setsockopt SO_REUSEPORT failed.";
        return -1;
    }
#endif
    return 0;
}

int SocketUtil::setBroadcast(int fd, bool on) {
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (int*)&on, sizeof(int))) {
        TraceL << "setsockopt SO_BROADCAST failed.";
        return -1;
    }
    return 0;
}

int SocketUtil::setKeepAlive(int fd, bool on, int interval, int keepAliveTime, int times) {
#if !defined(SOL_TCP) && defined(IPPROTO_TCP)
#define SOL_TCP IPPROTO_TCP
#endif
#if !defined(TCP_KEEPIDLE) && defined(TCP_KEEPALIVE)
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

    int ret = 0;
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const int*)&on, sizeof(int))) {
        TraceL << "setsockopt SO_KEEPALIVE failed.";
        ret = -1;
    }

    if (on && interval > 0) {
        if (-1 == setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (int*)&keepAliveTime, sizeof(keepAliveTime))) {
            TraceL << "setsockopt SO_KEEPALIVE failed.";
            ret = -1;
        }
        if (-1 == setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (int*)&interval, sizeof(interval))) {
            TraceL << "setsockopt SO_KEEPALIVE failed.";
            ret = -1;
        }
        if (-1 == setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (int*)&times, sizeof(times))) {
            TraceL << "setsockopt SO_KEEPALIVE failed.";
            ret = -1;
        }
    }
    return ret;
}

int SocketUtil::setCloExec(int fd, bool on) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        TraceL << "fcntl F_GETFD failed";
        return -1;
    }
    if (on) {
        flags |= FD_CLOEXEC;
    } else {
        int cloexec = FD_CLOEXEC;
        flags &= ~cloexec;
    }
    int ret = fcntl(fd, F_SETFD, flags);
    if (ret == -1) {
        TraceL << "fcntl F_SETFD failed";
        return -1;
    }
    return ret;
}

int SocketUtil::setCloseWait(int sockfd, int second) {
    linger mlinger;
    mlinger.l_onoff = (second > 0);
    mlinger.l_linger = second;
    if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &mlinger, sizeof(mlinger))) {
        TraceL << "setsockopt SO_LINGER failed.";
        return -1;
    }
    return 0;
}

int SocketUtil::bindSock(int sockfd, const char* NICIp, uint16_t port, int family) {
    switch (family) {
        case AF_INET: return bindSock4(sockfd, NICIp, port);
        case AF_INET6: return bindSock6(sockfd, NICIp, port);
        default: assert(0); return -1;
    }
}

int SocketUtil::bindSock4(int sockfd, const char* NICIp, uint16_t port) {
    sockaddr_in addr;
    bzero(&addr, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (1 != inet_pton(AF_INET, NICIp, &(addr.sin_addr))) {
        // 原代码这儿是 如果ip是"::" 就不报错，感觉没必要
        WarnL << "inet_pton to ipv4 address failed: " << NICIp;
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    if (-1 == ::bind(sockfd, (sockaddr*)&addr, sizeof(addr))) {
        WarnL << "Bind socket failed: " << uv_strerror(uv_translate_posix_error(errno));
        return -1;
    }
    return 0;
}

int SocketUtil::bindSock6(int sockfd, const char* NICIp, uint16_t port) {
    int opt = false;
    // set ipv6 only
    if (-1 == setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&opt, sizeof(opt))) {
        TraceL << "setsockopt IPV6_V6ONLY failed";
    }
    sockaddr_in6 addr6;
    bzero(&addr6, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(port);

    if (1 != inet_pton(AF_INET6, NICIp, &(addr6.sin6_addr))) {
        WarnL << "inet_pton to ipv4 address failed: " << NICIp;
        addr6.sin6_addr = IN6ADDR_ANY_INIT;
    }
    if (-1 == ::bind(sockfd, (sockaddr*)&addr6, sizeof(addr6))) {
        WarnL << "Bind socket failed: " << uv_strerror(uv_translate_posix_error(errno));
        return -1;
    }
    return 0;
}

bool SocketUtil::supportIpv6() {
    auto fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (-1 == fd) {
        return false;
    }
    close(fd);
    return true;
}

bool DNSCache::getDomainIP(const char* host, sockaddr_storage& storage, int family, int sockType, int protocol, int expireSec) {
    try {
        storage = SocketUtil::makeSockaddr(host, 0);
        return true;
    } catch (...) {
        auto sharedAddr = getCacheDomainIP(host, expireSec);
        if (!sharedAddr) {
            sharedAddr = getSystemDomainIP(host);
            if (sharedAddr) {
                setCacheDomainIP(host, sharedAddr);
            }
        }

        if (sharedAddr) {
            auto addr = getPerferredAddress(sharedAddr.get(), family, sockType, protocol);
            memcpy(&storage, addr->ai_addr, addr->ai_addrlen);
            return true;
        }
    }
    return false;
}

std::shared_ptr<addrinfo> DNSCache::getCacheDomainIP(const char* host, int expireSec) {
    std::lock_guard<std::mutex> lck(_mtx);
    auto cacheIP = _dnsCache.find(host);
    if (cacheIP == _dnsCache.end()) {
        return nullptr;
    }
    if (cacheIP->second.createTime + expireSec < time(nullptr)) {
        _dnsCache.erase(cacheIP);
        return nullptr;
    }
    return cacheIP->second._addrInfo;
}

std::shared_ptr<addrinfo> DNSCache::getSystemDomainIP(const char* host) {
    addrinfo* ret = nullptr;
    while (-1 == getaddrinfo(host, nullptr, nullptr, &ret) && UV_EINTR == uv_translate_posix_error(errno)) {};
    if (!ret) {
        WarnL << "getaddrinfo failed: " << host;
        return nullptr;
    };

    return std::shared_ptr<addrinfo>(ret, freeaddrinfo);
}

void DNSCache::setCacheDomainIP(const char* host, std::shared_ptr<addrinfo> addr) {
    std::lock_guard<std::mutex> lck(_mtx);
    DNSItem item;
    item._addrInfo = std::move(addr);
    item.createTime = time(nullptr);
    _dnsCache[host] = item;
}

addrinfo* DNSCache::getPerferredAddress(addrinfo* ret, int family, int socketType, int protocol) {
    while (ret) {
        if (ret->ai_family == family && ret->ai_socktype == socketType && ret->ai_protocol == protocol) {
            return ret;
        }
        ret = ret->ai_next;
    }
    return ret;
}

}  // namespace myNet