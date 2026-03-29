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

find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE libpulse-simple)
if(PULSE_FOUND)
    add_executable(test_spectrum tests/test_spectrum.c lib/src/spectrum.c)
    target_include_directories(test_spectrum PRIVATE lib/src lib/include vendor/kissfft)
    target_link_libraries(test_spectrum kissfft m)
    target_compile_definitions(test_spectrum PRIVATE F87_HAS_AUDIO=1)
    add_test(NAME spectrum_tests COMMAND test_spectrum)
endif()

add_executable(test_sensor tests/test_sensor.c lib/src/sensor.c)
target_include_directories(test_sensor PRIVATE lib/src lib/include)
target_link_libraries(test_sensor Threads::Threads)
add_test(NAME sensor_tests COMMAND test_sensor)

pkg_check_modules(SYSTEMD libsystemd)
if(SYSTEMD_FOUND)
    add_executable(test_daemon tests/test_daemon.c)
    target_link_libraries(test_daemon PRIVATE f87)
    add_test(NAME daemon_tests COMMAND test_daemon)
endif()

option(BUILD_CLI "Build the f87ctl command-line tool" ON)
option(BUILD_GUI "Build the f87control GTK4 GUI application" OFF)
option(BUILD_DAEMON "Build the f87d D-Bus daemon" ON)

add_subdirectory(vendor/kissfft)
add_subdirectory(lib)

if(BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_GUI)
    add_subdirectory(gui)
endif()

if(BUILD_DAEMON)
    add_subdirectory(daemon)
endif()
