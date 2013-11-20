cmake_minimum_required(VERSION 2.6)

set(SHARED_FLAGS "-g -Wall -Wextra -Werror -Wno-unused-parameter")

set(CMAKE_C_FLAGS "-std=gnu99 ${SHARED_FLAGS}")

include(ExternalProject)

ExternalProject_Add(libcouplet
    PREFIX libcouplet
    GIT_REPOSITORY https://github.com/pasis/libcouplet
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./bootstrap.sh && ./configure
    BUILD_COMMAND make
    INSTALL_COMMAND echo)

set(SOURCES
    "hintd.c"
    "queue.c"
    "comm.c"
    "lpc1114.c"
    "broker.c")

set(STATIC_LIBS
    "libcouplet/src/libcouplet/.libs/libcouplet.a")

include_directories("../")
include_directories(".")

add_executable(hintd
    ${SOURCES})
add_dependencies(hintd libcouplet)
target_link_libraries(hintd pthread)
