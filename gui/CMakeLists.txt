find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ADW REQUIRED libadwaita-1)

# Compile GResource
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
set(GRESOURCE_XML ${CMAKE_CURRENT_SOURCE_DIR}/resources/f87control.gresource.xml)
set(GRESOURCE_C ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
add_custom_command(
    OUTPUT ${GRESOURCE_C}
    COMMAND ${GLIB_COMPILE_RESOURCES} --target=${GRESOURCE_C} --generate-source
            --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/resources ${GRESOURCE_XML}
    DEPENDS ${GRESOURCE_XML} ${CMAKE_CURRENT_SOURCE_DIR}/resources/style.css
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(f87control
    src/main.c
    src/window.c
    src/sidebar.c
    src/app_state.c
    ${GRESOURCE_C}
)

target_include_directories(f87control PRIVATE
    ${GTK4_INCLUDE_DIRS}
    ${ADW_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(f87control PRIVATE
    f87
    ${GTK4_LIBRARIES}
    ${ADW_LIBRARIES}
)

target_compile_options(f87control PRIVATE
    ${GTK4_CFLAGS_OTHER}
    ${ADW_CFLAGS_OTHER}
)
