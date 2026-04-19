#pragma once

#include <QString>
#include <QWidget>

class QLabel;

class BottomStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit BottomStatusBar(QWidget* parent = nullptr);
    void setDataStatusText(const QString& text);
    void setVersionStatusText(const QString& text);

private:
    QLabel* modeLabel_ = nullptr;
    QLabel* dataStatusLabel_ = nullptr;
    QLabel* versionStatusLabel_ = nullptr;
};
