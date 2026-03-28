cmake_minimum_required(VERSION 3.16)
project(F87Control
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
add_executable(test_device tests/test_device.c)
target_link_libraries(test_device PRIVATE f87)
add_test(NAME device_tests COMMAND test_device)

find_package(Threads REQUIRED)
add_executable(test_ring_buffer tests/test_ring_buffer.c lib/src/ring_buffer.c)
target_include_directories(test_ring_buffer PRIVATE lib/src lib/include)
target_link_libraries(test_ring_buffer Threads::Threads)
add_test(NAME ring_buffer_tests COMMAND test_ring_buffer)

option(BUILD_CLI "Build the f87ctl command-line tool" ON)
option(BUILD_GUI "Build the f87control GTK4 GUI application" OFF)

add_subdirectory(vendor/kissfft)
add_subdirectory(lib)

if(BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_GUI)
    add_subdirectory(gui)
endif()
