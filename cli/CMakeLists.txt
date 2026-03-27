find_package(PkgConfig REQUIRED)
pkg_check_modules(JSON_C REQUIRED json-c)

add_executable(f87ctl
    src/main.c
)

target_include_directories(f87ctl
    PRIVATE
        ${JSON_C_INCLUDE_DIRS}
)

target_link_libraries(f87ctl
    PRIVATE
        f87
        ${JSON_C_LIBRARIES}
)

target_compile_options(f87ctl
    PRIVATE
        ${JSON_C_CFLAGS_OTHER}
)
