# Cross-compile toolchain file for the Datafrog SF3500 (MIPS32r2, little endian).
# Set SF3500_TC to the toolchain's "host" directory (it contains
# bin/mips-mti-linux-gnu-gcc and mipsel-buildroot-linux-gnu/sysroot).
if(NOT DEFINED SF3500_TC)
    set(SF3500_TC "$ENV{SF3500_TC}")
endif()
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)
set(CMAKE_SYSROOT ${SF3500_TC}/mipsel-buildroot-linux-gnu/sysroot)
set(CMAKE_C_COMPILER ${SF3500_TC}/bin/mips-mti-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${SF3500_TC}/bin/mips-mti-linux-gnu-g++)
set(CMAKE_C_FLAGS_INIT "-mips32r2 -mtune=74kc -mhard-float")
set(CMAKE_CXX_FLAGS_INIT "-mips32r2 -mtune=74kc -mhard-float")
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
