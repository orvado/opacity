#include <iostream>
#include <filesystem>
#include "opacity/core/Logger.h"
#include "opacity/core/Config.h"
#include "opacity/ui/MainWindow.h"

/**
 * @brief Main entry point for Opacity application
 * 
 * Initializes all subsystems and runs the main application loop.
 */
int main()
{
    try
    {
        // Initialize logging first
        opacity::core::Logger::Initialize("debug");
        
        SPDLOG_INFO("========================================");
        SPDLOG_INFO("Opacity - Windows File Manager");
        SPDLOG_INFO("Version: 1.0.0");
        SPDLOG_INFO("========================================");

        // Initialize configuration system
        opacity::core::Config::Initialize("Opacity");
        SPDLOG_INFO("Configuration system initialized");

        // Initialize and run UI
        opacity::ui::MainWindow window;
        if (window.Initialize())
        {
            SPDLOG_INFO("Main window initialized");
            window.Run();
            window.Shutdown();
        }
        else
        {
            SPDLOG_ERROR("Failed to initialize main window");
            return 1;
        }

        opacity::core::Logger::Shutdown();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        SPDLOG_CRITICAL("Fatal error: {}", ex.what());
        return 1;
    }
}
