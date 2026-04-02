#pragma once

#include "spdlog/common.h"
#include "spdlog/logger.h"

#include <memory>
#include <string>
#include <utility>

namespace backend::liveplayer::logging {

constexpr const char *kLiveWatchLoggerName = "live_watch";

std::string resolveLiveWatchLogFilePath();
std::shared_ptr<spdlog::logger> initializeLiveWatchLogger();
std::shared_ptr<spdlog::logger> liveWatchLogger();

template <typename... Args>
inline void debug(spdlog::format_string_t<Args...> formatString, Args &&...args)
{
    if (auto logger = liveWatchLogger()) {
        logger->debug(formatString, std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline void info(spdlog::format_string_t<Args...> formatString, Args &&...args)
{
    if (auto logger = liveWatchLogger()) {
        logger->info(formatString, std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline void warn(spdlog::format_string_t<Args...> formatString, Args &&...args)
{
    if (auto logger = liveWatchLogger()) {
        logger->warn(formatString, std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline void error(spdlog::format_string_t<Args...> formatString, Args &&...args)
{
    if (auto logger = liveWatchLogger()) {
        logger->error(formatString, std::forward<Args>(args)...);
    }
}

}  // namespace backend::liveplayer::logging
