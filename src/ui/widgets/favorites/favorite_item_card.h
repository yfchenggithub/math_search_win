#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QEnterEvent;
class QEvent;
class QFocusEvent;
class QHBoxLayout;
class QLabel;
class QKeyEvent;
class QMouseEvent;
class QPoint;
class QPushButton;
class QResizeEvent;
class QVBoxLayout;

struct FavoriteItemViewData {
    QString conclusionId;
    QString title;
    QString module;
    QStringList tags;
    QString difficulty;
    QString summary;
    QString favoriteTimeText;
};

class FavoriteItemCard final : public QWidget {
    Q_OBJECT

public:
    explicit FavoriteItemCard(QWidget* parent = nullptr);
    void setData(const FavoriteItemViewData& item);

signals:
    void openRequested(const QString& conclusionId);
    void unfavoriteRequested(const QString& conclusionId);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUi();
    void setupConnections();
    void rebuildChips();
    void refreshTitleLabel();
    void refreshSummaryLabel();
    void updateStateProperty(const char* propertyName, bool value);
    bool isActionButton(const QWidget* widget) const;
    bool shouldTriggerOpen(const QPoint& pos) const;
    static void refreshStyle(QWidget* widget);

private:
    FavoriteItemViewData data_;

    QLabel* titleLabel_ = nullptr;
    QLabel* moduleLabel_ = nullptr;
    QWidget* chipsContainer_ = nullptr;
    QHBoxLayout* chipsLayout_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* favoriteTimeLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* unfavoriteButton_ = nullptr;
};
