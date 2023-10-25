#include <arpa/inet.h>
#include <assert.h>
#include <bits/stdc++.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

static inline string my_inet_ntop(int af, const void* addr);
string inetNtoa(const struct in_addr& addr);
std::string inetNtoa(const struct in6_addr& addr);
std::string inetNtoa(const struct sockaddr* addr);

static inline string my_inet_ntop(int af, const void* addr) {
    string ret;
    ret.resize(128);
    if (!inet_ntop(af, const_cast<void*>(addr), (char*)ret.data(), ret.size())) {
        ret.clear();
    } else {
        ret.resize(strlen(ret.data()));
    }
    return ret;
}

string inetNtoa(const struct in_addr& addr) { return my_inet_ntop(AF_INET, &addr); }

std::string inetNtoa(const struct in6_addr& addr) { return my_inet_ntop(AF_INET6, &addr); }

std::string inetNtoa(const struct sockaddr* addr) {
    switch (addr->sa_family) {
    case AF_INET:
        return inetNtoa(((struct sockaddr_in*)addr)->sin_addr);
    case AF_INET6: {
        if (IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6*)addr)->sin6_addr)) {
            struct in_addr addr4;
            memcpy(&addr4, 12 + (char*)&(((struct sockaddr_in6*)addr)->sin6_addr), 4);
            return inetNtoa(addr4);
        }
        return inetNtoa(((struct sockaddr_in6*)addr)->sin6_addr);
    }
    default:
        assert(false);
        return "";
    }
}

std::vector<std::pair<std::string, std::string>> getNICList() {
    std::vector<std::pair<std::string, std::string>> ret;
    ifconf mifconf;
    char buf[1024 * 10];
    mifconf.ifc_len = sizeof(buf);
    // # define ifc_buf	ifc_ifcu.ifcu_buf	/* Buffer address.  */
    mifconf.ifc_buf = buf;

    // 创建一个sockfd，利用ioctl函数运行系统命令获取网卡列表
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return ret;
    }
    if (-1 == ioctl(sockfd, SIOCGIFCONF, &mifconf)) {
        return ret;
    };
    ::close(sockfd);

    ifreq* iter = (ifreq*)buf;
    for (int i = mifconf.ifc_len / sizeof(ifreq); i > 0; --i, ++iter) {
        ret.push_back({inetNtoa(&(iter->ifr_addr)), iter->ifr_name});
    }

    return ret;
}

template <typename FUN> void for_each_netAdapter_posix(FUN&& fun) { // type: struct ifreq *
    struct ifconf ifconf;
    char buf[1024 * 10];
    // 初始化ifconf
    ifconf.ifc_len = sizeof(buf);
    ifconf.ifc_buf = buf;
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return;
    }
    if (-1 == ioctl(sockfd, SIOCGIFCONF, &ifconf)) { // 获取所有接口信息
        close(sockfd);
        return;
    }
    close(sockfd);
    // 接下来一个一个的获取IP地址
    struct ifreq* adapter = (struct ifreq*)buf;
    for (int i = (ifconf.ifc_len / sizeof(struct ifreq)); i > 0; --i, ++adapter) {
        if (fun(adapter)) {
            break;
        }
    }
}

bool check_ip(string& address, const string& ip) {
    if (ip != "127.0.0.1" && ip != "0.0.0.0") {
        /*获取一个有效IP*/
        address = ip;
        uint32_t addressInNetworkOrder = htonl(inet_addr(ip.data()));
        if (/*(addressInNetworkOrder >= 0x0A000000 && addressInNetworkOrder < 0x0E000000) ||*/
            (addressInNetworkOrder >= 0xAC100000 && addressInNetworkOrder < 0xAC200000) || (addressInNetworkOrder >= 0xC0A80000 && addressInNetworkOrder < 0xC0A90000)) {
            // A类私有IP地址：
            // 10.0.0.0～10.255.255.255
            // B类私有IP地址：
            // 172.16.0.0～172.31.255.255
            // C类私有IP地址：
            // 192.168.0.0～192.168.255.255
            // 如果是私有地址 说明在nat内部

            /* 优先采用局域网地址，该地址很可能是wifi地址
             * 一般来说,无线路由器分配的地址段是BC类私有ip地址
             * 而A类地址多用于蜂窝移动网络
             */
            return true;
        }
    }
    return false;
}

string get_local_ip() {
    string address = "127.0.0.1";
    for_each_netAdapter_posix([&](struct ifreq* adapter) {
        string ip = inetNtoa(&(adapter->ifr_addr));
        if (strstr(adapter->ifr_name, "docker")) {
            return false;
        }
        return check_ip(address, ip);
    });
    return address;
}

int main() {
    auto NCIList = getNICList();
    for (auto& i : NCIList) {
        cout << i.second << " : " << i.first << endl;
    }
    cout << get_local_ip() << endl;
    return 0;
}