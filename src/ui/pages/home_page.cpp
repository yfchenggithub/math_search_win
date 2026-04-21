#include "ui/pages/home_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "infrastructure/data/conclusion_index_repository.h"
#include "shared/constants.h"
#include "ui/style/app_style.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int kPreviewLimit = 3;

QPushButton* createQuickActionCard(QWidget* parent,
                                   const QString& title,
                                   const QString& description,
                                   QLabel** descriptionLabelOut,
                                   bool weakEntry)
{
    auto* card = new QPushButton(parent);
    card->setObjectName(QStringLiteral("homeQuickActionCard"));
    card->setCursor(Qt::PointingHandCursor);
    card->setText(QString());
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card->setMinimumHeight(96);
    card->setProperty("weakEntry", weakEntry);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(5);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("homeQuickActionTitle"));
    titleLabel->setWordWrap(true);

    auto* descriptionLabel = new QLabel(description, card);
    descriptionLabel->setObjectName(QStringLiteral("homeQuickActionDescription"));
    descriptionLabel->setWordWrap(true);

    cardLayout->addWidget(titleLabel);
    cardLayout->addWidget(descriptionLabel);
    cardLayout->addStretch(1);

    if (descriptionLabelOut != nullptr) {
        *descriptionLabelOut = descriptionLabel;
    }
    return card;
}

QPushButton* createPreviewButton(QWidget* parent, const QString& title, const QString& meta)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("homePreviewItem"));
    button->setCursor(Qt::PointingHandCursor);
    button->setText(QString());
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    button->setMinimumHeight(66);

    auto* layout = new QVBoxLayout(button);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(title, button);
    titleLabel->setObjectName(QStringLiteral("homePreviewItemTitle"));
    titleLabel->setWordWrap(true);

    auto* metaLabel = new QLabel(meta, button);
    metaLabel->setObjectName(QStringLiteral("homePreviewItemMeta"));
    metaLabel->setWordWrap(true);

    layout->addWidget(titleLabel);
    layout->addWidget(metaLabel);

    return button;
}

QWidget* createPreviewEmptyItem(QWidget* parent, const QString& title, const QString& meta)
{
    auto* item = new QWidget(parent);
    item->setObjectName(QStringLiteral("homePreviewItem"));
    item->setProperty("empty", true);
    item->setAttribute(Qt::WA_StyledBackground, true);
    item->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(item);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(title, item);
    titleLabel->setObjectName(QStringLiteral("homePreviewItemTitle"));
    titleLabel->setWordWrap(true);

    auto* metaLabel = new QLabel(meta, item);
    metaLabel->setObjectName(QStringLiteral("homePreviewItemMeta"));
    metaLabel->setWordWrap(true);

    layout->addWidget(titleLabel);
    layout->addWidget(metaLabel);
    return item;
}

}  // namespace

HomePage::HomePage(const infrastructure::data::ConclusionIndexRepository* indexRepository, QWidget* parent)
    : QWidget(parent), indexRepository_(indexRepository)
{
    ui::style::ensureAppStyleSheetLoaded();
    setupUi();
    setupConnections();
    reloadData();

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("page constructed name=home mode=entry_redesign"));
}

void HomePage::setupUi()
{
    setObjectName(QStringLiteral("homePage"));
    setProperty("pageRole", QStringLiteral("home"));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout_->setSpacing(0);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("pageScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    contentWidget_ = new QWidget(scrollArea_);
    contentWidget_->setObjectName(QStringLiteral("homePageContent"));

    contentLayout_ = new QVBoxLayout(contentWidget_);
    contentLayout_->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                       ui::style::tokens::kPageOuterMargin,
                                       ui::style::tokens::kPageOuterMargin,
                                       ui::style::tokens::kPageOuterMargin);
    contentLayout_->setSpacing(16);

    setupHeroSection();
    setupQuickActionsSection();
    setupPreviewSections();
    setupFooterSection();
    contentLayout_->addStretch(1);

    scrollArea_->setWidget(contentWidget_);
    rootLayout_->addWidget(scrollArea_);
}

void HomePage::setupHeroSection()
{
    heroWidget_ = new QWidget(contentWidget_);
    heroWidget_->setObjectName(QStringLiteral("homeHero"));
    heroWidget_->setAttribute(Qt::WA_StyledBackground, true);

    auto* heroLayout = new QVBoxLayout(heroWidget_);
    heroLayout->setContentsMargins(28, 24, 28, 24);
    heroLayout->setSpacing(10);

    titleLabel_ = new QLabel(QStringLiteral("高中数学二级结论搜索系统"), heroWidget_);
    titleLabel_->setObjectName(QStringLiteral("homeHeroTitle"));

    subtitleLabel_ = new QLabel(
        QStringLiteral("本地离线使用，3 秒定位结论。先点“立即搜索”，再从最近搜索与收藏快速回访。"),
        heroWidget_);
    subtitleLabel_->setObjectName(QStringLiteral("homeHeroSubtitle"));
    subtitleLabel_->setWordWrap(true);

    auto* actionsRow = new QHBoxLayout();
    actionsRow->setContentsMargins(0, 6, 0, 0);
    actionsRow->setSpacing(10);

    startSearchButton_ = new QPushButton(QStringLiteral("立即搜索"), heroWidget_);
    startSearchButton_->setObjectName(QStringLiteral("homeHeroPrimaryButton"));
    startSearchButton_->setCursor(Qt::PointingHandCursor);

    recentShortcutButton_ = new QPushButton(QStringLiteral("最近搜索"), heroWidget_);
    recentShortcutButton_->setObjectName(QStringLiteral("homeHeroSecondaryButton"));
    recentShortcutButton_->setCursor(Qt::PointingHandCursor);

    favoritesShortcutButton_ = new QPushButton(QStringLiteral("我的收藏"), heroWidget_);
    favoritesShortcutButton_->setObjectName(QStringLiteral("homeHeroSecondaryButton"));
    favoritesShortcutButton_->setCursor(Qt::PointingHandCursor);

    actionsRow->addWidget(startSearchButton_, 0);
    actionsRow->addWidget(recentShortcutButton_, 0);
    actionsRow->addWidget(favoritesShortcutButton_, 0);
    actionsRow->addStretch(1);

    heroLayout->addWidget(titleLabel_);
    heroLayout->addWidget(subtitleLabel_);
    heroLayout->addLayout(actionsRow);

    contentLayout_->addWidget(heroWidget_);
}

void HomePage::setupQuickActionsSection()
{
    quickActionsWidget_ = new QWidget(contentWidget_);
    quickActionsWidget_->setObjectName(QStringLiteral("homeQuickActions"));

    auto* gridLayout = new QGridLayout(quickActionsWidget_);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setHorizontalSpacing(12);
    gridLayout->setVerticalSpacing(12);

    recentQuickActionCard_ = createQuickActionCard(quickActionsWidget_,
                                                   QStringLiteral("最近搜索"),
                                                   QStringLiteral("快速回看近期检索关键词"),
                                                   &recentQuickActionDescription_,
                                                   false);

    favoritesQuickActionCard_ = createQuickActionCard(quickActionsWidget_,
                                                      QStringLiteral("我的收藏"),
                                                      QStringLiteral("继续阅读已收藏的高频结论"),
                                                      &favoritesQuickActionDescription_,
                                                      false);

    settingsQuickActionCard_ = createQuickActionCard(quickActionsWidget_,
                                                     QStringLiteral("设置"),
                                                     QStringLiteral("调整本地参数与使用偏好"),
                                                     &settingsQuickActionDescription_,
                                                     true);

    gridLayout->addWidget(recentQuickActionCard_, 0, 0);
    gridLayout->addWidget(favoritesQuickActionCard_, 0, 1);
    gridLayout->addWidget(settingsQuickActionCard_, 0, 2);
    gridLayout->setColumnStretch(0, 1);
    gridLayout->setColumnStretch(1, 1);
    gridLayout->setColumnStretch(2, 1);

    contentLayout_->addWidget(quickActionsWidget_);
}

void HomePage::setupPreviewSections()
{
    auto* sectionsRow = new QWidget(contentWidget_);
    auto* rowLayout = new QHBoxLayout(sectionsRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(12);

    recentPreviewSection_ = new QWidget(sectionsRow);
    recentPreviewSection_->setObjectName(QStringLiteral("homePreviewSection"));
    recentPreviewSection_->setAttribute(Qt::WA_StyledBackground, true);
    auto* recentLayout = new QVBoxLayout(recentPreviewSection_);
    recentLayout->setContentsMargins(16, 14, 16, 14);
    recentLayout->setSpacing(10);

    auto* recentHeaderRow = new QHBoxLayout();
    recentHeaderRow->setContentsMargins(0, 0, 0, 0);
    recentHeaderRow->setSpacing(8);

    auto* recentTitle = new QLabel(QStringLiteral("最近搜索预览"), recentPreviewSection_);
    recentTitle->setObjectName(QStringLiteral("homeSectionTitle"));
    viewAllRecentButton_ = new QPushButton(QStringLiteral("查看全部"), recentPreviewSection_);
    viewAllRecentButton_->setObjectName(QStringLiteral("homeSectionActionButton"));
    viewAllRecentButton_->setCursor(Qt::PointingHandCursor);

    recentHeaderRow->addWidget(recentTitle, 1);
    recentHeaderRow->addWidget(viewAllRecentButton_, 0);

    recentPreviewLayout_ = new QVBoxLayout();
    recentPreviewLayout_->setContentsMargins(0, 0, 0, 0);
    recentPreviewLayout_->setSpacing(8);

    recentLayout->addLayout(recentHeaderRow);
    recentLayout->addLayout(recentPreviewLayout_);

    favoritesPreviewSection_ = new QWidget(sectionsRow);
    favoritesPreviewSection_->setObjectName(QStringLiteral("homePreviewSection"));
    favoritesPreviewSection_->setAttribute(Qt::WA_StyledBackground, true);
    auto* favoritesLayout = new QVBoxLayout(favoritesPreviewSection_);
    favoritesLayout->setContentsMargins(16, 14, 16, 14);
    favoritesLayout->setSpacing(10);

    auto* favoritesHeaderRow = new QHBoxLayout();
    favoritesHeaderRow->setContentsMargins(0, 0, 0, 0);
    favoritesHeaderRow->setSpacing(8);

    auto* favoritesTitle = new QLabel(QStringLiteral("收藏预览"), favoritesPreviewSection_);
    favoritesTitle->setObjectName(QStringLiteral("homeSectionTitle"));
    viewAllFavoritesButton_ = new QPushButton(QStringLiteral("查看全部"), favoritesPreviewSection_);
    viewAllFavoritesButton_->setObjectName(QStringLiteral("homeSectionActionButton"));
    viewAllFavoritesButton_->setCursor(Qt::PointingHandCursor);

    favoritesHeaderRow->addWidget(favoritesTitle, 1);
    favoritesHeaderRow->addWidget(viewAllFavoritesButton_, 0);

    favoritesPreviewLayout_ = new QVBoxLayout();
    favoritesPreviewLayout_->setContentsMargins(0, 0, 0, 0);
    favoritesPreviewLayout_->setSpacing(8);

    favoritesLayout->addLayout(favoritesHeaderRow);
    favoritesLayout->addLayout(favoritesPreviewLayout_);

    rowLayout->addWidget(recentPreviewSection_, 1);
    rowLayout->addWidget(favoritesPreviewSection_, 1);
    contentLayout_->addWidget(sectionsRow);
}

void HomePage::setupFooterSection()
{
    footerWidget_ = new QWidget(contentWidget_);
    auto* footerLayout = new QHBoxLayout(footerWidget_);
    footerLayout->setContentsMargins(2, 2, 2, 0);
    footerLayout->setSpacing(10);

    footerMetaLabel_ = new QLabel(QStringLiteral("本地离线模式"), footerWidget_);
    footerMetaLabel_->setObjectName(QStringLiteral("homeFooterMeta"));

    activationFooterButton_ = new QPushButton(QStringLiteral("激活与升级"), footerWidget_);
    activationFooterButton_->setObjectName(QStringLiteral("homeSectionActionButton"));
    activationFooterButton_->setCursor(Qt::PointingHandCursor);

    footerLayout->addWidget(footerMetaLabel_, 1);
    footerLayout->addWidget(activationFooterButton_, 0);

    contentLayout_->addWidget(footerWidget_);
}

void HomePage::setupConnections()
{
    connect(startSearchButton_, &QPushButton::clicked, this, &HomePage::navigateToSearchRequested);
    connect(recentShortcutButton_, &QPushButton::clicked, this, &HomePage::navigateToRecentRequested);
    connect(favoritesShortcutButton_, &QPushButton::clicked, this, &HomePage::navigateToFavoritesRequested);

    connect(recentQuickActionCard_, &QPushButton::clicked, this, &HomePage::navigateToRecentRequested);
    connect(favoritesQuickActionCard_, &QPushButton::clicked, this, &HomePage::navigateToFavoritesRequested);
    connect(settingsQuickActionCard_, &QPushButton::clicked, this, &HomePage::navigateToSettingsRequested);
    connect(activationFooterButton_, &QPushButton::clicked, this, &HomePage::navigateToActivationRequested);

    connect(viewAllRecentButton_, &QPushButton::clicked, this, &HomePage::navigateToRecentRequested);
    connect(viewAllFavoritesButton_, &QPushButton::clicked, this, &HomePage::navigateToFavoritesRequested);
}

void HomePage::reloadData()
{
    if (!historyRepository_.load()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("home_page load history failed fallback=memory"));
    }
    recentItemCount_ = historyRepository_.count();
    recentItems_ = historyRepository_.recentItems(kPreviewLimit);

    if (!favoritesRepository_.load()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("home_page load favorites failed fallback=memory"));
    }
    const QStringList favoriteIds = favoritesRepository_.allIds();
    favoriteItemCount_ = favoriteIds.size();

    favoriteItems_.clear();
    favoriteItems_.reserve(std::min<int>(kPreviewLimit, static_cast<int>(favoriteIds.size())));
    for (int i = 0; i < favoriteIds.size() && favoriteItems_.size() < kPreviewLimit; ++i) {
        const QString id = favoriteIds.at(i).trimmed();
        if (id.isEmpty()) {
            continue;
        }

        FavoritePreviewItem item;
        item.conclusionId = id;
        item.title = id;

        if (indexRepository_ != nullptr) {
            const auto* doc = indexRepository_->getDocById(id);
            if (doc != nullptr) {
                if (!doc->title.trimmed().isEmpty()) {
                    item.title = doc->title.trimmed();
                }
                item.module = doc->module.trimmed();
            }
        }

        favoriteItems_.push_back(std::move(item));
    }

    updateQuickActionSummary();
    rebuildRecentPreview();
    rebuildFavoritesPreview();
    updateActivationSummaryIfNeeded();
}

void HomePage::rebuildRecentPreview()
{
    clearLayoutItems(recentPreviewLayout_);

    if (recentItems_.isEmpty()) {
        recentPreviewLayout_->addWidget(
            createPreviewEmptyItem(recentPreviewSection_,
                                   QStringLiteral("暂无最近搜索"),
                                   QStringLiteral("先进行一次搜索，这里会展示最近 3 条关键词。")));
        return;
    }

    bool hasRenderableItems = false;
    for (const domain::models::SearchHistoryItem& item : std::as_const(recentItems_)) {
        const QString query = item.query.trimmed();
        if (query.isEmpty()) {
            continue;
        }

        QString meta = ui::style::formatRelativeDateTime(item.searchedAt);
        if (meta.isEmpty()) {
            meta = QStringLiteral("搜索关键词");
        }

        auto* previewButton = createPreviewButton(recentPreviewSection_, normalizedText(query, 40), meta);
        connect(previewButton, &QPushButton::clicked, this, [this, query]() { emit searchRequested(query); });
        recentPreviewLayout_->addWidget(previewButton);
        hasRenderableItems = true;
    }

    if (!hasRenderableItems) {
        recentPreviewLayout_->addWidget(
            createPreviewEmptyItem(recentPreviewSection_,
                                   QStringLiteral("暂无最近搜索"),
                                   QStringLiteral("搜索记录为空，请先执行一次搜索。")));
    }
}

void HomePage::rebuildFavoritesPreview()
{
    clearLayoutItems(favoritesPreviewLayout_);

    if (favoriteItems_.isEmpty()) {
        favoritesPreviewLayout_->addWidget(
            createPreviewEmptyItem(favoritesPreviewSection_,
                                   QStringLiteral("暂无收藏"),
                                   QStringLiteral("收藏高频结论后，这里会展示最近 3 条。")));
        return;
    }

    bool hasRenderableItems = false;
    for (const FavoritePreviewItem& item : std::as_const(favoriteItems_)) {
        const QString conclusionId = item.conclusionId.trimmed();
        if (conclusionId.isEmpty()) {
            continue;
        }

        const QString title = normalizedText(item.title, 40);
        const QString module = item.module.trimmed();
        const QString meta = module.isEmpty() ? QStringLiteral("结论 ID: %1").arg(conclusionId)
                                              : QStringLiteral("%1 · %2").arg(module, conclusionId);

        auto* previewButton = createPreviewButton(favoritesPreviewSection_, title, normalizedText(meta, 56));
        connect(previewButton, &QPushButton::clicked, this, [this, conclusionId]() {
            emit openConclusionRequested(conclusionId);
        });
        favoritesPreviewLayout_->addWidget(previewButton);
        hasRenderableItems = true;
    }

    if (!hasRenderableItems) {
        favoritesPreviewLayout_->addWidget(
            createPreviewEmptyItem(favoritesPreviewSection_,
                                   QStringLiteral("暂无收藏"),
                                   QStringLiteral("当前没有可展示的收藏条目。")));
    }
}

void HomePage::updateQuickActionSummary()
{
    if (recentQuickActionDescription_ != nullptr) {
        recentQuickActionDescription_->setText(QStringLiteral("共 %1 条记录，快速回访最近检索。").arg(recentItemCount_));
    }

    if (favoritesQuickActionDescription_ != nullptr) {
        favoritesQuickActionDescription_->setText(QStringLiteral("共 %1 条收藏，继续阅读已标记结论。")
                                                      .arg(favoriteItemCount_));
    }

    if (settingsQuickActionDescription_ != nullptr) {
        settingsQuickActionDescription_->setText(QStringLiteral("弱入口：调整显示与本地存储偏好。"));
    }

    if (recentShortcutButton_ != nullptr) {
        recentShortcutButton_->setText(QStringLiteral("最近搜索（%1）").arg(recentItemCount_));
    }

    if (favoritesShortcutButton_ != nullptr) {
        favoritesShortcutButton_->setText(QStringLiteral("我的收藏（%1）").arg(favoriteItemCount_));
    }
}

void HomePage::updateActivationSummaryIfNeeded()
{
    if (footerMetaLabel_ == nullptr) {
        return;
    }

    footerMetaLabel_->setText(
        QStringLiteral("本地离线模式 · 最近搜索 %1 条 · 收藏 %2 条 · %3")
            .arg(recentItemCount_)
            .arg(favoriteItemCount_)
            .arg(UiConstants::kStatusVersion));
}

void HomePage::clearLayoutItems(QVBoxLayout* layout)
{
    if (layout == nullptr) {
        return;
    }

    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            while (QLayoutItem* childItem = childLayout->takeAt(0)) {
                if (QWidget* childWidget = childItem->widget()) {
                    childWidget->deleteLater();
                }
                delete childItem;
            }
            delete childLayout;
        }
        delete item;
    }
}

QString HomePage::normalizedText(const QString& text, int maxLength)
{
    const QString simplified = text.simplified();
    if (maxLength <= 0 || simplified.size() <= maxLength) {
        return simplified;
    }
    return simplified.left(std::max(1, maxLength - 1)) + QStringLiteral("…");
}
