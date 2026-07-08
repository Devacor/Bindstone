# jaiscript — the standalone runner/REPL (file runner, interactive REPL, --watch live-coding).
# The target is jaiscript_runner (the library already owns the `jaiscript` target name);
# the produced binary is bin/jaiscript.exe.
add_executable(jaiscript_runner main.cpp)
set_target_properties(jaiscript_runner PROPERTIES OUTPUT_NAME jaiscript)
target_link_libraries(jaiscript_runner PRIVATE jaiscript)

if(MSVC)
    target_compile_options(jaiscript_runner PRIVATE /W3 /EHsc /permissive- /bigobj)
    set_property(TARGET jaiscript_runner PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()
