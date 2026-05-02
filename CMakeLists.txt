cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

project(sickboy)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SICKBOY_SOURCES "src/*.cpp")
file(GLOB_RECURSE SICKBOY_HEADERS "include/*.h")

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)

add_executable(sickboy ${SICKBOY_HEADERS} ${SICKBOY_SOURCES})
target_include_directories(sickboy PRIVATE "include")
if (MSVC)
target_compile_options(sickboy PRIVATE "/W4" "/WX")
else()
target_compile_options(sickboy PRIVATE "-Wall" "-Wextra" "-Wpedantic" "-Werror")
endif()
target_link_libraries(sickboy glfw glad::glad OpenAL::OpenAL)