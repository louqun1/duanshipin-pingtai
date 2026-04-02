#include <QApplication>

#include "bootstrap/AppBootstrap.hpp"
#include "liveplayer/logging/LiveWatchLogger.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

std::string resolveLogFilePath()
{
    const std::filesystem::path logDir = std::filesystem::current_path() / "logs";
    std::error_code error;
    std::filesystem::create_directories(logDir, error);
    return (logDir / "flashpoint.log").string();
}

void initialize_logging()
{
    const std::string logFilePath = resolveLogFilePath();
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, false);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    auto logger = std::make_shared<spdlog::logger>("flashpoint", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    spdlog::info("Logging initialized, file={}", logFilePath);

    backend::liveplayer::logging::initializeLiveWatchLogger();
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Flashpoint");
    QApplication::setApplicationName("FlashpointShortVideos");
    initialize_logging();
    spdlog::info("Starting desktop rebuild skeleton");

    try {
        AppBootstrap bootstrap;
        bootstrap.mainWindow()->show();

        // 每当 socket 上又来了新数据，Qt 就会反复触发 HttpFlvStreamReader.cpp (line 86) handleReadyRead()
        const int exitCode = app.exec();//启动了事件循环，只要程序没退出，Qt 就会一直监听网络事件
        spdlog::shutdown();
        return exitCode;
    } catch (const std::exception &exception) {
        spdlog::error("Application bootstrap failed: {}", exception.what());
        spdlog::shutdown();
        return 1;
    }
}
