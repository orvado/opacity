add_library(opacity_core
    Logger.cpp
    Config.cpp
    Path.cpp
    ShellIntegration.cpp
    PluginManager.cpp
    CrashRecovery.cpp
    BookmarkManager.cpp
    SessionManager.cpp
    TagManager.cpp
    Localization.cpp
    UpdateManager.cpp
)

target_include_directories(opacity_core 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opacity_core
    PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    dbghelp
    wininet
    crypt32
)

set_target_properties(opacity_core PROPERTIES
    FOLDER "Opacity/Core"
)
