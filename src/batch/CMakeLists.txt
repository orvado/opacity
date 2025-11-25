# Batch operations library

add_library(opacity_batch
    BatchRename.cpp
    DuplicateFinder.cpp
)

target_include_directories(opacity_batch 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_batch
    PRIVATE
    opacity_core
    opacity_filesystem
    spdlog::spdlog
)

# Windows-specific linking for shell operations
if(WIN32)
    target_link_libraries(opacity_batch
        PRIVATE
        shell32
    )
endif()

set_target_properties(opacity_batch PROPERTIES
    FOLDER "Opacity/Batch"
)

message(STATUS "Opacity batch library configured")
