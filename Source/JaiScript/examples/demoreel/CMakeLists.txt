# jai_demoreel - demoscene tech reel: every effect is JaiScript, C++ is a thin console host.
add_executable(jai_demoreel main.cpp)
target_link_libraries(jai_demoreel PRIVATE jaiscript)

# The exe prefers the source scenes dir (live-editable for the 'r' hot reload),
# falling back to demoreel_scenes/ next to the binary.
target_compile_definitions(jai_demoreel PRIVATE
    JAI_DEMOREEL_SOURCE_SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenes"
)

if(MSVC)
    target_compile_options(jai_demoreel PRIVATE /W3 /EHsc /permissive- /bigobj)
    set_property(TARGET jai_demoreel PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_link_libraries(jai_demoreel PRIVATE winmm)
endif()

add_custom_command(TARGET jai_demoreel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scenes
        $<TARGET_FILE_DIR:jai_demoreel>/demoreel_scenes
)
