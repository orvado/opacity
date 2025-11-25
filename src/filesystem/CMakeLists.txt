add_library(opacity_filesystem
    FsItem.cpp
    FileSystemManager.cpp
)

target_include_directories(opacity_filesystem 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_filesystem
    PRIVATE
    opacity_core
    spdlog::spdlog
)

set_target_properties(opacity_filesystem PROPERTIES
    FOLDER "Opacity/FileSystem"
)
