# Opacity Application Main Executable

# Add all subsystem libraries
add_subdirectory(core)
add_subdirectory(filesystem)
add_subdirectory(ui)
add_subdirectory(search)
add_subdirectory(preview)
add_subdirectory(diff)

# Main application executable
add_executable(opacity
    main.cpp
)

target_include_directories(opacity 
    PRIVATE 
    ${PROJECT_SOURCE_DIR}/include
)

# Link all subsystem libraries
target_link_libraries(opacity
    PRIVATE
    opacity_core
    opacity_filesystem
    opacity_ui
    opacity_search
    opacity_preview
    opacity_diff
    imgui::imgui
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Set as startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT opacity)

# MSVC specific settings
if(MSVC)
    # Use dynamic runtime to match vcpkg libraries
    set_property(TARGET opacity PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    
    # Set warning level to W4
    target_compile_options(opacity PRIVATE /W4 /WX-)
    
    # Keep console window for debugging (remove for release)
    # set_target_properties(opacity PROPERTIES WIN32_EXECUTABLE ON)
endif()

message(STATUS "Opacity main executable configured")
