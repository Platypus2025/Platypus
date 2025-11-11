cmake_minimum_required(VERSION 3.13.4)
project(LogStructs)

#set(CMAKE_CXX_COMPILER /path/to/llvm-platypus/build/bin/clang++)
#set(CMAKE_C_COMPILER /path/to/llvm-platypus/build/bin/clang-20)
set(CMAKE_CXX_COMPILER /usr/bin/clang++-20)
set(CMAKE_C_COMPILER /usr/bin/clang-20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Uncomment these two lines if you want to pass the LLVM Path
# set(LT_LLVM_INSTALL_DIR "" CACHE PATH "LLVM installation directory")
# list(APPEND CMAKE_PREFIX_PATH "${LT_LLVM_INSTALL_DIR}/lib/cmake/llvm/")

find_package(LLVM 20.1.0 REQUIRED CONFIG)

# Include directories specified by LLVM in the project's include path
include_directories(${LLVM_INCLUDE_DIRS})
# Include definitions specified by LLVM in the project's options
add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIR})

# Use the same C++ standard as LLVM does
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

add_library(Logstructs SHARED LogStructs.cpp Helper_Functions.cpp)

# Link against LLVM libraries
target_link_libraries(Logstructs ${llvm_libs})
