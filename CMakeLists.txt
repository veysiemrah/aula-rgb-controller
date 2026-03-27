cmake_minimum_required(VERSION 3.16)
project(F87Control
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_CLI "Build the f87ctl command-line tool" ON)
option(BUILD_GUI "Build the f87control GTK4 GUI application" OFF)

add_subdirectory(lib)

if(BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_GUI)
    add_subdirectory(gui)
endif()
