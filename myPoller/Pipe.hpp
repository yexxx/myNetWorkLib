#ifndef Pipe_hpp
#define Pipe_hpp

#include <unistd.h>

#include "../myNetwork/SocketUtil.hpp"
#include "../myNetwork/uv_errno.hpp"

namespace myNet {

class Pipe {
  public:
    Pipe() {
        if (-1 == pipe(_pipeFd)) {
            throw std::runtime_error("Create posix pipe failed: " + std::string(uv_strerror(uv_translate_posix_error(errno))));
        }

        SocketUtil::setNoBlocked(_pipeFd[0]);
        SocketUtil::setNoBlocked(_pipeFd[1]);
        SocketUtil::setCloExec(_pipeFd[0]);
        SocketUtil::setCloExec(_pipeFd[1]);
    }

    ~Pipe() {
        for (auto& pipeFd : _pipeFd) {
            if (-1 != pipeFd) {
                close(pipeFd);
            }
            pipeFd = -1;
        }
    }

    int write(const void* buf, int n) {
        int ret{-1};
        do {
            ret = ::write(_pipeFd[1], buf, n);
        } while (-1 == ret && UV_EINTR == uv_translate_posix_error(errno));
        return ret;
    }

    int read(void* buf, int n) {
        int ret{-1};
        do {
            ret = ::read(_pipeFd[0], buf, n);
        } while (-1 == ret && UV_EINTR == uv_translate_posix_error(errno));
        return ret;
    }

    int writeFd() const {
        return _pipeFd[1];
    };

    int readFd() const {
        return _pipeFd[0];
    };

  private:
    // 0: recv, 1: send
    int _pipeFd[2]{-1, -1};
};

} // namespace myNet

#endif // Pipe_hpp