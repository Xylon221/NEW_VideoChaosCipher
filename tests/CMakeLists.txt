# Google Test 自动下载
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# 公共头文件路径
set(TEST_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
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
