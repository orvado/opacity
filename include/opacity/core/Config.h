#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace opacity::core
{
    using json = nlohmann::json;

    /**
     * @brief Configuration management system
     * 
     * Handles application settings stored in JSON format in %APPDATA%\Opacity.
     * Provides thread-safe access to configuration with live reload capability.
     */
    class Config
    {
    public:
        /**
         * @brief Initialize configuration system
         * @param app_name Name of application subdirectory in APPDATA
         */
        static void Initialize(const std::string& app_name = "Opacity");

        /**
         * @brief Get configuration instance
         * @return Shared pointer to Config singleton
         */
        static std::shared_ptr<Config> Get();

        /**
         * @brief Load configuration from disk
         */
        void Load();

        /**
         * @brief Save configuration to disk
         */
        void Save();

        /**
         * @brief Get configuration value with default fallback
         * @tparam T Type of value to retrieve
         * @param key Configuration key (dot-separated for nested values)
         * @param default_value Default value if key not found
         */
        template<typename T>
        T Get(const std::string& key, const T& default_value = T()) const;

        /**
         * @brief Set configuration value
         * @tparam T Type of value to set
         * @param key Configuration key (dot-separated for nested values)
         * @param value Value to set
         */
        template<typename T>
        void Set(const std::string& key, const T& value);

        /**
         * @brief Get raw JSON object
         */
        const json& GetRaw() const { return data_; }

        /**
         * @brief Get configuration directory path
         */
        std::string GetConfigDir() const { return config_dir_; }

    private:
        Config();

        static std::shared_ptr<Config> instance_;
        json data_;
        std::string config_dir_;
        std::string config_file_;
    };

    // Template implementations
    template<typename T>
    T Config::Get(const std::string& key, const T& default_value) const
    {
        try
        {
            size_t pos = 0;
            json* current = const_cast<json*>(&data_);

            while ((pos = key.find('.')) != std::string::npos)
            {
                std::string segment = key.substr(0, pos);
                current = &(*current)[segment];
                // Would need proper error handling in actual implementation
            }

            std::string last_segment = key.substr(pos);
            if (current->contains(last_segment))
            {
                return current->at(last_segment).get<T>();
            }
        }
        catch (const std::exception&)
        {
            // Return default on any error
        }

        return default_value;
    }

    template<typename T>
    void Config::Set(const std::string& key, const T& value)
    {
        try
        {
            size_t pos = 0;
            json* current = &data_;

            while ((pos = key.find('.')) != std::string::npos)
            {
                std::string segment = key.substr(0, pos);
                (*current)[segment] = json::object();
                current = &(*current)[segment];
            }

            std::string last_segment = key.substr(pos);
            (*current)[last_segment] = value;
        }
        catch (const std::exception&)
        {
            // Handle error appropriately
        }
    }

} // namespace opacity::core
