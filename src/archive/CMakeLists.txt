# Archive management library

add_library(opacity_archive
    ArchiveManager.cpp
    ${PROJECT_SOURCE_DIR}/external/miniz.c
    ${PROJECT_SOURCE_DIR}/external/miniz_tdef.c
    ${PROJECT_SOURCE_DIR}/external/miniz_tinfl.c
    ${PROJECT_SOURCE_DIR}/external/miniz_zip.c
)

target_include_directories(opacity_archive 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external
)

target_link_libraries(opacity_archive
    PRIVATE
    opacity_core
    opacity_filesystem
    spdlog::spdlog
)

set_target_properties(opacity_archive PROPERTIES
    FOLDER "Opacity/Archive"
)

message(STATUS "Opacity archive library configured")
