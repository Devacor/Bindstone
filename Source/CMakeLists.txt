#add_subdirectory(Animation)

if(XCODE)
  get_resources_source_list(BindStone ALL_RESOURCES)
  source_group(TREE ${CMAKE_SOURCE_DIR} PREFIX "Resources" FILES ${ALL_RESOURCES})
endif()

add_executable(BindStone main.cpp StdAfx.cpp ${ALL_RESOURCES})
copy_resources(BindStone)

add_subdirectory(Utility)
add_subdirectory(ArtificialIntelligence)
add_subdirectory(Render)
add_subdirectory(Audio)
add_subdirectory(Editor)
add_subdirectory(Game)
add_subdirectory(Interface)
add_subdirectory(Network)
add_subdirectory(Physics)

# Group source files
file(GLOB_RECURSE TREE_SOURCES LIST_DIRECTORIES false *.cpp *.c)
source_group(TREE ${CMAKE_SOURCE_DIR}/Source PREFIX "Source Files" FILES ${TREE_SOURCES})

file(GLOB_RECURSE TREE_HEADERS LIST_DIRECTORIES false *.hpp *.h)
source_group(TREE ${CMAKE_SOURCE_DIR}/Source PREFIX "Header Files" FILES ${TREE_HEADERS})

target_link_libraries(BindStone
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

target_include_directories(BindStone PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_options(BindStone PRIVATE
)

target_compile_definitions(BindStone PRIVATE
)

if(NOT XCODE)
  add_precompiled_header(BindStone StdAfx.h SOURCE_CXX StdAfx.cpp FORCEINCLUDE)
endif()

set_target_properties(BindStone PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME BindStone
  MACOSX_BUNDLE_GUI_IDENTIFIER com.mutedvision.bindstone
  MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION}
  MACOSX_BUNDLE_LONG_VERSION_STRING ${APP_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_VERSION}
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/CMake/Info.plist.in
  XCODE_ATTRIBUTE_GCC_PREFIX_HEADER "${PROJECT_SOURCE_DIR}/Source/StdAfx.h"
  XCODE_ATTRIBUTE_GCC_PRECOMPILE_PREFIX_HEADER "YES"
#  MACOSX_BUNDLE_ICON_FILE
)
