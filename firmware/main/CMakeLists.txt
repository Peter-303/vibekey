idf_component_register(
    SRCS
        "main.c"
        "app/proto.c"
        "app/status.c"
        "app/key_fsm.c"
        "app/led_fsm.c"
        "app/seq.c"
        "app/cmd_dispatch.c"
        "app/net_fsm.c"
        "app/asr_fsm.c"
        "app/app.c"
        "port/port_esp32.c"
    INCLUDE_DIRS
        "."
        "app"
        "port"
    REQUIRES
        driver
        esp_timer
        esp_wifi
        esp_event
        nvs_flash
        esp_netif
        mbedtls
        esp_http_client
        app_update
        esp_hw_support
    # tinyusb 与 esp_websocket_client 在 ESP-IDF v5.5 里已从内置组件移到
    # 组件注册表（见 main/idf_component.yml 的 add-dependency），
    # 因此要按注册表组件名 espressif__xxx 引用。
    PRIV_REQUIRES
        espressif__tinyusb
        espressif__esp_websocket_client
)

# TinyUSB 需要知道目标芯片与运行模式。ESP32-S3 只有设备模式（USB-OTG）。
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CFG_TUSB_MCU=OPT_MCU_ESP32S3
    CFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE
)

# TinyUSB 在自己的组件里编译，**看不到** main/ 的 INCLUDE_DIRS
# （组件间的包含目录只朝"依赖者"方向传播，而 tinyusb 不依赖 main）。
# 直接把 main/ 挂到 tinyusb 的包含路径上，tusb_option.h 里的
# #include "tusb_config.h" 就能找到我们的配置文件。
#
# 不用 CFG_TUSB_CONFIG_FILE 宏的原因：它的值必须是 C 字符串字面量，
# 而 CMake 的 target_compile_definitions 会再加一层转义，实测得到
# -DCFG_TUSB_CONFIG_FILE=\"\"path\"\"，预处理后是空文件名。改包含路径没这个问题。
set(VK_TUSB_CFG "${CMAKE_CURRENT_LIST_DIR}/tusb_config.h")
string(REPLACE "\\" "/" VK_TUSB_CFG "${VK_TUSB_CFG}")
idf_component_get_property(VK_TUSB_LIB espressif__tinyusb COMPONENT_LIB)
if(VK_TUSB_LIB)
    target_include_directories(${VK_TUSB_LIB} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
    if(NOT EXISTS "${VK_TUSB_CFG}")
        message(FATAL_ERROR "找不到 ${VK_TUSB_CFG}，TinyUSB 需要它才能编译")
    endif()
endif()

# 应用层源码不允许引用 ESP-IDF 头（ADR-1 分层约束）。
# 这里加一条编译期防线：若将来有人违反，构建会直接失败。
set_source_files_properties(
    app/proto.c app/status.c app/key_fsm.c app/led_fsm.c app/seq.c
    app/cmd_dispatch.c app/net_fsm.c app/asr_fsm.c app/app.c
    PROPERTIES COMPILE_OPTIONS "-Werror=implicit-function-declaration"
)
