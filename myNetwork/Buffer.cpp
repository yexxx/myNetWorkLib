#include "Buffer.hpp"

#include <assert.h>
#include <string.h>

#include "Network/Buffer.h"
#include "Util/logger.h"

namespace myNet {

BufferString::BufferString(std::string data, size_t offset, size_t len)
    : _data(std::move(data)) {
    assert(offset + len <= _data.size());
    if (!len) len = _data.size() - offset;
    _size = len;
    _offset = offset;
}

void BufferRaw::setCapacity(size_t capacity) {
    if (_data) {
        if (capacity > _capacity) {
            delete[] _data;
        } else if (_capacity < 2 * 1024 || _capacity < 2 * capacity) {  // 小于两字节或请求内存大于当前内存的一半，不用重新分配内存
            return;
        }
    }

    _data = new char[capacity];
    _capacity = capacity;
}

void BufferRaw::setSize(size_t size) {
    if (size > _capacity) {
        throw std::invalid_argument("Buffer::setSize out of range");
    }
    _size = size;
}

void BufferRaw::assign(const char* data, size_t size) {
    if (size <= 0) size = strlen(data);
    setCapacity(size + 1);
    memcpy(_data, data, size);
    _data[size] = 0;
    setSize(size);
}

#define INET4LEN 16U
#define INET6LEN 28U

BufferSock::BufferSock(Buffer::Ptr buf, sockaddr* addr, int addrLen) {
    if (addr) {
        _addrLen = addrLen ? addrLen : (addr->sa_family == AF_INET ? INET4LEN : INET6LEN);
        memcpy(&_addr, addr, _addrLen);
    }
    assert(buf);
    _buffer = std::move(buf);
};

void BufferCallback::sendCompleted(bool flag) {
    if (_sendResultCB) {
        while (!_bufList.empty()) {
            _sendResultCB(_bufList.front(), flag);
            _bufList.pop_front();
        }
    } else {
        _bufList.clear();
    }
}

void BufferCallback::sendFrontSuccess() {
    if (_sendResultCB) { _sendResultCB(_bufList.front(), true); }
    _bufList.pop_front();
}

BufferSendMsg::BufferSendMsg(std::list<Buffer::Ptr> bufList, onSendResultCB sendResultCB)
    : BufferCallback(std::move(bufList), std::move(sendResultCB)), _iovec(_bufList.size()) {
    size_t i = 0;
    for (auto& buf : _bufList) {
        _iovec[i].iov_base = buf->data();
        _iovec[i].iov_len = buf->size();
        _remainSize += _iovec[i].iov_len;
        ++i;
    }
}

ssize_t BufferSendMsg::send(int fd, int flags) {
    auto beginRemainSize = _remainSize;
    ssize_t sendingSize = 0;
    while (_remainSize && sendingSize != -1) {
        do {
            msghdr msg;
            msg.msg_name = nullptr;
            msg.msg_namelen = 0;
            msg.msg_iov = &(_iovec[_iovecOffset]);
            msg.msg_iovlen = std::min(count(), (size_t)IOV_MAX);
            msg.msg_control = nullptr;
            msg.msg_controllen = 0;
            msg.msg_flags = flags;
            sendingSize = sendmsg(fd, &msg, flags);
        } while (-1 == sendingSize && 4 == errno);  // 4: 用户打断

        if (sendingSize >= (ssize_t)_remainSize) {
            _remainSize = 0;
            sendCompleted(true);
            break;
        }

        if (sendingSize > 0) {
            reOffset(sendingSize);
            break;
        }
    }

    return beginRemainSize - _remainSize > 0 ? beginRemainSize - _remainSize : -1;
}

void BufferSendMsg::reOffset(size_t n) {
    _remainSize -= n;
    size_t offset = 0;
    for (auto i = _iovecOffset; i <= _iovec.size(); ++i) {
        offset += _iovec[i].iov_len;
        if (offset < n) {
            sendFrontSuccess();
            continue;
        }

        _iovecOffset = i;
        if (offset - n == 0) {
            _iovecOffset += 1;
            sendFrontSuccess();
        } else {
            _iovec[i].iov_base = (char*)_iovec[i].iov_base + _iovec[i].iov_len - (offset - n);
            _iovec[i].iov_len = offset - n;
        }
        break;
    }
}

BufferList::Ptr BufferList::create(std::list<Buffer::Ptr> bufList, onSendResultCB sendResultCB, bool isUdp) {
    return std::make_shared<BufferSendMsg>(std::move(bufList), std::move(sendResultCB));
}
}  // namespace myNet