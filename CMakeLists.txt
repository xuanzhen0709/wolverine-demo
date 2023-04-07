cmake_minimum_required(VERSION 3.9.0)

project(wolverine-demo VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS_DEBUG "-g -mavx")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mavx")

if(CMAKE_BUILD_TYPE MATCHES "debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

find_package(wolverine REQUIRED CONFIG)

add_subdirectory(src)