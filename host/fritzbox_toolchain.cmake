SET(CMAKE_SYSTEM_NAME Linux)
include(CMakeForceCompiler)

CMAKE_FORCE_C_COMPILER(mips-linux-uclibc-gcc GNU)

SET(CMAKE_FIND_ROOT_PATH  /home/horazont/Builds/freetz-trunk/toolchain/build/mips_gcc-4.8.4_uClibc-0.9.33.2-nptl/mips-linux-uclibc/include)
#~ include_directories("")

# search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(PLATFORM "mips-linux-uclibc")
