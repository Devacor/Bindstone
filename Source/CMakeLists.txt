add_subdirectory(Animation)
add_subdirectory(Utility)
add_subdirectory(ArtificialIntelligence)
add_subdirectory(Render)
add_subdirectory(Audio)

add_executable(MoonDog main.cpp)
target_link_libraries(MoonDog artificialintelligence audio)
