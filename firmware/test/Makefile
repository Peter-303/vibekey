# VibeKey 固件应用层测试
#
# 目的：在没有任何硬件、没有 ESP-IDF 的机器上验证 app/ 下的全部逻辑。
# 依据：docs/00-技术设计文档.md §11.1（ADR-1）
#
# 用法：
#   make           构建
#   make run       构建并运行
#   make clean
#
# 或不用 make，直接：
#   cc -std=c99 -Wall -Wextra -I../main/app -o run_tests \
#      ../main/app/proto.c ../main/app/status.c ../main/app/key_fsm.c \
#      ../main/app/led_fsm.c ../main/app/seq.c \
#      test_proto.c test_status.c test_key_fsm.c test_led_fsm.c \
#      test_seq.c test_main.c stub_port.c && ./run_tests

# 编译器：默认 gcc（Windows 上 WinLibs/MSYS2 只提供 gcc，不提供 cc）。
# make 内置默认值是 cc，用 origin 判断才能在保留 `make CC=clang` 能力的同时改掉默认值。
ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O1 -g
APP_DIR := ../main/app
BUILD   := build
TARGET  := run_tests

APP_SRCS := $(APP_DIR)/proto.c $(APP_DIR)/status.c $(APP_DIR)/key_fsm.c \
            $(APP_DIR)/led_fsm.c $(APP_DIR)/seq.c \
            $(APP_DIR)/cmd_dispatch.c $(APP_DIR)/net_fsm.c \
            $(APP_DIR)/asr_fsm.c $(APP_DIR)/app.c
TST_SRCS := test_proto.c test_status.c test_key_fsm.c test_led_fsm.c \
            test_seq.c test_cmd_dispatch.c test_net_fsm.c test_asr_fsm.c \
            test_app.c test_main.c stub_port.c

APP_OBJS := $(addprefix $(BUILD)/,$(notdir $(APP_SRCS:.c=.o)))
TST_OBJS := $(addprefix $(BUILD)/,$(notdir $(TST_SRCS:.c=.o)))

.PHONY: all run clean
all: $(TARGET)

$(TARGET): $(APP_OBJS) $(TST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS) $(TST_OBJS)

# app/ 下的源码
$(BUILD)/%.o: $(APP_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(APP_DIR) -c -o $@ $<

# test/ 下的源码
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(APP_DIR) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET)
