# GUI build — placeholder, requires GTK4 development libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(JSON_C REQUIRED json-c)

add_executable(f87control
    src/main.c
)

target_include_directories(f87control
    PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${JSON_C_INCLUDE_DIRS}
)

target_link_libraries(f87control
    PRIVATE
        f87
        ${GTK4_LIBRARIES}
        ${JSON_C_LIBRARIES}
)

target_compile_options(f87control
    PRIVATE
        ${GTK4_CFLAGS_OTHER}
        ${JSON_C_CFLAGS_OTHER}
)
