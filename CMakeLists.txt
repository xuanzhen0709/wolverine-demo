cmake_minimum_required(VERSION 3.9.0)

include(cmake/version.cmake)
project(wolverine-demo VERSION ${PROJECT_VERSION} LANGUAGES C CXX)

find_package(wolverine 2.0.0...100.100.100 REQUIRED CONFIG)
find_package(cfi_operators REQUIRED CONFIG)

message(${Python3_NumPy_INCLUDE_DIRS})

add_subdirectory(demos)
add_subdirectory(tools)
