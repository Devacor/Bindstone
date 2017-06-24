add_subdirectory(Animation)
add_subdirectory(Utility)

add_executable(MoonDog main.cpp)
target_link_libraries(MoonDog animation)
