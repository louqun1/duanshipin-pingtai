#include <QApplication>

#include "bootstrap/AppBootstrap.hpp"
#include "spdlog/spdlog.h"

#include <exception>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Flashpoint");
    QApplication::setApplicationName("FlashpointShortVideos");
    spdlog::info("Starting desktop rebuild skeleton");

    try {
        AppBootstrap bootstrap;
        bootstrap.mainWindow()->show();

        return app.exec();
    } catch (const std::exception &exception) {
        spdlog::error("Application bootstrap failed: {}", exception.what());
        return 1;
    }
}
