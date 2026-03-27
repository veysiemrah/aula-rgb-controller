find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_library(f87 SHARED
    src/device.c
    src/protocol.c
    src/lighting.c
    src/effects.c
)

target_include_directories(f87
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(f87
    PRIVATE
        ${LIBUSB_LIBRARIES}
)

target_compile_options(f87
    PRIVATE
        ${LIBUSB_CFLAGS_OTHER}
)

set_target_properties(f87 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
