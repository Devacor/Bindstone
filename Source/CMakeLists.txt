#add_subdirectory(Animation)

add_executable(MoonDog main.cpp StdAfx.cpp)

add_subdirectory(Utility)
add_subdirectory(ArtificialIntelligence)
add_subdirectory(Render)
add_subdirectory(Audio)
#add_subdirectory(ClickerGame)
#add_subdirectory(DiggerGame)
add_subdirectory(Editor)
add_subdirectory(Game)
add_subdirectory(Interface)
add_subdirectory(Network)
add_subdirectory(Physics)

target_link_libraries(MoonDog
  CONAN_PKG::OpenSSL
  CONAN_PKG::libpqxx
  CONAN_PKG::Box2D
  CONAN_PKG::chaiscript
  CONAN_PKG::boost
  CONAN_PKG::cereal
  CONAN_PKG::exprtk
  CONAN_PKG::SDL2
  CONAN_PKG::SDL2_ttf
  CONAN_PKG::SDL2_mixer
  CONAN_PKG::SDL2_image
  CONAN_PKG::gl3w
  CONAN_PKG::spine
  CONAN_PKG::boolinq
)

target_include_directories(MoonDog PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_options(MoonDog PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/std:c++latest /bigobj>
  $<$<BOOL:${GENERATOR_MSVC}>:/MP>
)

target_compile_definitions(MoonDog PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:_SCL_SECURE_NO_WARNINGS NOMINMAX _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0501 _HAS_AUTO_PTR_ETC>
)

add_precompiled_header(MoonDog StdAfx.h SOURCE_CXX StdAfx.cpp FORCEINCLUDE)
