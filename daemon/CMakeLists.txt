find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)
pkg_check_modules(JSON_C REQUIRED json-c)

add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
    src/dbus_interface.c
    src/idle_monitor.c
    src/profile_manager.c
)

target_include_directories(f87d PRIVATE
    ${SYSTEMD_INCLUDE_DIRS}
    ${JSON_C_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_link_libraries(f87d PRIVATE
    f87
    ${SYSTEMD_LIBRARIES}
    ${JSON_C_LIBRARIES}
)

target_compile_options(f87d PRIVATE
    ${SYSTEMD_CFLAGS_OTHER}
)

install(TARGETS f87d DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/dbus/org.f87.Control.service
        DESTINATION share/dbus-1/services)
install(FILES ${CMAKE_SOURCE_DIR}/systemd/f87d.service
        DESTINATION lib/systemd/user)
