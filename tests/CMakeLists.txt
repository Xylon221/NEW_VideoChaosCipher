# 优先使用系统安装的 GTest，未安装时自动从 GitHub 下载
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
include(GoogleTest)

# 公共头文件搜索路径
set(TEST_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/include
)

# ---- test_encryptor ----
add_executable(test_encryptor
    test_encryptor.cpp
    ${PROJECT_SOURCE_DIR}/src/encryptor.cpp
)
target_include_directories(test_encryptor PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_encryptor PRIVATE
    ${OpenCV_LIBS}
    GTest::gtest_main
)
gtest_discover_tests(test_encryptor)

# ---- test_safequeue ----
add_executable(test_safequeue
    test_safequeue.cpp
)
target_include_directories(test_safequeue PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_safequeue PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(test_safequeue)

# ---- test_analyzer ----
add_executable(test_analyzer
    test_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/src/analyzer.cpp
    ${PROJECT_SOURCE_DIR}/src/encryptor.cpp
)
target_include_directories(test_analyzer PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_analyzer PRIVATE
    ${OpenCV_LIBS}
    GTest::gtest_main
)
gtest_discover_tests(test_analyzer)
