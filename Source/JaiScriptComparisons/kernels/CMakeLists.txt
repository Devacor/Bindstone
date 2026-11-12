# jai_kernel_host: the kernel suite's JaiScript host (the runner's stdlib plus the add_one the interop kernel calls).
# The interpreter rows run through it too, so it links the library with the interpreter when the build has one.
add_executable(jai_kernel_host host/jai_kernel_host.cpp)
if(TARGET jaiscript_interpreter)
    target_link_libraries(jai_kernel_host PRIVATE jaiscript_interpreter)
else()
    target_link_libraries(jai_kernel_host PRIVATE jaiscript)
endif()
if(MSVC)
    target_compile_options(jai_kernel_host PRIVATE /EHsc /permissive-
        $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Ob2> $<$<CONFIG:Release>:/Oi> $<$<CONFIG:Release>:/Ot>)
    target_link_options(jai_kernel_host PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
comparison_ltcg(jai_kernel_host)
