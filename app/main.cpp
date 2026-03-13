#include <QApplication>

#include "bootstrap/AppBootstrap.hpp"
#include "spdlog/spdlog.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    spdlog::info("Starting desktop rebuild skeleton");

    AppBootstrap bootstrap;
    bootstrap.mainWindow()->show();

    return app.exec();
}
