cmake_minimum_required(VERSION 2.6)

set(SHARED_FLAGS "-g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 ${SHARED_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")

include(ExternalProject)

set(COUPLET_CONFIGURE ./configure)

set(LIBS
    ${CMAKE_BINARY_DIR}/libcouplet/src/libcouplet/.libs/libcouplet.a)

if (DEFINED PLATFORM)
if (PLATFORM STREQUAL "mips-linux-uclibc")

set(LIBS ${LIBS}
    ${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libexpat.so
    ${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libc.so.0
    ${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libresolv.so.0
    ${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libssl.so
    ${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libpthread.so.0
    ${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libcrypto.so.1)

set(COUPLET_CONFIGURE
    CC=/home/horazont/Builds/freetz-trunk/toolchain/build/mips_gcc-4.6.3_uClibc-0.9.32.1/mips-linux-uclibc/bin/mips-linux-uclibc-gcc
    expat_CFLAGS=-I${CMAKE_SOURCE_DIR}/fritzbox-static/includes/
    expat_LIBS=${CMAKE_SOURCE_DIR}/fritzbox-static/libs/libexpat.so
    ${COUPLET_CONFIGURE}
    --host ${PLATFORM}
    --enable-static=yes
    --enable-shared=no)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --sysroot=/home/horazont/Builds/freetz-trunk/toolchain/build/mips_gcc-4.6.3_uClibc-0.9.32.1/mips-linux-uclibc/")

else (PLATFORM STREQUAL "mips-linux-uclibc")
message(FATAL_ERROR "unknown platform ${PLATFORM}")
endif (PLATFORM STREQUAL "mips-linux-uclibc")
else (DEFINED PLATFORM)

set(LIBS pthread ${LIBS} ssl resolv expat)
endif (DEFINED PLATFORM)

ExternalProject_Add(libcouplet
    PREFIX libcouplet
    GIT_REPOSITORY https://github.com/pasis/libcouplet
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./bootstrap.sh && ${COUPLET_CONFIGURE}
    BUILD_COMMAND make
    INSTALL_COMMAND echo)

set(SOURCES
    "src/hintd.c"
    "src/queue.c"
    "src/comm.c"
    "src/broker.c"
    "src/screen.c"
    "src/screen_utils.c"
    "src/screen_dept.c"
    "src/screen_weather.c"
    "src/screen_misc.c"
    "src/screen_net.c"
    "src/timestamp.c"
    "src/array.c"
    "src/heap.c"
    "src/lpcdisplay.c"
    "src/xmppintf.c"
    "src/departure.c"
    "src/utils.c")

include_directories("../")
include_directories(".")
include_directories(${CMAKE_BINARY_DIR}/libcouplet/src/libcouplet/include)

add_executable(hintd
    ${SOURCES})
add_dependencies(hintd libcouplet)
target_link_libraries(hintd ${LIBS})
