# jai_rogue - ASCII roguelike whose entire game logic is JaiScript; C++ is a thin console host.
add_executable(jai_rogue main.cpp)
target_link_libraries(jai_rogue PRIVATE jaiscript)

# The exe prefers the source scripts dir (live-editable for --dev hot reload),
# falling back to rogue_scripts/ next to the binary.
target_compile_definitions(jai_rogue PRIVATE
    JAI_ROGUE_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
)

if(MSVC)
    target_compile_options(jai_rogue PRIVATE /W3 /EHsc /permissive- /bigobj)
    set_property(TARGET jai_rogue PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

add_custom_command(TARGET jai_rogue POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:jai_rogue>/rogue_scripts
)
