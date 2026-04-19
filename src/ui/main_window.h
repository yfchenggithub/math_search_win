#pragma once

#include <QString>
#include <QMainWindow>

class BottomStatusBar;
class NavigationSidebar;
class QStackedWidget;
class TopBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void switchPage(int pageIndex);

private:
    void setupUi();
    void setupPages();
    QString titleForPage(int pageIndex) const;
    QString subtitleForPage(int pageIndex) const;

    NavigationSidebar* navigationSidebar_ = nullptr;
    TopBar* topBar_ = nullptr;
    QStackedWidget* pageStack_ = nullptr;
    BottomStatusBar* bottomStatusBar_ = nullptr;
};
