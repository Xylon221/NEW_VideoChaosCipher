cmake_minimum_required(VERSION 3.14)
project(VideoChaosCipher CXX)

# 查找 OpenCV 库
find_package(OpenCV REQUIRED)

# 查找 FFmpeg 库（VPU 硬件加速视频 I/O）
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale
)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${FFMPEG_INCLUDE_DIRS})

# 编译选项
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type: Debug or Release" FORCE)
endif()
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 主程序 (VPU 加速版)
add_executable(app
    src/main.cpp
    src/encryptor.cpp
    src/analyzer.cpp
    src/vpu_io.cpp
)

# 单帧加密演示工具
add_executable(test_frame
    tools/encrypt_single_frame.cpp
    src/analyzer.cpp
)

# 链接 OpenCV + FFmpeg
target_link_libraries(app ${OpenCV_LIBS} ${FFMPEG_LIBRARIES})
target_link_libraries(test_frame ${OpenCV_LIBS})

# 安装 (make install 将 app 安装到系统 bin 目录)
install(TARGETS app DESTINATION bin)

# 单元测试 (可选: cmake -DBUILD_TESTS=ON ..)
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
