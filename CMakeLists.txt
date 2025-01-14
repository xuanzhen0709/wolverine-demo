cmake_minimum_required(VERSION 3.9.0)

project(wolverine-demo VERSION 0.1 LANGUAGES C CXX)

find_package(wolverine REQUIRED CONFIG)
find_package(cfi_operators REQUIRED CONFIG)

message(${Python3_NumPy_INCLUDE_DIRS})

add_subdirectory(demos)
add_subdirectory(tools)
