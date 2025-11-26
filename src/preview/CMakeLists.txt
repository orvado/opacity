add_library(opacity_preview
    PreviewManager.cpp
    ImagePreviewHandler.cpp
    TextPreviewHandler.cpp
    MediaPreviewHandler.cpp
    DocumentPreviewHandler.cpp
    HexPreviewHandler.cpp
)

target_include_directories(opacity_preview 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_preview
    PRIVATE
    opacity_core
    opacity_filesystem
    spdlog::spdlog
    mfplat
    mfreadwrite
    mfuuid
    propsys
    d3d11
)

set_target_properties(opacity_preview PROPERTIES
    FOLDER "Opacity/Preview"
)
