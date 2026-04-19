#include <QApplication>
#include <QCoreApplication>

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "ui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("math_search"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("local.offline"));
    QCoreApplication::setApplicationName(QStringLiteral("math_search_win"));

    logging::Logger::instance().initialize();
    LOG_INFO(LogCategory::AppStartup,
             QStringLiteral("application startup app=%1 logDir=%2")
                 .arg(QCoreApplication::applicationName(), logging::Logger::instance().logDirectory()));

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        LOG_INFO(LogCategory::AppShutdown, QStringLiteral("aboutToQuit signal received"));
    });

    MainWindow window;
    window.show();
    LOG_INFO(LogCategory::AppStartup, QStringLiteral("main window created and shown"));

    const int exitCode = app.exec();
    LOG_INFO(LogCategory::AppShutdown, QStringLiteral("application exited exitCode=%1").arg(exitCode));
    logging::Logger::instance().shutdown();
    return exitCode;
}
