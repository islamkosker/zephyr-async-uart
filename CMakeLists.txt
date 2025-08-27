# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr-async-uart)


add_subdirectory(app/main)
add_subdirectory(app/peripherals)
add_subdirectory(app/utils)