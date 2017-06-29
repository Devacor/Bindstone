add_library(artificialintelligence STATIC
  pathfinding.cpp
)

target_link_libraries(artificialintelligence PUBLIC utility render)
target_include_directories(artificialintelligence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})