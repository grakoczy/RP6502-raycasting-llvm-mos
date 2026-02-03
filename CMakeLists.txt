cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)
set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)
project(MY-RP6502-PROJECT)

add_executable(raycast)

rp6502_asset(raycast 0x10000 textures/background-320x180.bin)
rp6502_asset(raycast 0x1E100 textures/textures.bin)
rp6502_asset(raycast 0x1E500 textures/sprites.bin)
rp6502_asset(raycast 0x1F100 textures/needle-32x32.bin)


rp6502_executable(raycast
    background-320x180.bin.rp6502
    textures.bin.rp6502
    sprites.bin.rp6502
    needle-32x32.bin.rp6502
)

target_sources(raycast PRIVATE
    src/colors.c
    src/bitmap_graphics.cpp
    src/raycaster_quadrant.cpp
    # src/raycaster.cpp
)
