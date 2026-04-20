#include "ui/pages/recent_searches_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "ui/widgets/recent_search_item_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString locateAppStylePath()
{
    auto tryFindInTree = [](const QString& startPath) -> QString {
        QDir dir(startPath);
        for (int depth = 0; depth < 10; ++depth) {
            const QString rootStyle = dir.filePath(QStringLiteral("app.qss"));
            if (QFileInfo::exists(rootStyle)) {
                return QDir::cleanPath(rootStyle);
            }

            const QString sourceStyle = dir.filePath(QStringLiteral("src/ui/style/app.qss"));
            if (QFileInfo::exists(sourceStyle)) {
                return QDir::cleanPath(sourceStyle);
            }

            if (!dir.cdUp()) {
                break;
            }
        }
        return {};
    };

    const QString fromAppDir = tryFindInTree(QCoreApplication::applicationDirPath());
    if (!fromAppDir.isEmpty()) {
        return fromAppDir;
    }

    return tryFindInTree(QDir::currentPath());
}

bool loadTextFile(const QString& path, QString* outText)
{
    if (outText == nullptr || path.trimmed().isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    *outText = QString::fromUtf8(file.readAll());
    return !outText->trimmed().isEmpty();
}

}  // namespace

RecentSearchesPage::RecentSearchesPage(QWidget* parent) : QWidget(parent)
{
    applyAppStyleSheetOnce();
    setupUi();
    setupConnections();
    reloadData();

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("page constructed name=recent_searches mode=timeline_ui"));
}

void RecentSearchesPage::setupUi()
{
    setObjectName(QStringLiteral("RecentSearchPage"));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(24, 24, 24, 24);
    rootLayout_->setSpacing(22);

    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName(QStringLiteral("recentHeaderWidget"));
    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(16);

    auto* titleBlock = new QWidget(headerWidget_);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);

    titleLabel_ = new QLabel(QStringLiteral("最近搜索"), titleBlock);
    titleLabel_->setObjectName(QStringLiteral("recentPageTitleLabel"));

    subtitleLabel_ = new QLabel(QStringLiteral("快速回访你最近搜索过的内容"), titleBlock);
    subtitleLabel_->setObjectName(QStringLiteral("recentPageSubtitleLabel"));

    summaryLabel_ = new QLabel(QStringLiteral("共 0 条"), titleBlock);
    summaryLabel_->setObjectName(QStringLiteral("recentSummaryLabel"));

    titleLayout->addWidget(titleLabel_);
    titleLayout->addWidget(subtitleLabel_);
    titleLayout->addWidget(summaryLabel_);

    clearAllButton_ = new QPushButton(QStringLiteral("清空历史"), headerWidget_);
    clearAllButton_->setObjectName(QStringLiteral("recentClearAllButton"));
    clearAllButton_->setCursor(Qt::PointingHandCursor);

    headerLayout->addWidget(titleBlock, 1);
    headerLayout->addWidget(clearAllButton_, 0, Qt::AlignTop);

    rootLayout_->addWidget(headerWidget_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("recentScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    listContainer_ = new QWidget(scrollArea_);
    listContainer_->setObjectName(QStringLiteral("recentListContainer"));
    listLayout_ = new QVBoxLayout(listContainer_);
    listLayout_->setContentsMargins(0, 2, 0, 2);
    listLayout_->setSpacing(12);
    listLayout_->addStretch(1);

    scrollArea_->setWidget(listContainer_);
    rootLayout_->addWidget(scrollArea_, 1);

    emptyStateWidget_ = new QWidget(this);
    emptyStateWidget_->setObjectName(QStringLiteral("recentEmptyStateWidget"));
    auto* emptyRootLayout = new QVBoxLayout(emptyStateWidget_);
    emptyRootLayout->setContentsMargins(0, 0, 0, 0);
    emptyRootLayout->setSpacing(0);
    emptyRootLayout->addStretch(2);

    auto* emptyCard = new QWidget(emptyStateWidget_);
    emptyCard->setObjectName(QStringLiteral("recentEmptyStateCard"));
    auto* emptyCardLayout = new QVBoxLayout(emptyCard);
    emptyCardLayout->setContentsMargins(30, 28, 30, 28);
    emptyCardLayout->setSpacing(10);

    emptyTitleLabel_ = new QLabel(QStringLiteral("还没有最近搜索"), emptyCard);
    emptyTitleLabel_->setObjectName(QStringLiteral("recentEmptyTitleLabel"));
    emptyTitleLabel_->setAlignment(Qt::AlignCenter);

    emptyDescriptionLabel_ =
        new QLabel(QStringLiteral("你搜索过的内容会出现在这里，方便快速回访"), emptyCard);
    emptyDescriptionLabel_->setObjectName(QStringLiteral("recentEmptyDescriptionLabel"));
    emptyDescriptionLabel_->setAlignment(Qt::AlignCenter);
    emptyDescriptionLabel_->setWordWrap(true);

    emptyActionButton_ = new QPushButton(QStringLiteral("去搜索"), emptyCard);
    emptyActionButton_->setObjectName(QStringLiteral("recentEmptyActionButton"));
    emptyActionButton_->setCursor(Qt::PointingHandCursor);

    emptyCardLayout->addWidget(emptyTitleLabel_);
    emptyCardLayout->addWidget(emptyDescriptionLabel_);
    emptyCardLayout->addSpacing(8);
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
        itemWidget->setTimeText(formatRelativeTime(item.searchedAt));

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
    const QString latestText = formatRelativeTime(items_.first().searchedAt);
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

QString RecentSearchesPage::formatRelativeTime(const QDateTime& timestamp)
{
    if (!timestamp.isValid()) {
        return QString();
    }

    const QDateTime localTime = timestamp.toLocalTime();
    const QDate eventDate = localTime.date();
    const QDate today = QDate::currentDate();

    if (eventDate == today) {
        return QStringLiteral("今天 %1").arg(localTime.time().toString(QStringLiteral("HH:mm")));
    }

    if (eventDate == today.addDays(-1)) {
        return QStringLiteral("昨天 %1").arg(localTime.time().toString(QStringLiteral("HH:mm")));
    }

    return localTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

bool RecentSearchesPage::applyAppStyleSheetOnce()
{
    if (qApp == nullptr) {
        return false;
    }

    const bool alreadyAttempted = qApp->property("math_search_app_qss_attempted").toBool();
    if (alreadyAttempted) {
        return qApp->property("math_search_app_qss_applied").toBool();
    }
    qApp->setProperty("math_search_app_qss_attempted", true);

    const QString stylePath = locateAppStylePath();
    QString styleText;
    if (stylePath.isEmpty() || !loadTextFile(stylePath, &styleText)) {
        LOG_WARN(LogCategory::UiMainWindow, QStringLiteral("app stylesheet missing path=app.qss"));
        qApp->setProperty("math_search_app_qss_applied", false);
        return false;
    }

    const QString existingStyle = qApp->styleSheet().trimmed();
    if (existingStyle.isEmpty()) {
        qApp->setStyleSheet(styleText);
    } else {
        qApp->setStyleSheet(existingStyle + QStringLiteral("\n\n") + styleText);
    }

    qApp->setProperty("math_search_app_qss_applied", true);
    qApp->setProperty("math_search_app_qss_path", stylePath);

    LOG_INFO(LogCategory::UiMainWindow, QStringLiteral("app stylesheet loaded path=%1").arg(stylePath));
    return true;
}
