#add_subdirectory(Animation)

add_executable(MoonDog ../SolutionSpecific/BindstoneClient/main.cpp StdAfx.cpp)

add_subdirectory(SharedCore/Utility) 
add_subdirectory(SharedCore/ArtificialIntelligence)
add_subdirectory(SharedCore/Render)
add_subdirectory(SharedCore/Audio)
#add_subdirectory(ClickerGame)
#add_subdirectory(DiggerGame)
add_subdirectory(Editor)
add_subdirectory(Game)
add_subdirectory(SharedCore/Interface)
add_subdirectory(SharedCore/Network)
add_subdirectory(SharedCore/Physics)

target_link_libraries(MoonDog
  CONAN_PKG::OpenSSL
  CONAN_PKG::Box2D
  CONAN_PKG::chaiscript
  CONAN_PKG::boost
  CONAN_PKG::cereal
  CONAN_PKG::exprtk
  CONAN_PKG::SDL2
  CONAN_PKG::SDL2_ttf
  CONAN_PKG::SDL2_mixer
  CONAN_PKG::SDL2_image
  CONAN_PKG::spine
  CONAN_PKG::boolinq
)

target_include_directories(MoonDog PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_options(MoonDog PRIVATE
)

target_compile_definitions(MoonDog PRIVATE
)

if(NOT XCODE)
  add_precompiled_header(MoonDog StdAfx.h SOURCE_CXX StdAfx.cpp FORCEINCLUDE)
endif()
