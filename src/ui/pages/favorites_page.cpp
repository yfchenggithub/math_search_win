#include "ui/pages/favorites_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "infrastructure/data/conclusion_content_repository.h"
#include "infrastructure/data/conclusion_index_repository.h"
#include "ui/style/app_style.h"
#include "ui/widgets/favorites/favorite_item_card.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

QString firstNonEmpty(std::initializer_list<QString> values)
{
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return {};
}

QStringList normalizeTagList(const QStringList& rawTags)
{
    QStringList normalizedTags;
    normalizedTags.reserve(rawTags.size());

    QSet<QString> dedupe;
    dedupe.reserve(rawTags.size());

    for (const QString& tag : rawTags) {
        const QString normalized = tag.trimmed();
        if (normalized.isEmpty() || dedupe.contains(normalized)) {
            continue;
        }
        dedupe.insert(normalized);
        normalizedTags.push_back(normalized);
    }

    return normalizedTags;
}

}  // namespace

FavoritesPage::FavoritesPage(const infrastructure::data::ConclusionContentRepository* contentRepository,
                             const infrastructure::data::ConclusionIndexRepository* indexRepository,
                             QWidget* parent)
    : QWidget(parent), contentRepository_(contentRepository), indexRepository_(indexRepository)
{
    ui::style::ensureAppStyleSheetLoaded();
    setObjectName(QStringLiteral("favoritesPage"));
    setProperty("pageRole", QStringLiteral("favorites"));
    setupUi();
    setupConnections();
    reloadData();

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("page constructed name=favorites mode=content_list_ui"));
}

void FavoritesPage::reloadData()
{
    const bool loaded = favoritesRepository_.load();
    if (!loaded) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("favorites_page load favorites failed fallback=memory"));
    }

    rebuildFavoriteTimestampIndex();

    const QStringList favoriteIds = favoritesRepository_.allIds();
    items_.clear();
    items_.reserve(favoriteIds.size());

    for (int i = 0; i < favoriteIds.size(); ++i) {
        FavoriteItem item = buildItemFromId(favoriteIds.at(i), i);
        if (!item.conclusionId.isEmpty()) {
            items_.push_back(std::move(item));
        }
    }

    applySort();
    rebuildCards();
    updateEmptyState();
}

void FavoritesPage::setupUi()
{
    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin);
    rootLayout_->setSpacing(ui::style::tokens::kPageSectionSpacing);

    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName(QStringLiteral("pageHeader"));
    auto* headerLayout = new QVBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    titleLabel_ = new QLabel(QStringLiteral("我的收藏"), headerWidget_);
    titleLabel_->setObjectName(QStringLiteral("pageTitleLabel"));

    subtitleLabel_ = new QLabel(QStringLiteral("沉淀常用二级结论，形成稳定可复习的知识资产。"), headerWidget_);
    subtitleLabel_->setObjectName(QStringLiteral("pageSubtitleLabel"));

    headerLayout->addWidget(titleLabel_);
    headerLayout->addWidget(subtitleLabel_);
    rootLayout_->addWidget(headerWidget_);

    toolbarWidget_ = new QWidget(this);
    toolbarWidget_->setObjectName(QStringLiteral("pageToolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbarWidget_);
    toolbarLayout->setContentsMargins(14, 12, 14, 12);
    toolbarLayout->setSpacing(10);

    summaryLabel_ = new QLabel(QStringLiteral("共 0 条收藏"), toolbarWidget_);
    summaryLabel_->setObjectName(QStringLiteral("pageSummaryLabel"));

    sortComboBox_ = new QComboBox(toolbarWidget_);
    sortComboBox_->setObjectName(QStringLiteral("toolbarSortCombo"));
    sortComboBox_->addItem(QStringLiteral("最近收藏"), static_cast<int>(SortMode::RecentFirst));
    sortComboBox_->addItem(QStringLiteral("按标题"), static_cast<int>(SortMode::TitleAsc));
    sortComboBox_->addItem(QStringLiteral("按模块"), static_cast<int>(SortMode::ModuleAsc));

    filterButton_ = new QPushButton(QStringLiteral("筛选（即将支持）"), toolbarWidget_);
    filterButton_->setObjectName(QStringLiteral("toolbarButton"));
    filterButton_->setCursor(Qt::PointingHandCursor);

    toolbarLayout->addWidget(summaryLabel_);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(sortComboBox_, 0);
    toolbarLayout->addWidget(filterButton_, 0);

    rootLayout_->addWidget(toolbarWidget_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("pageScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    cardsContainer_ = new QWidget(scrollArea_);
    cardsContainer_->setObjectName(QStringLiteral("pageContentContainer"));
    cardsLayout_ = new QVBoxLayout(cardsContainer_);
    cardsLayout_->setContentsMargins(0, 2, 0, 2);
    cardsLayout_->setSpacing(ui::style::tokens::kCardSpacing);
    cardsLayout_->addStretch(1);

    scrollArea_->setWidget(cardsContainer_);
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
    emptyCard->setProperty("emptyVariant", QStringLiteral("favorites"));

    auto* emptyCardLayout = new QVBoxLayout(emptyCard);
    emptyCardLayout->setContentsMargins(ui::style::tokens::kEmptyCardPaddingHorizontal + 2,
                                        ui::style::tokens::kEmptyCardPaddingVertical,
                                        ui::style::tokens::kEmptyCardPaddingHorizontal + 2,
                                        ui::style::tokens::kEmptyCardPaddingVertical);
    emptyCardLayout->setSpacing(10);

    auto* emptyTitleLabel = new QLabel(QStringLiteral("还没有收藏的结论"), emptyCard);
    emptyTitleLabel->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyTitleLabel->setAlignment(Qt::AlignCenter);

    auto* emptyDescriptionLabel = new QLabel(QStringLiteral("把常用二级结论加入收藏，后续复习会更方便。"), emptyCard);
    emptyDescriptionLabel->setObjectName(QStringLiteral("emptyStateDescription"));
    emptyDescriptionLabel->setAlignment(Qt::AlignCenter);
    emptyDescriptionLabel->setWordWrap(true);

    emptyActionButton_ = new QPushButton(QStringLiteral("去搜索"), emptyCard);
    emptyActionButton_->setObjectName(QStringLiteral("emptyStatePrimaryButton"));
    emptyActionButton_->setCursor(Qt::PointingHandCursor);

    emptyCardLayout->addWidget(emptyTitleLabel);
    emptyCardLayout->addWidget(emptyDescriptionLabel);
    emptyCardLayout->addSpacing(ui::style::tokens::kSmallSpacing);
    emptyCardLayout->addWidget(emptyActionButton_, 0, Qt::AlignHCenter);

    emptyRootLayout->addWidget(emptyCard, 0, Qt::AlignHCenter);
    emptyRootLayout->addStretch(3);

    rootLayout_->addWidget(emptyStateWidget_, 1);
}

void FavoritesPage::setupConnections()
{
    connect(sortComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applySort();
        rebuildCards();
        updateEmptyState();
    });

    connect(filterButton_, &QPushButton::clicked, this, []() {});

    connect(emptyActionButton_, &QPushButton::clicked, this, &FavoritesPage::navigateToSearchRequested);
}

void FavoritesPage::applySort()
{
    const SortMode sortMode = static_cast<SortMode>(sortComboBox_->currentData().toInt());

    auto textLess = [](const QString& lhs, const QString& rhs) {
        const int compare = lhs.compare(rhs, Qt::CaseInsensitive);
        return compare == 0 ? lhs < rhs : compare < 0;
    };

    switch (sortMode) {
    case SortMode::RecentFirst:
        std::sort(items_.begin(), items_.end(), [&textLess](const FavoriteItem& lhs, const FavoriteItem& rhs) {
            const bool lhsHasTime = lhs.favoriteAt.isValid();
            const bool rhsHasTime = rhs.favoriteAt.isValid();
            if (lhsHasTime != rhsHasTime) {
                return lhsHasTime;
            }

            if (lhsHasTime && rhsHasTime && lhs.favoriteAt != rhs.favoriteAt) {
                return lhs.favoriteAt > rhs.favoriteAt;
            }

            if (lhs.sourceOrder != rhs.sourceOrder) {
                return lhs.sourceOrder > rhs.sourceOrder;
            }

            if (!lhs.title.isEmpty() || !rhs.title.isEmpty()) {
                return textLess(lhs.title, rhs.title);
            }
            return textLess(lhs.conclusionId, rhs.conclusionId);
        });
        return;

    case SortMode::TitleAsc:
        std::sort(items_.begin(), items_.end(), [&textLess](const FavoriteItem& lhs, const FavoriteItem& rhs) {
            if (lhs.title != rhs.title) {
                return textLess(lhs.title, rhs.title);
            }
            return textLess(lhs.conclusionId, rhs.conclusionId);
        });
        return;

    case SortMode::ModuleAsc:
        std::sort(items_.begin(), items_.end(), [&textLess](const FavoriteItem& lhs, const FavoriteItem& rhs) {
            const QString lhsModule = lhs.module.trimmed().isEmpty() ? QStringLiteral("~") : lhs.module;
            const QString rhsModule = rhs.module.trimmed().isEmpty() ? QStringLiteral("~") : rhs.module;
            if (lhsModule != rhsModule) {
                return textLess(lhsModule, rhsModule);
            }
            if (lhs.title != rhs.title) {
                return textLess(lhs.title, rhs.title);
            }
            return textLess(lhs.conclusionId, rhs.conclusionId);
        });
        return;
    }
}

void FavoritesPage::rebuildCards()
{
    clearCards();

    for (const FavoriteItem& item : std::as_const(items_)) {
        FavoriteItemViewData cardData;
        cardData.conclusionId = item.conclusionId;
        cardData.title = item.title;
        cardData.module = item.module;
        cardData.tags = item.tags;
        cardData.difficulty = item.difficultyText;
        cardData.summary = item.summaryText;
        cardData.favoriteTimeText = item.favoriteTimeText;

        auto* card = new FavoriteItemCard(cardsContainer_);
        card->setData(cardData);

        connect(card, &FavoriteItemCard::openRequested, this, &FavoritesPage::openConclusionRequested);
        connect(card, &FavoriteItemCard::unfavoriteRequested, this, [this](const QString& conclusionId) {
            if (conclusionId.trimmed().isEmpty()) {
                return;
            }
            favoritesRepository_.remove(conclusionId);
            reloadData();
            emit favoritesChanged();
        });

        cardsLayout_->addWidget(card);
    }

    cardsLayout_->addStretch(1);
}

void FavoritesPage::clearCards()
{
    if (cardsLayout_ == nullptr) {
        return;
    }

    while (QLayoutItem* item = cardsLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void FavoritesPage::updateEmptyState()
{
    const bool hasItems = !items_.isEmpty();
    scrollArea_->setVisible(hasItems);
    emptyStateWidget_->setVisible(!hasItems);
    sortComboBox_->setEnabled(hasItems);

    if (!hasItems) {
        summaryLabel_->setText(QStringLiteral("共 0 条收藏"));
        return;
    }

    QDateTime latestFavoriteTime;
    for (const FavoriteItem& item : std::as_const(items_)) {
        if (!item.favoriteAt.isValid()) {
            continue;
        }
        if (!latestFavoriteTime.isValid() || item.favoriteAt > latestFavoriteTime) {
            latestFavoriteTime = item.favoriteAt;
        }
    }

    QString summary = QStringLiteral("共 %1 条收藏").arg(items_.size());
    const QString latestTimeText = ui::style::formatRelativeDateTime(latestFavoriteTime);
    if (!latestTimeText.isEmpty()) {
        summary.append(QStringLiteral(" · 最近收藏 %1").arg(latestTimeText));
    }
    summaryLabel_->setText(summary);
}

void FavoritesPage::rebuildFavoriteTimestampIndex()
{
    favoriteTimestampById_.clear();

    bool ok = false;
    const QJsonDocument document = localStorageService_.readJsonFile(localStorageService_.favoritesFilePath(), &ok);
    if (!ok || !document.isObject()) {
        return;
    }

    const QJsonObject root = document.object();
    const QJsonArray itemsArray = root.value(QStringLiteral("items")).toArray();

    for (const QJsonValue& value : itemsArray) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        const QString id = normalizedText(object.value(QStringLiteral("id")).toVariant().toString());
        if (id.isEmpty()) {
            continue;
        }

        QDateTime candidate = parseDateTime(object.value(QStringLiteral("favoritedAt")).toVariant().toString());
        if (!candidate.isValid()) {
            candidate = parseDateTime(object.value(QStringLiteral("createdAt")).toVariant().toString());
        }
        if (!candidate.isValid()) {
            candidate = parseDateTime(object.value(QStringLiteral("updatedAt")).toVariant().toString());
        }
        if (!candidate.isValid()) {
            continue;
        }

        const auto existing = favoriteTimestampById_.constFind(id);
        if (existing == favoriteTimestampById_.constEnd() || candidate > existing.value()) {
            favoriteTimestampById_.insert(id, candidate);
        }
    }
}

FavoritesPage::FavoriteItem FavoritesPage::buildItemFromId(const QString& conclusionId, int sourceOrder) const
{
    FavoriteItem item;
    item.conclusionId = normalizedText(conclusionId);
    item.sourceOrder = sourceOrder;
    if (item.conclusionId.isEmpty()) {
        return item;
    }

    const auto* record = contentRepository_ == nullptr ? nullptr : contentRepository_->getById(item.conclusionId);
    const auto* doc = indexRepository_ == nullptr ? nullptr : indexRepository_->getDocById(item.conclusionId);

    item.title = firstNonEmpty(
        {record == nullptr ? QString() : record->meta.title, doc == nullptr ? QString() : doc->title, item.conclusionId});
    item.module = firstNonEmpty(
        {record == nullptr ? QString() : record->identity.module, doc == nullptr ? QString() : doc->module});

    if (record != nullptr && !record->meta.tags.isEmpty()) {
        item.tags = normalizeTagList(record->meta.tags);
    } else if (doc != nullptr) {
        item.tags = normalizeTagList(doc->tags);
    }

    double difficulty = 0.0;
    if (record != nullptr && record->meta.difficulty > 0) {
        difficulty = static_cast<double>(record->meta.difficulty);
    } else if (doc != nullptr && doc->difficulty > 0.0) {
        difficulty = doc->difficulty;
    }
    item.difficultyText = ui::style::formatDifficultyText(difficulty);

    const QString statementCandidate = record == nullptr ? QString() : record->content.plain.statement;
    const QString summaryCandidate =
        firstNonEmpty({record == nullptr ? QString() : record->content.plain.summary,
                       record == nullptr ? QString() : record->meta.summary,
                       doc == nullptr ? QString() : doc->summary});
    item.summaryText = preferredSummary(statementCandidate, summaryCandidate);

    const auto it = favoriteTimestampById_.constFind(item.conclusionId);
    if (it != favoriteTimestampById_.constEnd()) {
        item.favoriteAt = it.value();
        item.favoriteTimeText = ui::style::formatRelativeDateTime(item.favoriteAt);
    }

    return item;
}

QString FavoritesPage::preferredSummary(const QString& statement, const QString& summaryCandidate)
{
    const QString normalizedStatement = normalizedText(statement);
    const QString normalizedSummary = normalizedText(summaryCandidate);

    if (normalizedStatement.isEmpty()) {
        return normalizedSummary;
    }
    if (normalizedSummary.isEmpty()) {
        return normalizedStatement;
    }

    if (normalizedStatement.size() <= 96) {
        return normalizedStatement;
    }
    if (normalizedSummary.size() <= normalizedStatement.size()) {
        return normalizedSummary;
    }
    if (normalizedStatement.size() <= 180) {
        return normalizedStatement;
    }
    return normalizedSummary;
}

QString FavoritesPage::normalizedText(const QString& text)
{
    QString output;
    output.reserve(text.size());

    bool lastWasSpace = false;
    for (const QChar ch : text) {
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

QDateTime FavoritesPage::parseDateTime(const QString& rawText)
{
    const QString text = rawText.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    bool epochOk = false;
    const qint64 epochValue = text.toLongLong(&epochOk);
    if (epochOk) {
        if (epochValue > 1000000000000LL) {
            return QDateTime::fromMSecsSinceEpoch(epochValue).toLocalTime();
        }
        if (epochValue > 1000000000LL) {
            return QDateTime::fromSecsSinceEpoch(epochValue).toLocalTime();
        }
    }

    const QList<Qt::DateFormat> dateFormats = {Qt::ISODateWithMs, Qt::ISODate};
    for (const Qt::DateFormat format : dateFormats) {
        QDateTime dateTime = QDateTime::fromString(text, format);
        if (!dateTime.isValid()) {
            continue;
        }
        return dateTime.toLocalTime();
    }

    const QStringList fallbackFormats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"),
        QStringLiteral("yyyy/MM/dd HH:mm"),
    };

    for (const QString& format : fallbackFormats) {
        const QDateTime dateTime = QDateTime::fromString(text, format);
        if (dateTime.isValid()) {
            return dateTime;
        }
    }

    return {};
}
