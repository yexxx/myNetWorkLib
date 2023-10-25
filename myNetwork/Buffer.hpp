#ifndef Buffer_hpp
#define Buffer_hpp

#include <sys/socket.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "myUtil.hpp"

namespace myNet {

class Buffer : public noncopyable {
  public:
    using Ptr = std::shared_ptr<Buffer>;

    Buffer() = default;
    virtual ~Buffer() = default;

    virtual char* data() const = 0;
    virtual size_t size() const = 0;
    virtual size_t getCapacity() const {
        return size();
    }

    virtual std::string toString() const {
        return std::string(data(), size());
    }
};

class BufferString : public Buffer {
  public:
    using Ptr = std::shared_ptr<BufferString>;

    BufferString(std::string data, size_t offset = 0, size_t len = 0);
    ~BufferString() override = default;

    char* data() const override {
        return const_cast<char*>(_data.data()) + _offset;
    }
    size_t size() const override {
        return _size;
    }

  private:
    std::string _data;
    size_t _size;
    size_t _offset;
};

class BufferRaw : public Buffer {
  public:
    using Ptr = std::shared_ptr<BufferRaw>;

    static Ptr create() {
        return std::make_shared<BufferRaw>();
    };

    ~BufferRaw() override {
        if (_data) {
            delete[] _data;
        }
    }

    char* data() const override {
        return _data;
    }
    size_t size() const override {
        return _size;
    }

    void setCapacity(size_t capacity);
    size_t getCapacity() const override {
        return _capacity;
    }

    virtual void setSize(size_t size);

    void assign(const char* data, size_t size = 0);

  private:
    char* _data{nullptr};
    size_t _size{0};
    size_t _capacity{0};
};

#if !defined(IOV_MAX)
#define IOV_MAX 1024
#endif

class BufferSock : public Buffer {
  public:
    using Ptr = std::shared_ptr<BufferSock>;

    BufferSock(Buffer::Ptr buf, sockaddr* addr = nullptr, int addrLen = 0);
    ~BufferSock() override = default;

    char* data() const override {
        return _buffer->data();
    };
    size_t size() const override {
        return _buffer->size();
    };
    const sockaddr* sockAddr() const {
        return (sockaddr*)&_addr;
    };
    socklen_t sockLen() const {
        return _addrLen;
    };

  private:
    int _addrLen{0};
    sockaddr_storage _addr;
    Buffer::Ptr _buffer;
};

class BufferList : public noncopyable {
  public:
    using Ptr = std::shared_ptr<BufferList>;
    using onSendResultCB = std::function<void(const Buffer::Ptr& Buffer, bool sendSuccess)>;

    // 原代码有针对udp大量数据的优化，暂时未实现
    static Ptr create(std::list<Buffer::Ptr> bufList, onSendResultCB sendResultCB, bool isUdp);

    BufferList() = default;
    virtual ~BufferList() = default;

    virtual bool empty() = 0;
    virtual size_t count() = 0;
    virtual ssize_t send(int fd, int flags) = 0;
};

class BufferCallback {
  public:
    BufferCallback(std::list<Buffer::Ptr> bufList, BufferList::onSendResultCB sendResultCB) : _bufList(std::move(bufList)), _sendResultCB(std::move(sendResultCB)){};

    ~BufferCallback() {
        sendCompleted(false);
    }

    void sendCompleted(bool flag);

    void sendFrontSuccess();

  protected:
    BufferList::onSendResultCB _sendResultCB;
    std::list<Buffer::Ptr> _bufList;
};

class BufferSendMsg : public BufferList, public BufferCallback {
  public:
    BufferSendMsg(std::list<Buffer::Ptr> bufList, onSendResultCB sendResultCB);
    ~BufferSendMsg() override = default;

    bool empty() override {
        return _remainSize == 0;
    }
    size_t count() override {
        return _iovec.size() - _iovecOffset;
    }
    ssize_t send(int fd, int flags) override;

  private:
    void reOffset(size_t n);

    size_t _iovecOffset{0};
    size_t _remainSize{0};
    std::vector<iovec> _iovec;
};

} // namespace myNet

#endif // Buffer_hpp