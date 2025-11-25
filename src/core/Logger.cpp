#include "opacity/core/Logger.h"

namespace opacity::core
{
    std::shared_ptr<spdlog::logger> Logger::instance_ = nullptr;

    void Logger::Initialize(const std::string& log_level)
    {
        try
        {
            auto max_size = 1024 * 1024 * 10; // 10 MB
            auto max_files = 3;
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                "opacity.log", false);

            auto logger = std::make_shared<spdlog::logger>("opacity", sink);
            logger->set_level(spdlog::level::from_str(log_level));
            logger->flush_on(spdlog::level::err);

            spdlog::set_default_logger(logger);
            instance_ = logger;

            SPDLOG_INFO("Logger initialized");
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            // Fallback to console logging
            auto console_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
            instance_ = std::make_shared<spdlog::logger>("opacity", console_sink);
            SPDLOG_ERROR("Failed to initialize file logger: {}", ex.what());
        }
    }

    std::shared_ptr<spdlog::logger> Logger::Get()
    {
        if (!instance_)
        {
            Initialize();
        }
        return instance_;
    }

    void Logger::Shutdown()
    {
        if (instance_)
        {
            instance_->flush();
            spdlog::drop("opacity");
            instance_ = nullptr;
        }
    }

    void Logger::SetLevel(const std::string& level)
    {
        if (instance_)
        {
            instance_->set_level(spdlog::level::from_str(level));
            SPDLOG_INFO("Log level changed to: {}", level);
        }
    }

} // namespace opacity::core
