# jaiweb - the web application host (HTTP/1.1, hot-reloading app modules, SQLite). The target is jaiweb; the binary
# is bin/jaiweb[.exe], with the script library it imports (lib/) copied beside it as bin/jaiweb_lib.
enable_language(C)

add_library(jaiweb_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(jaiweb_sqlite PUBLIC third_party/sqlite)
target_compile_definitions(jaiweb_sqlite PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_THREADSAFE=1)
if(MSVC)
	target_compile_options(jaiweb_sqlite PRIVATE /W0)
else()
	target_compile_options(jaiweb_sqlite PRIVATE -w)
	find_package(Threads REQUIRED)
	target_link_libraries(jaiweb_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

add_executable(jaiweb main.cpp)
target_link_libraries(jaiweb PRIVATE jaiscript jaiweb_sqlite)
target_compile_definitions(jaiweb PRIVATE JAIWEB_SOURCE_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lib")
if(WIN32)
	target_compile_definitions(jaiweb PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
	target_link_libraries(jaiweb PRIVATE ws2_32 bcrypt)
endif()
if(MSVC)
	target_compile_options(jaiweb PRIVATE /W3)
endif()

add_custom_command(TARGET jaiweb POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lib $<TARGET_FILE_DIR:jaiweb>/jaiweb_lib)
