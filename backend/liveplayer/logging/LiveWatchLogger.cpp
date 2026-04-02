#include "liveplayer/logging/LiveWatchLogger.hpp"

#include "spdlog/logger.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

#include <filesystem>
#include <system_error>

namespace backend::liveplayer::logging {

std::string resolveLiveWatchLogFilePath()
{
    const std::filesystem::path logDir = std::filesystem::current_path() / "logs";
    std::error_code error;
    std::filesystem::create_directories(logDir, error);
    return (logDir / "live_watch.log").string();
}

std::shared_ptr<spdlog::logger> initializeLiveWatchLogger()
{
    if (auto existingLogger = spdlog::get(kLiveWatchLoggerName)) {
        return existingLogger;
    }

    const std::string logFilePath = resolveLiveWatchLogFilePath();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, false);
    auto logger = std::make_shared<spdlog::logger>(kLiveWatchLoggerName, fileSink);
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] [%n] %v");

    spdlog::register_or_replace(logger);
    logger->info("[logger] live watch logger initialized, file={}", logFilePath);
    return logger;
}

std::shared_ptr<spdlog::logger> liveWatchLogger()
{
    return spdlog::get(kLiveWatchLoggerName);
}

}  // namespace backend::liveplayer::logging
