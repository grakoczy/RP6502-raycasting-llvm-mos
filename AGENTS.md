# AGENTS.md - Context for AI Agents

## Project Overview
This project is a Raycaster implementation for the RP6502 platform (Picocomputer), using `llvm-mos` for compilation.

## Technical Stack
- **Language**: C++ (with C linkage for system calls)
- **Compiler**: llvm-mos (mos-clang)
- **Target Platform**: RP6502
- **Build System**: CMake

## Key Directories
- `src/`: Source code (main logic, graphics).
- `textures/`: Texture assets and Python conversion scripts.
- `tools/`: Build tools and RP6502 upload utility.
- `build/`: CMake build output.

## Guidelines
1. **Performance**: The 6502 is an 8-bit processor. Avoid floating point arithmetic, heavy division, or multiplication where possible. Use fixed-point arithmetic (`FpF.hpp`).
2. **Memory**: Manage zero-page usage carefully (`.zp.bss` sections).
3. **Assets**: Textures and sprites are converted to binary format and loaded into XRAM.
4. **Build**: Use the provided CMake tasks or `cmake` commands in the terminal.

## Current Tasks
- [ ] Implement/Refine raycasting algorithm.
- [ ] Optimize rendering loop.
- [ ] Manage asset loading.
