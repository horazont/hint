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
    "src/hintd.c"
    "src/queue.c"
    "src/comm.c"
    "src/broker.c"
    "src/screen.c"
    "src/timestamp.c"
    "src/array.c"
    "src/lpcdisplay.c"
    "src/xmppintf.c"
    "src/departure.c"
    "src/utils.c")

set(STATIC_LIBS
    ${CMAKE_BINARY_DIR}/libcouplet/src/libcouplet/.libs/libcouplet.a)

include_directories("../")
include_directories(".")
include_directories(${CMAKE_BINARY_DIR}/libcouplet/src/libcouplet/include)

add_executable(hintd
    ${SOURCES})
add_dependencies(hintd libcouplet)
target_link_libraries(hintd pthread ${STATIC_LIBS} ssl resolv expat)
