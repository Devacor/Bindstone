# Where the comparison build finds JaiScript: $ENV{JAISCRIPT_DIR}, else the jaiscript/ submodule, else a ../JaiScript tree
# (comparison_paths.py applies the same rule to the runners). A -DJAISCRIPT_DIR on the command line wins over all three.
if(DEFINED ENV{JAISCRIPT_DIR})
	set(comparison_jaiscript_default "$ENV{JAISCRIPT_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/jaiscript/CMakeLists.txt")
	set(comparison_jaiscript_default "${CMAKE_CURRENT_LIST_DIR}/jaiscript")
else()
	get_filename_component(comparison_jaiscript_default "${CMAKE_CURRENT_LIST_DIR}/../JaiScript" ABSOLUTE)
endif()
set(JAISCRIPT_DIR "${comparison_jaiscript_default}" CACHE PATH "The JaiScript source root")
