cmake_minimum_required(VERSION 3.20)

project(sickboy)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SICKBOY_SOURCES "src/*.cpp")
file(GLOB_RECURSE SICKBOY_HEADERS "include/*.h")

add_executable(sickboy ${SICKBOY_HEADERS} ${SICKBOY_SOURCES})
target_include_directories(sickboy PRIVATE "include")
target_compile_options(sickboy PRIVATE "-Wall" "-Wextra" "-Wpedantic" "-Werror")