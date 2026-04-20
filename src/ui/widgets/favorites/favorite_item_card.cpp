#include "ui/widgets/favorites/favorite_item_card.h"

#include "ui/style/app_style.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString collapseWhitespace(const QString& input)
{
    QString output;
    output.reserve(input.size());

    bool lastWasSpace = false;
    for (const QChar ch : input) {
        if (ch.isSpace()) {
            if (!lastWasSpace) {
                output.push_back(QChar::Space);
            }
            lastWasSpace = true;
            continue;
        }

        output.push_back(ch);
        lastWasSpace = false;
    }
    return output.trimmed();
}

QString elideTextToLines(const QFontMetrics& metrics, const QString& rawText, int width, int maxLines)
{
    const QString text = collapseWhitespace(rawText);
    if (text.isEmpty() || width <= 10 || maxLines <= 0) {
        return text;
    }

    QStringList lines;
    lines.reserve(maxLines);

    int cursor = 0;
    while (cursor < text.size() && lines.size() < maxLines) {
        QString line;
        while (cursor < text.size()) {
            const QChar ch = text.at(cursor);
            const QString candidate = line + ch;

            if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > width) {
                break;
            }

            line = candidate;
            ++cursor;
        }

        if (line.isEmpty() && cursor < text.size()) {
            line = text.mid(cursor, 1);
            ++cursor;
        }
        lines.push_back(line.trimmed());
    }

    const bool truncated = cursor < text.size();
    if (truncated && !lines.isEmpty()) {
        lines.last() = metrics.elidedText(lines.last() + text.mid(cursor), Qt::ElideRight, width);
    }

    return lines.join(QStringLiteral("\n"));
}

QLabel* createChip(QWidget* parent, const QString& text, const QString& role)
{
    const QString normalizedRole = role.trimmed().toLower();

    auto* label = new QLabel(text, parent);
    if (normalizedRole == QStringLiteral("difficulty")) {
        label->setObjectName(QStringLiteral("difficultyBadge"));
    } else if (normalizedRole == QStringLiteral("meta")) {
        label->setObjectName(QStringLiteral("metaBadge"));
    } else {
        label->setObjectName(QStringLiteral("tagChip"));
    }
    label->setVisible(!text.trimmed().isEmpty());
    return label;
}

}  // namespace

FavoriteItemCard::FavoriteItemCard(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("contentCard"));
    setProperty("cardRole", QStringLiteral("favoriteContent"));
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);

    setProperty("hovered", false);
    setProperty("pressed", false);
    setProperty("focused", false);

    setupUi();
    setupConnections();
}

void FavoriteItemCard::setData(const FavoriteItemViewData& item)
{
    data_.conclusionId = item.conclusionId.trimmed();
    data_.title = collapseWhitespace(item.title);
    data_.module = collapseWhitespace(item.module);
    data_.tags.clear();
    data_.tags.reserve(item.tags.size());
    for (const QString& tag : item.tags) {
        const QString normalizedTag = collapseWhitespace(tag);
        if (!normalizedTag.isEmpty()) {
            data_.tags.push_back(normalizedTag);
        }
    }
    data_.difficulty = collapseWhitespace(item.difficulty);
    data_.summary = collapseWhitespace(item.summary);
    data_.favoriteTimeText = collapseWhitespace(item.favoriteTimeText);

    if (data_.title.isEmpty()) {
        data_.title = data_.conclusionId;
    }

    moduleLabel_->setVisible(!data_.module.isEmpty());
    moduleLabel_->setText(data_.module);

    summaryLabel_->setVisible(!data_.summary.isEmpty());
    favoriteTimeLabel_->setVisible(!data_.favoriteTimeText.isEmpty());
    favoriteTimeLabel_->setText(data_.favoriteTimeText);

    openButton_->setEnabled(!data_.conclusionId.isEmpty());
    unfavoriteButton_->setEnabled(!data_.conclusionId.isEmpty());

    titleLabel_->setToolTip(data_.title);
    summaryLabel_->setToolTip(data_.summary);

    rebuildChips();
    refreshTitleLabel();
    refreshSummaryLabel();
}

void FavoriteItemCard::enterEvent(QEnterEvent* event)
{
    updateStateProperty("hovered", true);
    QWidget::enterEvent(event);
}

void FavoriteItemCard::leaveEvent(QEvent* event)
{
    updateStateProperty("hovered", false);
    updateStateProperty("pressed", false);
    QWidget::leaveEvent(event);
}

void FavoriteItemCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        updateStateProperty("pressed", true);
    }
    QWidget::mousePressEvent(event);
}

void FavoriteItemCard::mouseReleaseEvent(QMouseEvent* event)
{
    const bool shouldOpen = event->button() == Qt::LeftButton && shouldTriggerOpen(event->pos());
    updateStateProperty("pressed", false);
    QWidget::mouseReleaseEvent(event);

    if (shouldOpen && !data_.conclusionId.isEmpty()) {
        setFocus(Qt::MouseFocusReason);
        emit openRequested(data_.conclusionId);
    }
}

void FavoriteItemCard::focusInEvent(QFocusEvent* event)
{
    updateStateProperty("focused", true);
    QWidget::focusInEvent(event);
}

void FavoriteItemCard::focusOutEvent(QFocusEvent* event)
{
    updateStateProperty("focused", false);
    QWidget::focusOutEvent(event);
}

void FavoriteItemCard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshTitleLabel();
    refreshSummaryLabel();
}

void FavoriteItemCard::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
        && !data_.conclusionId.isEmpty()) {
        emit openRequested(data_.conclusionId);
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void FavoriteItemCard::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(ui::style::tokens::kCardPaddingHorizontal,
                                   ui::style::tokens::kCardPaddingVertical,
                                   ui::style::tokens::kCardPaddingHorizontal,
                                   ui::style::tokens::kCardPaddingVertical);
    rootLayout->setSpacing(10);

    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("cardTitleLabel"));
    titleLabel_->setWordWrap(false);

    moduleLabel_ = new QLabel(this);
    moduleLabel_->setObjectName(QStringLiteral("cardMetaLabel"));
    moduleLabel_->setWordWrap(false);
    moduleLabel_->setVisible(false);

    chipsContainer_ = new QWidget(this);
    chipsContainer_->setObjectName(QStringLiteral("chipRow"));
    chipsLayout_ = new QHBoxLayout(chipsContainer_);
    chipsLayout_->setContentsMargins(0, 0, 0, 0);
    chipsLayout_->setSpacing(6);
    chipsContainer_->setVisible(false);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("cardSummaryLabel"));
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setVisible(false);

    auto* footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(0, 2, 0, 0);
    footerLayout->setSpacing(10);

    favoriteTimeLabel_ = new QLabel(this);
    favoriteTimeLabel_->setObjectName(QStringLiteral("subtleTextLabel"));
    favoriteTimeLabel_->setVisible(false);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(ui::style::tokens::kSmallSpacing);

    openButton_ = new QPushButton(QStringLiteral("打开详情"), this);
    openButton_->setObjectName(QStringLiteral("primaryButton"));
    openButton_->setProperty("buttonSize", QStringLiteral("compact"));
    openButton_->setCursor(Qt::PointingHandCursor);

    unfavoriteButton_ = new QPushButton(QStringLiteral("取消收藏"), this);
    unfavoriteButton_->setObjectName(QStringLiteral("weakDangerButton"));
    unfavoriteButton_->setProperty("buttonSize", QStringLiteral("compact"));
    unfavoriteButton_->setCursor(Qt::PointingHandCursor);

    actionLayout->addWidget(openButton_);
    actionLayout->addWidget(unfavoriteButton_);

    footerLayout->addWidget(favoriteTimeLabel_, 1, Qt::AlignVCenter);
    footerLayout->addLayout(actionLayout, 0);

    rootLayout->addWidget(titleLabel_);
    rootLayout->addWidget(moduleLabel_);
    rootLayout->addWidget(chipsContainer_);
    rootLayout->addWidget(summaryLabel_);
    rootLayout->addLayout(footerLayout);
}

void FavoriteItemCard::setupConnections()
{
    connect(openButton_, &QPushButton::clicked, this, [this]() {
        if (!data_.conclusionId.isEmpty()) {
            emit openRequested(data_.conclusionId);
        }
    });

    connect(unfavoriteButton_, &QPushButton::clicked, this, [this]() {
        if (!data_.conclusionId.isEmpty()) {
            emit unfavoriteRequested(data_.conclusionId);
        }
    });
}

void FavoriteItemCard::rebuildChips()
{
    while (QLayoutItem* item = chipsLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    int chipCount = 0;
    if (!data_.difficulty.isEmpty()) {
        chipsLayout_->addWidget(createChip(chipsContainer_, data_.difficulty, QStringLiteral("difficulty")));
        ++chipCount;
    }

    const int maxTagCount = 4;
    const int boundedTagCount = std::min(maxTagCount, static_cast<int>(data_.tags.size()));
    for (int i = 0; i < boundedTagCount; ++i) {
        chipsLayout_->addWidget(createChip(chipsContainer_, data_.tags.at(i), QStringLiteral("tag")));
        ++chipCount;
    }

    chipsLayout_->addStretch(1);
    chipsContainer_->setVisible(chipCount > 0);
}

void FavoriteItemCard::refreshTitleLabel()
{
    if (titleLabel_ == nullptr) {
        return;
    }

    const int availableWidth = std::max(100, titleLabel_->width());
    const QFontMetrics metrics(titleLabel_->font());
    titleLabel_->setText(metrics.elidedText(data_.title, Qt::ElideRight, availableWidth));
}

void FavoriteItemCard::refreshSummaryLabel()
{
    if (summaryLabel_ == nullptr || data_.summary.isEmpty()) {
        return;
    }

    const int availableWidth = std::max(120, summaryLabel_->width());
    const QFontMetrics metrics(summaryLabel_->font());
    summaryLabel_->setText(elideTextToLines(metrics, data_.summary, availableWidth, 2));
}

void FavoriteItemCard::updateStateProperty(const char* propertyName, bool value)
{
    const QVariant currentValue = property(propertyName);
    if (currentValue.isValid() && currentValue.toBool() == value) {
        return;
    }

    setProperty(propertyName, value);
    refreshStyle(this);
}

bool FavoriteItemCard::isActionButton(const QWidget* widget) const
{
    for (const QWidget* current = widget; current != nullptr; current = current->parentWidget()) {
        if (current == openButton_ || current == unfavoriteButton_) {
            return true;
        }
        if (current == this) {
            break;
        }
    }
    return false;
}

bool FavoriteItemCard::shouldTriggerOpen(const QPoint& pos) const
{
    const QWidget* target = childAt(pos);
    if (target == nullptr) {
        return true;
    }
    return !isActionButton(target);
}

void FavoriteItemCard::refreshStyle(QWidget* widget)
{
    if (widget == nullptr || widget->style() == nullptr) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
