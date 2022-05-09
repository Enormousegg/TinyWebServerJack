cmake_minimum_required(VERSION 3.0.0)
project(WebServer VERSION 0.1.0)

include(CTest)
enable_testing()

add_library(main main.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)



# 声明要求的 cmake 最低版本
cmake_minimum_required( VERSION 2.8 )

# 声明一个 cmake 工程
project(test_cmake)

# 设置编译模式
set( CMAKE_BUILD_TYPE "Debug" )
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

# 添加一个可执行程序
# 语法：add_executable( 程序名 源代码文件 ）
add_executable( main src/main.cpp )
#将可执行文件输出到文件夹bin中
set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/bin)

# 包含头文件所在目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加一个库
add_library( b src/b.cpp )
# 共享库
add_library( b_shared SHARED src/b.cpp )

# 将库文件链接到可执行程序上
target_link_libraries( main b_shared )