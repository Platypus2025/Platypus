cmake_minimum_required(VERSION 3.13.4)

set(LLVM_ROOT "$ENV{LLVM_ROOT}")

if("${LLVM_ROOT}" STREQUAL "")
  message(FATAL_ERROR "LLVM_ROOT is not set")
endif()

set(CMAKE_CXX_COMPILER "${LLVM_ROOT}/build/bin/clang++")
set(CMAKE_C_COMPILER "${LLVM_ROOT}/build/bin/clang-20")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(FindDynSym)


set(LLVM_DIR "${LLVM_ROOT}/build/lib/cmake/llvm")
find_package(LLVM 20.1.0 REQUIRED CONFIG)

message(STATUS "Using LLVMConfig.cmake from: ${LLVM_DIR}")
message(STATUS "LLVM package version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIR})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

add_library(Dynsym SHARED FindDynSym.cpp Helper_Functions.cpp)

# Link against LLVM libraries
target_link_libraries(Dynsym ${llvm_libs})
