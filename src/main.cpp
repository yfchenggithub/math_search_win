#include <QApplication>
#include <QCoreApplication>

#include "ui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("math_search"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("local.offline"));
    QCoreApplication::setApplicationName(QStringLiteral("math_search_win"));

    MainWindow window;
    window.show();

    return app.exec();
}
