#include <QApplication>
#include <QLabel>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QLabel label("math_search_win bootstrap OK");
    label.resize(320, 80);
    label.show();

    return app.exec();
}