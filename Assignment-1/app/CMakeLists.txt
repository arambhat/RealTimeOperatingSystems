cmake_minimum_required(VERSION 3.20.0)

set(BOARD mimxrt1050_evk)
set(BOARD_FLASH_RUNNER jlink)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ashishapp)
target_sources(app PRIVATE src/main.c src/task_model.h)
target_compile_options(app PRIVATE -Wall)