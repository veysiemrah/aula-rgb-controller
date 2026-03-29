find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
)

target_include_directories(f87d PRIVATE
    ${SYSTEMD_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_link_libraries(f87d PRIVATE
    f87
    ${SYSTEMD_LIBRARIES}
)

target_compile_options(f87d PRIVATE
    ${SYSTEMD_CFLAGS_OTHER}
)
