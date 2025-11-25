add_library(opacity_diff
    DiffEngine.cpp
    FolderComparison.cpp
)

target_include_directories(opacity_diff 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_diff
    PRIVATE
    opacity_core
    opacity_filesystem
    spdlog::spdlog
)

set_target_properties(opacity_diff PROPERTIES
    FOLDER "Opacity/Diff"
)
