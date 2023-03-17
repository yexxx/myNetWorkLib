# myNetwork

参考[ZLToolKit](https://github.com/ZLMediaKit/ZLToolKit)实现的网络库

安装测试：

- 本项目未完成的部分依赖[ZLToolKit](https://github.com/ZLMediaKit/ZLToolKit)，需编译安装该库
- mkdir build && cd build
- 运行test里面的文件

## 待解决错误

暂无

## 已解决错误

1. TCPClient connect 错误的问题:
   这个错误是因为之前Socket.cpp:151, Socket.cpp:170 都使用了一次makeSocketFD 导致前后使用的不是同一个socketFD导致的
2. TCPServer:10, TCPServer:75 poller 不在本线程的问题
   在TCPServer 初始化时未设置 _poller导致
