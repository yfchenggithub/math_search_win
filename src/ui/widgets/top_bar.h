#pragma once

#include <QString>
#include <QWidget>

class QLabel;

class TopBar : public QWidget {
    Q_OBJECT

public:
    explicit TopBar(QWidget* parent = nullptr);
    void setPageTitle(const QString& title, const QString& subtitle = QString());

private:
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
};
