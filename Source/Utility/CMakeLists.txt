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
  CONAN_PKG::SDL
)

target_compile_options(utility PRIVATE -fpermissive)
set_target_properties(utility PROPERTIES
  COTIRE_CXX_PREFIX_HEADER_INIT ${CMAKE_SOURCE_DIR}/Source/StdAfx.h
  COTIRE_ADD_UNITY_BUILD FALSE
)

cotire(utility)
