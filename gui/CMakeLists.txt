include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ADW REQUIRED libadwaita-1)

# Compile GResource
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
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
    src/i18n.c
    src/effect_meta.c
    src/window.c
    src/sidebar.c
    src/app_state.c
    src/controls.c
    src/keyboard_view.c
    src/preview.c
    src/sensor_editor.c
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
    m
)

target_compile_options(f87control PRIVATE
    ${GTK4_CFLAGS_OTHER}
    ${ADW_CFLAGS_OTHER}
)

target_compile_definitions(f87control PRIVATE
    LOCALEDIR="${CMAKE_INSTALL_PREFIX}/share/locale"
)

install(TARGETS f87control DESTINATION ${CMAKE_INSTALL_BINDIR})

# Gettext .po -> .mo compilation
find_program(MSGFMT msgfmt)
if(MSGFMT)
    set(LANGUAGES tr)
    foreach(LANG ${LANGUAGES})
        set(PO_FILE ${CMAKE_SOURCE_DIR}/po/${LANG}.po)
        set(MO_DIR ${CMAKE_BINARY_DIR}/locale/${LANG}/LC_MESSAGES)
        set(MO_FILE ${MO_DIR}/f87control.mo)
        file(MAKE_DIRECTORY ${MO_DIR})
        add_custom_command(
            OUTPUT ${MO_FILE}
            COMMAND ${MSGFMT} -o ${MO_FILE} ${PO_FILE}
            DEPENDS ${PO_FILE}
            COMMENT "Compiling ${LANG} translation"
        )
        list(APPEND MO_FILES ${MO_FILE})
        install(FILES ${MO_FILE}
                DESTINATION ${CMAKE_INSTALL_DATADIR}/locale/${LANG}/LC_MESSAGES)
    endforeach()
    add_custom_target(translations ALL DEPENDS ${MO_FILES})
endif()
