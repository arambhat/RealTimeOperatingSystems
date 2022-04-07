cmake_minimum_required(VERSION 3.13.1)

set(BOARD mimxrt1050_evk)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hc_sr04)


FILE(GLOB app_sources src/*.c)
target_include_directories(app PRIVATE 
    ${ZEPHYR_BASE}/subsys/net/ip include)
target_sources(app PRIVATE ${app_sources})
