cmake_minimum_required(VERSION 3.28.3)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(PosixSocketsProject CXX)
add_executable(main
    src/main.cpp
    src/client.cpp
    src/server.cpp
)
