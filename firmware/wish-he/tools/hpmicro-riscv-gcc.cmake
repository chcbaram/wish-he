
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)


set(HPM_RISCV_POSSIBLE_PATHS
    "C:/work/tools/baram-fw-tools/risc_toolchain/risc_gcc/test"
    ENV HPM_RISCV_TOOLCHAIN_DIR
)

set(MAKE_POSSIBLE_PATHS
    "c:/MinGW-32/bin"
    ENV MAKE_DIR
)

# xpack(riscv-none-elf-)과 HPMicro 공식(riscv32-unknown-elf-) 툴체인을 모두 지원한다.
# 찾은 실행 파일 이름에서 접두어를 역산하므로 툴체인을 바꿔도 이 파일은 그대로 둔다.
#
find_program(HPM_RISCV_TOOLCHAIN_DIR
    NAMES riscv-none-elf-gcc.exe        riscv-none-elf-gcc
          riscv32-unknown-elf-gcc.exe   riscv32-unknown-elf-gcc
    HINTS ${HPM_RISCV_POSSIBLE_PATHS}
    PATH_SUFFIXES bin
    DOC "HPMicro RISCV GCC Toolchain Directory"
)

if(NOT HPM_RISCV_TOOLCHAIN_DIR)
    message(FATAL_ERROR "RISCV Toolchain not found. Please set HPM_RISCV_TOOLCHAIN_DIR environment variable")
endif()



find_program(CMAKE_MAKE_PROGRAM
  NAMES make
        make.exe
  DOC "Find a suitable make program for building under Windows/MinGW"
  HINTS ${MAKE_POSSIBLE_PATHS}
)

if(NOT CMAKE_MAKE_PROGRAM)
    message(FATAL_ERROR "Make program not found. Please set MINGW_DIR environment variable")
else()
    message(STATUS "Found Make program: ${CMAKE_MAKE_PROGRAM}")
endif()


# HPM_RISCV_TOOLCHAIN_DIR에서 실행 파일 이름을 제거하고 경로와 접두어를 추출
#   .../bin/riscv-none-elf-gcc  ->  TOOLCHAIN_PATH=.../bin, TOOLCHAIN_PREFIX=.../bin/riscv-none-elf-
#
get_filename_component(TOOLCHAIN_PATH "${HPM_RISCV_TOOLCHAIN_DIR}" DIRECTORY)
get_filename_component(TOOLCHAIN_EXE  "${HPM_RISCV_TOOLCHAIN_DIR}" NAME_WE)
string(REGEX REPLACE "gcc$" "" TOOLCHAIN_TRIPLET "${TOOLCHAIN_EXE}")
set(TOOLCHAIN_PREFIX "${TOOLCHAIN_PATH}/${TOOLCHAIN_TRIPLET}")

message(STATUS "Found RISCV Toolchain: ${TOOLCHAIN_PREFIX}gcc")



set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if (WIN32)
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe" CACHE FILEPATH "C Compiler path")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++.exe" CACHE FILEPATH "C++ Compiler path")
else()
set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc" CACHE FILEPATH "C Compiler path")
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++" CACHE FILEPATH "C++ Compiler path")
endif()

set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy CACHE INTERNAL "objcopy tool")
set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump CACHE INTERNAL "objdump tool")
set(CMAKE_SIZE_UTIL ${TOOLCHAIN_PREFIX}size CACHE INTERNAL "size tool")

set(CMAKE_C_STANDARD    11)
set(CMAKE_CXX_STANDARD  17)

# Disable compiler checks.
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH ${BINUTILS_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
