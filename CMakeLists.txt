cmake_minimum_required(VERSION 3.5...3.31)
project(nasmon C)

set(CMAKE_C_STANDARD 11)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

# Default flags and libs
set(CMAKE_C_FLAGS "-march=native -Wall -pipe -fPIC -fmessage-length=0")
link_libraries("-lsensors")

# Compiler configuration
set(CMAKE_C_FLAGS_DEBUG "-g -O1")
#set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto=8 -floop-interchange -ftree-loop-distribution -floop-strip-mine -floop-block -ftree-vectorize -fuse-ld=gold")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-Wl,--as-needed")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,--as-needed -fuse-linker-plugin -s")

add_executable(nasmon utils.c lcd.c fan.c sensor.c smart.c sysload.c netif.c cpu.c nasmon.c sts_srv.c)
