#include "opacity/core/Config.h"
#include "opacity/core/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>

namespace opacity::core
{
    std::shared_ptr<Config> Config::instance_ = nullptr;

    Config::Config()
    {
        // Get AppData directory
        wchar_t appdata_path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata_path)))
        {
            config_dir_ = (std::filesystem::path(appdata_path) / "Opacity").string();
            config_file_ = (std::filesystem::path(config_dir_) / "config.json").string();

            // Ensure directory exists
            std::filesystem::create_directories(config_dir_);
        }
        else
        {
            SPDLOG_ERROR("Failed to get AppData directory");
        }
    }

    void Config::Initialize(const std::string& app_name)
    {
        if (!instance_)
        {
            instance_ = std::shared_ptr<Config>(new Config());
            instance_->Load();
        }
    }

    std::shared_ptr<Config> Config::Get()
    {
        if (!instance_)
        {
            Initialize();
        }
        return instance_;
    }

    void Config::Load()
    {
        try
        {
            if (std::filesystem::exists(config_file_))
            {
                std::ifstream config_stream(config_file_);
                config_stream >> data_;
                SPDLOG_INFO("Configuration loaded from: {}", config_file_);
            }
            else
            {
                SPDLOG_INFO("No existing configuration file, using defaults");
                data_ = json::object();
            }
        }
        catch (const std::exception& ex)
        {
            SPDLOG_ERROR("Failed to load configuration: {}", ex.what());
            data_ = json::object();
        }
    }

    void Config::Save()
    {
        try
        {
            std::ofstream config_stream(config_file_);
            config_stream << data_.dump(2);
            SPDLOG_INFO("Configuration saved to: {}", config_file_);
        }
        catch (const std::exception& ex)
        {
            SPDLOG_ERROR("Failed to save configuration: {}", ex.what());
        }
    }

} // namespace opacity::core
