cmake_minimum_required(VERSION 3.18)
add_subdirectory(tools)
set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)
project(MY-RP6502-PROJECT)

add_executable(raycast)
# rp6502_executable(raycast)

rp6502_asset(raycast 0x10000 textures/pixel-320x180.bin)
rp6502_executable(raycast
    pixel-320x180.bin.rp6502
)

target_sources(raycast PRIVATE
    src/colors.c
    src/bitmap_graphics.cpp
    src/raycaster.cpp
)
