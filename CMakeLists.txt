cmake_minimum_required(VERSION 3.9.0)

project(wolverine-demo VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "-Wall -mavx")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb3 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_INSTALL_PREFIX $ENV{HOME}/.local)

find_package(wolverine REQUIRED CONFIG)

add_subdirectory(src)