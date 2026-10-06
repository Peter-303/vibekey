# VibeKey 固件项目（ESP-IDF）
#
# 用法：
#   idf.py set-target esp32s3
#   idf.py menuconfig      # 可选：改 AP 密码 / TCP 端口等
#   idf.py build
#   idf.py -p COMx flash monitor
#
# 注意：main/port/port_board.h 的引脚目前是占位值，上板前必须先填真实分配表
#       （见 docs/01-引脚分配表.md）。

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vibekey)
