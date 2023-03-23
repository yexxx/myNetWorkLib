# myNetwork

参考[ZLToolKit](https://github.com/ZLMediaKit/ZLToolKit)实现的网络库

安装测试：

- 本项目未完成的部分依赖[ZLToolKit](https://github.com/ZLMediaKit/ZLToolKit)，需编译安装该库
- mkdir build && cd build
- 运行test里面的文件

## 待完成内容

将会完成

   1. TaskExcutor.hpp:13 ：时间管理库
   2. 日志库（ZLToolKit 中的WarnL, DebugL 等）
   3. 全面整理代码

## 待解决错误

1. Buffer无法处理中文字符

## 已解决错误

1. TCPClient connect 错误的问题:
   这个错误是因为之前Socket.cpp:151, Socket.cpp:170 都使用了一次makeSocketFD 导致前后使用的不是同一个socketFD导致的
2. TCPServer:10, TCPServer:75 poller 不在本线程的问题
   在TCPServer 初始化时未设置 _poller导致
3. UDPServer.cpp:216
   - 利用RAII特性使用`std::unique_ptr<std::nullptr_t, std::function<void(void*)>> deleter()`的自定义析构器时必须要new 一个变量，不然无法正确触发析构
   - 使用`shared_ptr<void>` 时可使用nullptr，不存在上述问题
4. 将构造设为隐私方法时的单例问题：
   - 正确写法
  
      ```cpp
      static WorkThreadPool& Instance() {
         static std::shared_ptr<WorkThreadPool> sharedRet(new WorkThreadPool());
         static auto& ret = *sharedRet;
         return ret;
      }
      ```

   - 错误写法：

      ```cpp
      static WorkThreadPool& Instance() {
         static auto sharedRet =   std::make_shared<WorkThreadPool>();
         static auto& ret = *sharedRet;
         return ret;
      }
      ```
