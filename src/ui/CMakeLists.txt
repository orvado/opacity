add_library(opacity_ui
    MainWindow.cpp
    Theme.cpp
    ImGuiBackend.cpp
)

target_include_directories(opacity_ui 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_ui
    PRIVATE
    opacity_core
    opacity_filesystem
    imgui::imgui
    spdlog::spdlog
    d3d11
    dxgi
    d3dcompiler
)

set_target_properties(opacity_ui PROPERTIES
    FOLDER "Opacity/UI"
)
