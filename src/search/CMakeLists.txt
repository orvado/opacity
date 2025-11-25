add_library(opacity_search
    SearchEngine.cpp
    FilterEngine.cpp
)

target_include_directories(opacity_search 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_search
    PRIVATE
    opacity_core
    opacity_filesystem
    spdlog::spdlog
)

set_target_properties(opacity_search PROPERTIES
    FOLDER "Opacity/Search"
)
