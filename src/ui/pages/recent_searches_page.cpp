#include "ui/pages/recent_searches_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "ui/style/app_style.h"
#include "ui/widgets/recent_search_item_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

RecentSearchesPage::RecentSearchesPage(QWidget* parent) : QWidget(parent)
{
    ui::style::ensureAppStyleSheetLoaded();
    setupUi();
    setupConnections();
    reloadData();

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("page constructed name=recent_searches mode=timeline_ui"));
}

void RecentSearchesPage::setupUi()
{
    setObjectName(QStringLiteral("recentSearchPage"));
    setProperty("pageRole", QStringLiteral("recent"));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin);
    rootLayout_->setSpacing(ui::style::tokens::kPageSectionSpacing);

    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName(QStringLiteral("pageHeader"));
    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(16);

    auto* titleBlock = new QWidget(headerWidget_);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);

    titleLabel_ = new QLabel(QStringLiteral("最近搜索"), titleBlock);
    titleLabel_->setObjectName(QStringLiteral("pageTitleLabel"));

    subtitleLabel_ = new QLabel(QStringLiteral("快速回访你最近搜索过的内容"), titleBlock);
    subtitleLabel_->setObjectName(QStringLiteral("pageSubtitleLabel"));

    summaryLabel_ = new QLabel(QStringLiteral("共 0 条"), titleBlock);
    summaryLabel_->setObjectName(QStringLiteral("pageSummaryLabel"));

    titleLayout->addWidget(titleLabel_);
    titleLayout->addWidget(subtitleLabel_);
    titleLayout->addWidget(summaryLabel_);

    clearAllButton_ = new QPushButton(QStringLiteral("清空历史"), headerWidget_);
    clearAllButton_->setObjectName(QStringLiteral("secondaryButton"));
    clearAllButton_->setCursor(Qt::PointingHandCursor);

    headerLayout->addWidget(titleBlock, 1);
    headerLayout->addWidget(clearAllButton_, 0, Qt::AlignTop);

    rootLayout_->addWidget(headerWidget_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("pageScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    listContainer_ = new QWidget(scrollArea_);
    listContainer_->setObjectName(QStringLiteral("pageContentContainer"));
    listLayout_ = new QVBoxLayout(listContainer_);
    listLayout_->setContentsMargins(0, 2, 0, 2);
    listLayout_->setSpacing(ui::style::tokens::kCardSpacing);
    listLayout_->addStretch(1);

    scrollArea_->setWidget(listContainer_);
    rootLayout_->addWidget(scrollArea_, 1);

    emptyStateWidget_ = new QWidget(this);
    emptyStateWidget_->setObjectName(QStringLiteral("emptyState"));
    auto* emptyRootLayout = new QVBoxLayout(emptyStateWidget_);
    emptyRootLayout->setContentsMargins(0, 0, 0, 0);
    emptyRootLayout->setSpacing(0);
    emptyRootLayout->addStretch(2);

    auto* emptyCard = new QWidget(emptyStateWidget_);
    emptyCard->setObjectName(QStringLiteral("elevatedCard"));
    emptyCard->setProperty("cardRole", QStringLiteral("emptyState"));
    emptyCard->setProperty("emptyVariant", QStringLiteral("recent"));

    auto* emptyCardLayout = new QVBoxLayout(emptyCard);
    emptyCardLayout->setContentsMargins(ui::style::tokens::kEmptyCardPaddingHorizontal,
                                        ui::style::tokens::kEmptyCardPaddingVertical,
                                        ui::style::tokens::kEmptyCardPaddingHorizontal,
                                        ui::style::tokens::kEmptyCardPaddingVertical);
    emptyCardLayout->setSpacing(10);

    emptyTitleLabel_ = new QLabel(QStringLiteral("还没有最近搜索"), emptyCard);
    emptyTitleLabel_->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyTitleLabel_->setAlignment(Qt::AlignCenter);

    emptyDescriptionLabel_ =
        new QLabel(QStringLiteral("你搜索过的内容会出现在这里，方便快速回访"), emptyCard);
    emptyDescriptionLabel_->setObjectName(QStringLiteral("emptyStateDescription"));
    emptyDescriptionLabel_->setAlignment(Qt::AlignCenter);
    emptyDescriptionLabel_->setWordWrap(true);

    emptyActionButton_ = new QPushButton(QStringLiteral("去搜索"), emptyCard);
    emptyActionButton_->setObjectName(QStringLiteral("emptyStatePrimaryButton"));
    emptyActionButton_->setCursor(Qt::PointingHandCursor);

    emptyCardLayout->addWidget(emptyTitleLabel_);
    emptyCardLayout->addWidget(emptyDescriptionLabel_);
    emptyCardLayout->addSpacing(ui::style::tokens::kSmallSpacing);
    emptyCardLayout->addWidget(emptyActionButton_, 0, Qt::AlignHCenter);

    emptyRootLayout->addWidget(emptyCard, 0, Qt::AlignHCenter);
    emptyRootLayout->addStretch(3);

    rootLayout_->addWidget(emptyStateWidget_, 1);
}

void RecentSearchesPage::setupConnections()
{
    connect(clearAllButton_, &QPushButton::clicked, this, &RecentSearchesPage::handleClearAll);
    connect(emptyActionButton_, &QPushButton::clicked, this, &RecentSearchesPage::navigateToSearchRequested);
}

void RecentSearchesPage::reloadData()
{
    const bool loaded = historyRepository_.load();
    if (!loaded) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("recent_searches_page load history failed fallback=memory"));
    }

    items_ = historyRepository_.recentItems(historyRepository_.maxItems());
    rebuildList();
    updateEmptyState();
}

void RecentSearchesPage::rebuildList()
{
    clearListItems();

    for (const domain::models::SearchHistoryItem& item : items_) {
        auto* itemWidget = new RecentSearchItemWidget(listContainer_);
        itemWidget->setData(item);
        itemWidget->setEntryId(item.query.trimmed());
        itemWidget->setTimeText(ui::style::formatRelativeDateTime(item.searchedAt));

        const QString source = item.source.trimmed();
        if (!source.isEmpty() && source.compare(QStringLiteral("manual"), Qt::CaseInsensitive) != 0) {
            itemWidget->setMetaText(QStringLiteral("来源：%1").arg(source));
        } else {
            itemWidget->setMetaText(QString());
        }

        connect(itemWidget, &RecentSearchItemWidget::reSearchClicked, this, &RecentSearchesPage::handleSearchAgain);
        connect(itemWidget, &RecentSearchItemWidget::deleteClicked, this, &RecentSearchesPage::handleItemDelete);

        listLayout_->addWidget(itemWidget);
    }

    listLayout_->addStretch(1);
}

void RecentSearchesPage::clearListItems()
{
    if (listLayout_ == nullptr) {
        return;
    }

    while (QLayoutItem* item = listLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void RecentSearchesPage::updateEmptyState()
{
    const bool hasItems = !items_.isEmpty();

    scrollArea_->setVisible(hasItems);
    emptyStateWidget_->setVisible(!hasItems);
    clearAllButton_->setEnabled(hasItems);

    if (!hasItems) {
        summaryLabel_->setText(QStringLiteral("共 0 条"));
        return;
    }

    QString summaryText = QStringLiteral("共 %1 条").arg(items_.size());
    const QString latestText = ui::style::formatRelativeDateTime(items_.first().searchedAt);
    if (!latestText.isEmpty()) {
        summaryText.append(QStringLiteral(" · 最近一条 %1").arg(latestText));
    }
    summaryLabel_->setText(summaryText);
}

void RecentSearchesPage::handleClearAll()
{
    if (items_.isEmpty()) {
        return;
    }

    historyRepository_.clear();
    reloadData();
    emit historyChanged();
}

void RecentSearchesPage::handleItemDelete(const QString& entryId)
{
    if (historyRepository_.removeQuery(entryId)) {
        reloadData();
        emit historyChanged();
    }
}

void RecentSearchesPage::handleSearchAgain(const QString& query)
{
    const QString normalizedQuery = query.trimmed();
    if (normalizedQuery.isEmpty()) {
        return;
    }

    historyRepository_.addQuery(normalizedQuery, QStringLiteral("recent"));
    reloadData();
    emit historyChanged();
    emit searchRequested(normalizedQuery);
}
