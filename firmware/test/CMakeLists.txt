# VibeKey 固件应用层测试（CMake 版，供已装 CMake 的机器使用）
#
# 目的与 Makefile 版相同：不需要 ESP-IDF、不需要硬件即可验证 app/ 逻辑。
# 用法：
#   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
#   或直接运行 build/run_tests

cmake_minimum_required(VERSION 3.16)
project(vibekey_app_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  message(WARNING "推荐使用 GCC 或 Clang 构建测试；当前编译器: ${CMAKE_C_COMPILER_ID}")
endif()

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main/app)

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(run_tests
    ${APP_DIR}/proto.c
    ${APP_DIR}/status.c
    ${APP_DIR}/key_fsm.c
    ${APP_DIR}/led_fsm.c
    ${APP_DIR}/seq.c
    ${APP_DIR}/cmd_dispatch.c
    ${APP_DIR}/net_fsm.c
    ${APP_DIR}/asr_fsm.c
    ${APP_DIR}/app.c
    test_proto.c
    test_status.c
    test_key_fsm.c
    test_led_fsm.c
    test_seq.c
    test_cmd_dispatch.c
    test_net_fsm.c
    test_asr_fsm.c
    test_app.c
    test_main.c
    stub_port.c
)

target_include_directories(run_tests PRIVATE ${APP_DIR})

enable_testing()
add_test(NAME app_tests COMMAND run_tests)
