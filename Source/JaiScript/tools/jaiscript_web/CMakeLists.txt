# jaiscript_web (Emscripten only): the engine and the runner's console builtins as WebAssembly for the site's play and
# sandbox pages. jaiscript_web_worker runs a script to its end in a Web Worker; jaiscript_web (Asyncify) runs it in steps,
# returning at every sleep, where the page cannot share memory with a worker. Each builds <name>.js, which loads <name>.wasm.
function(jaiscript_web_target name)
	add_executable(${name} web_main.cpp)
	target_link_libraries(${name} PRIVATE jaiscript)
	target_compile_options(${name} PRIVATE -fwasm-exceptions)
	target_link_options(${name} PRIVATE
		-fwasm-exceptions
		-sSTACK_SIZE=8388608
		-sALLOW_MEMORY_GROWTH
		-sINITIAL_MEMORY=67108864
		-sMODULARIZE
		-sEXPORT_NAME=createJaiScript
		-sENVIRONMENT=web,worker,node
		-sFORCE_FILESYSTEM
		-sEXPORTED_RUNTIME_METHODS=FS,NODEFS,ccall,cwrap,UTF8ToString
		-lnodefs.js
		${ARGN}
	)
endfunction()

jaiscript_web_target(jaiscript_web_worker)
jaiscript_web_target(jaiscript_web -sASYNCIFY -sASYNCIFY_STACK_SIZE=1048576)
