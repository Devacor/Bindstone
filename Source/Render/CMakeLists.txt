add_library(render STATIC
  boxaabb.cpp
  formattedText.cpp
  matrix.cpp
  points.cpp
  render.cpp
  sharedTextures.cpp
  texturePacker.cpp
  textures.cpp
  Scene/button.cpp
  Scene/clickable.cpp
  Scene/clipped.cpp
  Scene/component.cpp
  Scene/drawable.cpp
  Scene/emitter.cpp
  Scene/grid.cpp
  Scene/node.cpp
  Scene/palette.cpp
  Scene/path.cpp
  Scene/scroller.cpp
  Scene/slider.cpp
  Scene/spineMV.cpp
  Scene/sprite.cpp
  Scene/stencil.cpp
  Scene/text.cpp
)

target_include_directories(render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render PUBLIC
  utility
  CONAN_PKG::gl3w
  CONAN_PKG::SDL2
  CONAN_PKG::SDL2_ttf
  CONAN_PKG::SDL2_image
  CONAN_PKG::spine
)
