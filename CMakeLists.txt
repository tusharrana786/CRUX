cmake_minimum_required(VERSION 3.20)

project(
    crux
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(crux
    src/main.cpp
    src/runtime/reacctor/poller/EpollPoler.cpp
)

target_include_directories(crux PRIVATE include)