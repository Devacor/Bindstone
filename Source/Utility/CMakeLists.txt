add_library(utility STATIC
  chaiscriptStdLib.cpp
  expression.cpp
  generalUtility.cpp
  log.cpp
  sha512.cpp
  stopwatch.cpp
  threadPool.cpp
  tinyutf8.cpp
)

target_include_directories(utility PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/Source
)

target_link_libraries(utility PUBLIC
  CONAN_PKG::chaiscript
  CONAN_PKG::boost
  CONAN_PKG::cereal
  CONAN_PKG::exprtk
  CONAN_PKG::SDL2
)

target_compile_options(utility PUBLIC
  $<$<CXX_COMPILER_ID:MSVC>:/std:c++latest /bigobj>
)

target_compile_definitions(utility PUBLIC
  $<$<CXX_COMPILER_ID:MSVC>:_SCL_SECURE_NO_WARNINGS NOMINMAX _WIN32_WINNT=0x0501 _HAS_AUTO_PTR_ETC>
)