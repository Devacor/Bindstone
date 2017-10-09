#add_subdirectory(Animation)

if(XCODE)
  get_resources_source_list(MoonDog ALL_RESOURCES)
endif()

add_executable(MoonDog main.cpp StdAfx.cpp ${ALL_RESOURCES})
copy_resources(MoonDog)

add_subdirectory(Utility)
add_subdirectory(ArtificialIntelligence)
add_subdirectory(Render)
add_subdirectory(Audio)
add_subdirectory(Editor)
add_subdirectory(Game)
add_subdirectory(Interface)
add_subdirectory(Network)
add_subdirectory(Physics)

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

set_target_properties(MoonDog PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME BindStone
  MACOSX_BUNDLE_GUI_IDENTIFIER com.mutedvision.bindstone
  MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION}
  MACOSX_BUNDLE_LONG_VERSION_STRING ${APP_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_VERSION}
#  MACOSX_BUNDLE_ICON_FILE 
)
