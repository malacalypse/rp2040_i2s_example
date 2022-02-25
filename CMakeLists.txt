# CMakeLists.txt
#
# Copyright (c) 2022 Daniel Collins
#
# This file is part of rp2040_i2s_example.
#
# rp2040_i2s_example is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License, version 3 as published by the
# Free Software Foundation.
#
# rp2040_i2s_example is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# rp2040_i2s_example. If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/Users/studiodc/Development/raspi/pico/sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(i2s_example C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(i2s_example i2s.c)

pico_generate_pio_header(i2s_example ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

target_sources(i2s_example PRIVATE i2s_example.c)

pico_set_program_name(i2s_example "i2s_test")
pico_set_program_version(i2s_example "0.1")

# no_flash means the target is to run from RAM
# pico_set_binary_type(i2s_test no_flash)

pico_enable_stdio_uart(i2s_example 1)
pico_enable_stdio_usb(i2s_example 0)

# Add the standard library to the build
target_link_libraries(i2s_example pico_stdlib)

# Add any user requested libraries
target_link_libraries(i2s_example
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_clocks
        )

pico_add_extra_outputs(i2s_example)
