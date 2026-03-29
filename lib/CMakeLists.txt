find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

# Audio support (optional)
option(BUILD_AUDIO "Build with PulseAudio audio support" ON)
if(BUILD_AUDIO)
    pkg_check_modules(PULSE libpulse-simple)
    if(PULSE_FOUND)
        message(STATUS "PulseAudio found — audio support enabled")
    else()
        message(STATUS "PulseAudio not found — audio support disabled")
    endif()
endif()

add_library(f87 SHARED
    src/device.c
    src/protocol.c
    src/lighting.c
    src/effects.c
    src/animate.c
    src/ring_buffer.c
    src/effects_sw.c
    src/effects_sw_reactive.c
    src/sensor.c
    src/client.c
    src/logger.c
)

target_include_directories(f87
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${LIBUSB_INCLUDE_DIRS}
        ${SYSTEMD_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(f87
    PRIVATE
        ${LIBUSB_LIBRARIES}
        ${SYSTEMD_LIBRARIES}
        Threads::Threads
        m
)

target_compile_options(f87
    PRIVATE
        ${LIBUSB_CFLAGS_OTHER}
)

target_compile_definitions(f87 PRIVATE F87_HAS_JOURNAL=1)

# JSON config support (optional, for sensor profiles)
pkg_check_modules(JSON_C json-c)
if(JSON_C_FOUND)
    target_sources(f87 PRIVATE src/sensor_config.c)
    target_include_directories(f87 PRIVATE ${JSON_C_INCLUDE_DIRS})
    target_link_libraries(f87 PRIVATE ${JSON_C_LIBRARIES})
    target_compile_definitions(f87 PRIVATE F87_HAS_JSON=1)
endif()

if(BUILD_AUDIO AND PULSE_FOUND)
    target_sources(f87 PRIVATE src/audio.c src/spectrum.c src/visualizer.c)
    target_include_directories(f87 PRIVATE ${PULSE_INCLUDE_DIRS})
    target_link_libraries(f87 PRIVATE ${PULSE_LIBRARIES} kissfft m)
    target_compile_definitions(f87 PRIVATE F87_HAS_AUDIO=1)
endif()

set_target_properties(f87 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
