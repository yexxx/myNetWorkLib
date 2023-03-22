cmake_minimum_required(VERSION 3.15)
project(myNetwork)

# cpp11
set(CMAKE_CXX_STANDARD 17)

# 查找目录下的所有源文件, 并将名称保存到 DIR_LIB_SRCS 变量
aux_source_directory("myNetwork" DIR_LIB_SRCS)
aux_source_directory("myThread" DIR_LIB_SRCS)
aux_source_directory("myPoller" DIR_LIB_SRCS)
# 生成链接库
add_library(myNetWork ${DIR_LIB_SRCS})

# 子目录
add_subdirectory(tests)