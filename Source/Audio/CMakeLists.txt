add_library(audio STATIC
  package.h sound.cpp sound.h
)

target_link_libraries(audio PUBLIC utility render CONAN_PKG::SDL2_mixer)