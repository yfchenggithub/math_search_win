#pragma once

#include <QString>
#include <QWidget>

class QButtonGroup;
class QVBoxLayout;

class NavigationSidebar : public QWidget {
    Q_OBJECT

public:
    explicit NavigationSidebar(QWidget* parent = nullptr);
    void setCurrentIndex(int index);

signals:
    void pageRequested(int pageIndex);

private:
    void addNavButton(QVBoxLayout* layout, const QString& text, int pageIndex);

    QButtonGroup* buttonGroup_ = nullptr;
};
