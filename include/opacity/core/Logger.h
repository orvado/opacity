#pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/wincolor_sink.h>

namespace opacity::core
{
    /**
     * @brief Global logging system for Opacity
     * 
     * Provides file-based logging with configurable verbosity levels.
     * Thread-safe logging across all subsystems.
     */
    class Logger
    {
    public:
        /**
         * @brief Initialize the logging system
         * @param log_level Initial verbosity level (default: info)
         */
        static void Initialize(const std::string& log_level = "info");

        /**
         * @brief Get the logger instance
         * @return Shared pointer to the spdlog logger
         */
        static std::shared_ptr<spdlog::logger> Get();

        /**
         * @brief Shutdown logging system
         */
        static void Shutdown();

        /**
         * @brief Set logging verbosity level
         * @param level spdlog level (trace, debug, info, warn, err, critical, off)
         */
        static void SetLevel(const std::string& level);

    private:
        static std::shared_ptr<spdlog::logger> instance_;
    };

} // namespace opacity::core
