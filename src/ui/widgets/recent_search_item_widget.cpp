#include "ui/widgets/recent_search_item_widget.h"

#include "ui/style/app_style.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

RecentSearchItemWidget::RecentSearchItemWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("listItemCard"));
    setProperty("cardRole", QStringLiteral("recentListItem"));
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);

    setProperty("hovered", false);
    setProperty("pressed", false);
    setProperty("focused", false);

    setupUi();
    setupConnections();
}

void RecentSearchItemWidget::setData(const domain::models::SearchHistoryItem& item)
{
    setEntryId(item.query.trimmed());
    setQueryText(item.query);
    setTimeText(item.searchedAt.isValid() ? item.searchedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString());
    setMetaText(item.source.trimmed().isEmpty() ? QString() : item.source.trimmed());
}

void RecentSearchItemWidget::setEntryId(const QString& entryId)
{
    entryId_ = entryId.trimmed();
}

void RecentSearchItemWidget::setQueryText(const QString& queryText)
{
    queryText_ = queryText.trimmed();
    refreshQueryLabel();
    queryLabel_->setToolTip(queryText_);
}

void RecentSearchItemWidget::setTimeText(const QString& timeText)
{
    timeText_ = timeText.trimmed();
    timeLabel_->setText(timeText_);
}

void RecentSearchItemWidget::setMetaText(const QString& metaText)
{
    metaText_ = metaText.trimmed();
    const bool hasMeta = !metaText_.isEmpty();
    metaLabel_->setVisible(hasMeta);
    metaLabel_->setText(hasMeta ? metaText_ : QString());
}

void RecentSearchItemWidget::enterEvent(QEnterEvent* event)
{
    updateStateProperty("hovered", true);
    QWidget::enterEvent(event);
}

void RecentSearchItemWidget::leaveEvent(QEvent* event)
{
    updateStateProperty("hovered", false);
    updateStateProperty("pressed", false);
    QWidget::leaveEvent(event);
}

void RecentSearchItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        updateStateProperty("pressed", true);
    }
    QWidget::mousePressEvent(event);
}

void RecentSearchItemWidget::mouseReleaseEvent(QMouseEvent* event)
{
    const bool shouldTrigger = event->button() == Qt::LeftButton && shouldTriggerItemClick(event->pos());
    updateStateProperty("pressed", false);
    QWidget::mouseReleaseEvent(event);

    if (shouldTrigger && !queryText_.isEmpty()) {
        setFocus(Qt::MouseFocusReason);
        emit reSearchClicked(queryText_);
    }
}

void RecentSearchItemWidget::focusInEvent(QFocusEvent* event)
{
    updateStateProperty("focused", true);
    QWidget::focusInEvent(event);
}

void RecentSearchItemWidget::focusOutEvent(QFocusEvent* event)
{
    updateStateProperty("focused", false);
    QWidget::focusOutEvent(event);
}

void RecentSearchItemWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshQueryLabel();
}

void RecentSearchItemWidget::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
        && !queryText_.isEmpty()) {
        emit reSearchClicked(queryText_);
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RecentSearchItemWidget::setupUi()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(ui::style::tokens::kCardPaddingHorizontal,
                                   ui::style::tokens::kCardPaddingVertical,
                                   ui::style::tokens::kCardPaddingHorizontal,
                                   ui::style::tokens::kCardPaddingVertical);
    rootLayout->setSpacing(14);

    auto* textBlock = new QWidget(this);
    auto* textLayout = new QVBoxLayout(textBlock);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(5);

    queryLabel_ = new QLabel(textBlock);
    queryLabel_->setObjectName(QStringLiteral("cardTitleLabel"));
    queryLabel_->setWordWrap(false);
    queryLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    timeLabel_ = new QLabel(textBlock);
    timeLabel_->setObjectName(QStringLiteral("cardMetaLabel"));
    timeLabel_->setWordWrap(false);

    metaLabel_ = new QLabel(textBlock);
    metaLabel_->setObjectName(QStringLiteral("subtleTextLabel"));
    metaLabel_->setWordWrap(false);
    metaLabel_->setVisible(false);

    textLayout->addWidget(queryLabel_);
    textLayout->addWidget(timeLabel_);
    textLayout->addWidget(metaLabel_);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(ui::style::tokens::kSmallSpacing);

    searchAgainButton_ = new QPushButton(QStringLiteral("再次搜索"), this);
    searchAgainButton_->setObjectName(QStringLiteral("primaryButton"));
    searchAgainButton_->setCursor(Qt::PointingHandCursor);

    deleteButton_ = new QPushButton(QStringLiteral("删除"), this);
    deleteButton_->setObjectName(QStringLiteral("weakDangerButton"));
    deleteButton_->setCursor(Qt::PointingHandCursor);

    actionLayout->addWidget(searchAgainButton_);
    actionLayout->addWidget(deleteButton_);

    rootLayout->addWidget(textBlock, 1);
    rootLayout->addLayout(actionLayout, 0);
}

void RecentSearchItemWidget::setupConnections()
{
    connect(searchAgainButton_, &QPushButton::clicked, this, [this]() {
        if (!queryText_.isEmpty()) {
            emit reSearchClicked(queryText_);
        }
    });

    connect(deleteButton_, &QPushButton::clicked, this, [this]() {
        const QString id = entryId_.isEmpty() ? queryText_ : entryId_;
        if (!id.trimmed().isEmpty()) {
            emit deleteClicked(id);
        }
    });
}

void RecentSearchItemWidget::refreshQueryLabel()
{
    if (queryLabel_ == nullptr) {
        return;
    }

    const int availableWidth = std::max(60, queryLabel_->width());
    const QFontMetrics metrics(queryLabel_->font());
    const QString elidedText = metrics.elidedText(queryText_, Qt::ElideRight, availableWidth);
    queryLabel_->setText(elidedText);
}

void RecentSearchItemWidget::updateStateProperty(const char* propertyName, bool value)
{
    const QVariant currentValue = property(propertyName);
    if (currentValue.isValid() && currentValue.toBool() == value) {
        return;
    }

    setProperty(propertyName, value);
    refreshStyle(this);
}

bool RecentSearchItemWidget::isActionButton(const QWidget* widget) const
{
    for (const QWidget* current = widget; current != nullptr; current = current->parentWidget()) {
        if (current == searchAgainButton_ || current == deleteButton_) {
            return true;
        }
        if (current == this) {
            break;
        }
    }
    return false;
}

bool RecentSearchItemWidget::shouldTriggerItemClick(const QPoint& pos) const
{
    const QWidget* target = childAt(pos);
    if (target == nullptr) {
        return true;
    }
    return !isActionButton(target);
}

void RecentSearchItemWidget::refreshStyle(QWidget* widget)
{
    if (widget == nullptr || widget->style() == nullptr) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
