#pragma once

#include "domain/models/search_history_item.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QEnterEvent;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPoint;
class QResizeEvent;

class RecentSearchItemWidget final : public QWidget {
    Q_OBJECT

public:
    explicit RecentSearchItemWidget(QWidget* parent = nullptr);

    void setData(const domain::models::SearchHistoryItem& item);
    void setEntryId(const QString& entryId);
    void setQueryText(const QString& queryText);
    void setTimeText(const QString& timeText);
    void setMetaText(const QString& metaText);

signals:
    void reSearchClicked(const QString& query);
    void deleteClicked(const QString& entryId);

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
    void refreshQueryLabel();
    void updateStateProperty(const char* propertyName, bool value);
    bool isActionButton(const QWidget* widget) const;
    bool shouldTriggerItemClick(const QPoint& pos) const;
    static void refreshStyle(QWidget* widget);

    QString entryId_;
    QString queryText_;
    QString timeText_;
    QString metaText_;

    QLabel* queryLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* metaLabel_ = nullptr;
    QPushButton* searchAgainButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
};
