# jai_gloom - GLOOM, a terminal 2.5D raycast shooter written in JaiScript.
# C++ is a thin console host; walls and particles update through parallel_transform.
add_executable(jai_gloom main.cpp)
target_link_libraries(jai_gloom PRIVATE jaiscript)

# The exe prefers the source scripts dir (live-editable), falling back to
# gloom_scripts/ next to the binary.
target_compile_definitions(jai_gloom PRIVATE
    JAI_GLOOM_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
)

if(MSVC)
    target_compile_options(jai_gloom PRIVATE /W3 /EHsc /permissive- /bigobj)
    set_property(TARGET jai_gloom PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_link_libraries(jai_gloom PRIVATE winmm)
endif()

add_custom_command(TARGET jai_gloom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:jai_gloom>/gloom_scripts
)
