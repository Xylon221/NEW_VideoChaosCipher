cmake_minimum_required(VERSION 3.10)
project(VideoChaosCipher CXX)

# 查找 OpenCV 库
find_package(OpenCV REQUIRED)

add_executable(app src/main.cpp)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)

# 链接 OpenCV
target_link_libraries(app ${OpenCV_LIBS})