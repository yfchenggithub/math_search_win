#include "ui/pages/search_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "domain/adapters/conclusion_card_adapter.h"
#include "domain/adapters/conclusion_detail_adapter.h"
#include "domain/services/search_service.h"
#include "domain/services/suggest_service.h"
#include "infrastructure/data/conclusion_content_repository.h"
#include "infrastructure/data/conclusion_index_repository.h"
#include "license/feature_gate.h"
#include "license/license_service.h"
#include "ui/detail/detail_fallback_content_builder.h"
#include "ui/detail/detail_html_renderer.h"
#include "ui/detail/detail_pane.h"
#include "ui/detail/detail_render_coordinator.h"
#include "ui/detail/detail_render_path_resolver.h"
#include "ui/detail/detail_view_data_mapper.h"
#include "ui/style/app_style.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineView>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kResultItemDocIdRole = Qt::UserRole + 1;
const QString kDetailTimingIdleText = QStringLiteral("详情耗时：--");
const QString kDetailTimingLoadingText = QStringLiteral("详情加载中...");
const QString kDetailTimingFailedText = QStringLiteral("详情加载失败");
const QString kDetailTimingColorIdle = QStringLiteral("#7a7f87");
const QString kDetailTimingColorLoading = QStringLiteral("#8e959f");
const QString kDetailTimingColorSuccess = QStringLiteral("#5f666f");
const QString kDetailTimingColorFailed = QStringLiteral("#b06f5a");
constexpr int kTrialPreviewLimit = 5;

int findComboDataIndex(const QComboBox* combo, const QString& value)
{
    if (combo == nullptr) {
        return -1;
    }

    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toString() == value) {
            return i;
        }
    }
    return -1;
}

void repolishWidget(QWidget* widget)
{
    if (widget == nullptr || widget->style() == nullptr) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString detailMetaTextForPlaceholder(const QString& message)
{
    const QString normalized = message.trimmed();
    if (normalized.contains(QStringLiteral("没有找到"))) {
        return QStringLiteral("暂无匹配详情");
    }
    return QStringLiteral("等待选择结果");
}

bool shouldRecordSearchHistory(const QString& triggerSource)
{
    const QString normalizedTrigger = triggerSource.trimmed().toLower();
    return normalizedTrigger == QStringLiteral("button")
        || normalizedTrigger == QStringLiteral("return")
        || normalizedTrigger == QStringLiteral("suggest_click");
}

QString webFallbackUserMessage(const QString& reason)
{
    const QString normalized = reason.trimmed().toLower();
    if (normalized.contains(QStringLiteral("katex"))) {
        return QStringLiteral("KaTeX 本地资源缺失，已切换到兼容详情模式。");
    }
    if (normalized.contains(QStringLiteral("template"))) {
        return QStringLiteral("详情模板加载失败，已切换到兼容详情模式。");
    }
    if (normalized.contains(QStringLiteral("runtime"))) {
        return QStringLiteral("详情脚本渲染失败，已切换到兼容详情模式。");
    }
    return QStringLiteral("详情 Web 渲染不可用，已切换到兼容详情模式。");
}

}  // namespace

SearchPage::SearchPage(domain::services::SearchService* searchService,
                       domain::services::SuggestService* suggestService,
                       const infrastructure::data::ConclusionContentRepository* contentRepository,
                       const infrastructure::data::ConclusionIndexRepository* indexRepository,
                       const license::FeatureGate* featureGate,
                       const license::LicenseService* licenseService,
                       QWidget* parent)
    : QWidget(parent),
      searchService_(searchService),
      suggestService_(suggestService),
      contentRepository_(contentRepository),
      indexRepository_(indexRepository),
      featureGate_(featureGate),
      licenseService_(licenseService)
{
    ui::style::ensureAppStyleSheetLoaded();
    setObjectName(QStringLiteral("searchPage"));
    setProperty("pageRole", QStringLiteral("search"));
    indexReady_ = (indexRepository_ != nullptr && indexRepository_->docCount() > 0);
    contentReady_ = (contentRepository_ != nullptr && contentRepository_->size() > 0);
    detailHtmlRenderer_ = std::make_unique<ui::detail::DetailHtmlRenderer>();
    detailRenderCoordinator_ = std::make_unique<ui::detail::DetailRenderCoordinator>();
    detailViewDataMapper_ = std::make_unique<ui::detail::DetailViewDataMapper>();

    LOG_DEBUG(LogCategory::SearchEngine,
              QStringLiteral("page constructed name=search search_service_null=%1 suggest_service_null=%2")
                  .arg(searchService_ == nullptr ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(suggestService_ == nullptr ? QStringLiteral("true") : QStringLiteral("false")));

    detailSelectionCoalesceTimer_ = new QTimer(this);
    detailSelectionCoalesceTimer_->setSingleShot(true);
    detailSelectionCoalesceTimer_->setInterval(kDetailSelectionCoalesceMs);

    buildUi();
    connectSignals();
    ensureDetailShellLoaded();
    rebuildFilterOptions();
    applyFeatureGate();
    resetToEmptyState();

    if (licenseService_ != nullptr) {
        connect(licenseService_, &license::LicenseService::licenseStateChanged, this, [this](const license::LicenseState&) {
            applyFeatureGate();
            refreshFavoriteButtonState();
            const QString currentQuery = queryInput_ == nullptr ? QString() : queryInput_->text().trimmed();
            if (!currentQuery.isEmpty()) {
                lastSearchSignature_.clear();
                runSearch(currentQuery, QStringLiteral("license_state_changed"));
            }
        });
    }
}

bool SearchPage::isDetailWebReady() const
{
    return webDetailEnabled_;
}

void SearchPage::setBackendStatus(bool indexReady, bool contentReady)
{
    indexReady_ = indexReady;
    contentReady_ = contentReady;

    LOG_DEBUG(LogCategory::SearchEngine,
              QStringLiteral("backend status_updated index_ready=%1 content_ready=%2")
                  .arg(indexReady_ ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(contentReady_ ? QStringLiteral("true") : QStringLiteral("false")));

    rebuildFilterOptions();
    lastSuggestSignature_.clear();
    lastSearchSignature_.clear();
    hasPendingDetailRequest_ = false;
    clearDetailCaches();
    pendingDetailDocId_.clear();
    pendingDetailRequestId_ = 0;
    pendingDetailSelectionTimestampMs_ = 0;
    if (detailSelectionCoalesceTimer_ != nullptr) {
        detailSelectionCoalesceTimer_->stop();
    }
    if (detailRenderCoordinator_ != nullptr) {
        detailRenderCoordinator_->reset();
    }
    resetDetailTimingSessions(true);

    if (!indexReady_) {
        updateStatusLine(QStringLiteral("索引未就绪，当前无法搜索。"),
                         QStringLiteral("请检查 data/backend_search_index.json 的加载日志。"));
        showDetailError(QStringLiteral("索引未加载，当前无法显示搜索详情。"));
        return;
    }

    if (!contentReady_) {
        updateStatusLine(QStringLiteral("内容仓库未就绪，搜索可用但详情可能缺失。"));
        showDetailError(QStringLiteral("内容仓库未加载，选中结果后无法展示详情。"));
        return;
    }

    const QString currentQuery = queryInput_ == nullptr ? QString() : queryInput_->text().trimmed();
    if (currentQuery.isEmpty()) {
        resetToEmptyState();
    } else {
        runSearch(currentQuery, QStringLiteral("backend_status"));
    }
}

void SearchPage::setInitialQuery(const QString& query)
{
    if (queryInput_ == nullptr) {
        return;
    }

    const QString trimmedQuery = query.trimmed();
    {
        QSignalBlocker blocker(queryInput_);
        queryInput_->setText(trimmedQuery);
    }

    if (trimmedQuery.isEmpty()) {
        resetToEmptyState();
        return;
    }

    runSearch(trimmedQuery, QStringLiteral("initial_query"));
}

void SearchPage::setInitialModule(const QString& module)
{
    if (moduleFilterCombo_ == nullptr) {
        return;
    }

    const QString trimmedModule = module.trimmed();
    if (trimmedModule.isEmpty()) {
        return;
    }

    const int targetIndex = findComboDataIndex(moduleFilterCombo_, trimmedModule);
    if (targetIndex < 0) {
        LOG_WARN(LogCategory::SearchEngine,
                 QStringLiteral("initial_module ignored reason=unknown_module module=%1").arg(trimmedModule));
        return;
    }

    {
        QSignalBlocker blocker(moduleFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(targetIndex);
    }

    lastSuggestSignature_.clear();
    lastSearchSignature_.clear();

    const QString currentQuery = queryInput_ == nullptr ? QString() : queryInput_->text().trimmed();
    if (!currentQuery.isEmpty()) {
        runSearch(currentQuery, QStringLiteral("initial_module"));
    }
}

void SearchPage::triggerSearchFromRecent(const QString& query, const QString& module)
{
    if (!module.trimmed().isEmpty()) {
        setInitialModule(module);
    }
    setInitialQuery(query);
}

void SearchPage::openConclusionById(const QString& conclusionId)
{
    const QString normalizedId = conclusionId.trimmed();
    if (normalizedId.isEmpty()) {
        return;
    }

    if (!indexReady_ || indexRepository_ == nullptr) {
        updateStatusLine(QStringLiteral("索引未就绪，无法打开收藏详情。"),
                         QStringLiteral("请检查 data/backend_search_index.json 加载日志。"));
        showDetailError(QStringLiteral("索引未就绪，当前无法打开该收藏。"));
        return;
    }

    const auto* doc = indexRepository_->getDocById(normalizedId);
    if (doc == nullptr) {
        updateStatusLine(QStringLiteral("未找到对应收藏结论。"), QStringLiteral("conclusionId=%1").arg(normalizedId));
        showDetailError(QStringLiteral("未找到结论 ID: %1").arg(normalizedId));
        return;
    }

    if (moduleFilterCombo_ != nullptr && categoryFilterCombo_ != nullptr && tagFilterCombo_ != nullptr) {
        QSignalBlocker moduleBlocker(moduleFilterCombo_);
        QSignalBlocker categoryBlocker(categoryFilterCombo_);
        QSignalBlocker tagBlocker(tagFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(0);
        categoryFilterCombo_->setCurrentIndex(0);
        tagFilterCombo_->setCurrentIndex(0);
    }

    lastSuggestSignature_.clear();
    lastSearchSignature_.clear();
    clearSuggestions();

    if (queryInput_ != nullptr) {
        QSignalBlocker blocker(queryInput_);
        queryInput_->setText(doc->title.trimmed().isEmpty() ? normalizedId : doc->title.trimmed());
    }

    domain::models::SearchHit hit;
    hit.docId = doc->id;
    hit.title = doc->title;
    hit.module = doc->module;
    hit.category = doc->category;
    hit.difficulty = doc->difficulty;
    hit.tags = doc->tags;
    hit.summary = doc->summary;
    hit.coreFormula = doc->coreFormula;
    hit.score = doc->searchBoost;

    currentHits_.clear();
    currentHits_.push_back(std::move(hit));
    renderResults(currentHits_);

    if (resultList_ != nullptr && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    } else {
        enqueueDetailRenderRequest(normalizedId);
    }

    const QString moduleText = doc->module.trimmed().isEmpty() ? QStringLiteral("-") : doc->module.trimmed();
    updateStatusLine(QStringLiteral("已打开收藏结论。"),
                     QStringLiteral("conclusionId=%1 | module=%2").arg(normalizedId, moduleText));

    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("open conclusion from favorites doc_id=%1 title=%2")
                 .arg(normalizedId, doc->title.trimmed()));
}

void SearchPage::refreshFavoriteState()
{
    refreshFavoriteButtonState();
}

void SearchPage::onQueryTextChanged(const QString& text)
{
    if (suppressSuggestRefresh_) {
        return;
    }

    if (text.trimmed().isEmpty()) {
        lastSuggestSignature_.clear();
        lastSearchSignature_.clear();
        clearSuggestions();
        currentHits_.clear();
        renderResults(currentHits_);
        resetToEmptyState();
        return;
    }

    runSuggest(text);
}

void SearchPage::onQueryReturnPressed()
{
    runSearch(queryInput_ == nullptr ? QString() : queryInput_->text(), QStringLiteral("return"));
}

void SearchPage::onSearchButtonClicked()
{
    runSearch(queryInput_ == nullptr ? QString() : queryInput_->text(), QStringLiteral("button"));
}

void SearchPage::onSuggestionClicked(QListWidgetItem* item)
{
    if (item == nullptr || queryInput_ == nullptr) {
        return;
    }

    const QString suggestionText = item->data(kResultItemDocIdRole).toString().trimmed().isEmpty()
                                       ? item->text().trimmed()
                                       : item->data(kResultItemDocIdRole).toString().trimmed();
    if (suggestionText.isEmpty()) {
        return;
    }

    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("suggestion clicked text=%1 trigger=suggestion_click").arg(suggestionText));

    suppressSuggestRefresh_ = true;
    queryInput_->setText(suggestionText);
    suppressSuggestRefresh_ = false;
    runSearch(suggestionText, QStringLiteral("suggest_click"));
}

void SearchPage::onResultSelectionChanged(QListWidgetItem* currentItem)
{
    if (currentItem == nullptr) {
        hasPendingDetailRequest_ = false;
        pendingDetailDocId_.clear();
        pendingDetailRequestId_ = 0;
        pendingDetailSelectionTimestampMs_ = 0;
        currentDetailDocId_.clear();
        refreshFavoriteButtonState();
        if (detailRenderCoordinator_ != nullptr) {
            detailRenderCoordinator_->clearRenderedDetail();
        }
        resetDetailTimingSessions(true);
        showDetailPlaceholder(QStringLiteral("请选择左侧结果查看详情。"));
        return;
    }

    const QString docId = currentItem->data(kResultItemDocIdRole).toString().trimmed();
    if (docId.isEmpty()) {
        LOG_WARN(LogCategory::DetailRender, QStringLiteral("detail select_failed reason=missing_doc_id"));
        showDetailError(QStringLiteral("当前结果缺少结论 ID，无法显示详情。"));
        return;
    }

    enqueueDetailRenderRequest(docId);
}

void SearchPage::onFilterChanged()
{
    if (!isFeatureEnabled(license::Feature::AdvancedFilter)) {
        updateStatusLine(QStringLiteral("高级筛选未开放。"),
                         featureDisabledReason(license::Feature::AdvancedFilter));
        return;
    }

    lastSuggestSignature_.clear();
    lastSearchSignature_.clear();

    const QString query = queryInput_ == nullptr ? QString() : queryInput_->text().trimmed();
    if (query.isEmpty()) {
        updateStatusLine(QStringLiteral("筛选条件已更新。"), QStringLiteral("请输入关键词开始搜索。"));
        return;
    }

    runSuggest(query);
    runSearch(query, QStringLiteral("filter_change"));
}

void SearchPage::onSortChanged()
{
    if (currentHits_.isEmpty()) {
        return;
    }

    const QString currentDocId =
        resultList_ != nullptr && resultList_->currentItem() != nullptr
            ? resultList_->currentItem()->data(kResultItemDocIdRole).toString().trimmed()
            : QString();

    applySort(&currentHits_);
    renderResults(currentHits_);

    if (!currentDocId.isEmpty()) {
        for (int i = 0; i < resultList_->count(); ++i) {
            QListWidgetItem* item = resultList_->item(i);
            if (item != nullptr && item->data(kResultItemDocIdRole).toString() == currentDocId) {
                resultList_->setCurrentRow(i);
                return;
            }
        }
    }

    if (resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }
}

void SearchPage::onClearFiltersClicked()
{
    if (!isFeatureEnabled(license::Feature::AdvancedFilter)) {
        updateStatusLine(QStringLiteral("高级筛选未开放。"),
                         featureDisabledReason(license::Feature::AdvancedFilter));
        return;
    }

    if (moduleFilterCombo_ == nullptr || categoryFilterCombo_ == nullptr || tagFilterCombo_ == nullptr) {
        return;
    }

    {
        QSignalBlocker moduleBlocker(moduleFilterCombo_);
        QSignalBlocker categoryBlocker(categoryFilterCombo_);
        QSignalBlocker tagBlocker(tagFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(0);
        categoryFilterCombo_->setCurrentIndex(0);
        tagFilterCombo_->setCurrentIndex(0);
    }

    LOG_INFO(LogCategory::SearchEngine, QStringLiteral("filters cleared trigger=manual"));
    onFilterChanged();
}

void SearchPage::onFavoriteButtonClicked()
{
    const QString docId = currentDetailDocId_.trimmed();
    if (docId.isEmpty()) {
        updateStatusLine(QStringLiteral("当前未选中可收藏结论。"), QStringLiteral("请先在左侧选择一条结果。"));
        return;
    }

    if (!isFeatureEnabled(license::Feature::Favorites)) {
        updateStatusLine(QStringLiteral("收藏功能未开放。"), featureDisabledReason(license::Feature::Favorites));
        return;
    }

    if (!favoritesRepository_.load()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("favorites load failed before add doc_id=%1").arg(docId));
    }

    if (favoritesRepository_.contains(docId)) {
        favoritesRepository_.remove(docId);
        updateStatusLine(QStringLiteral("已取消收藏。"), QStringLiteral("docId=%1").arg(docId));
        refreshFavoriteButtonState(docId);
        emit favoritesChanged();
        return;
    }

    favoritesRepository_.add(docId);
    updateStatusLine(QStringLiteral("已加入收藏。"), QStringLiteral("docId=%1").arg(docId));
    refreshFavoriteButtonState(docId);
    emit favoritesChanged();
}

void SearchPage::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                   ui::style::tokens::kPageOuterMargin,
                                   ui::style::tokens::kPageOuterMargin,
                                   ui::style::tokens::kPageOuterMargin);
    mainLayout->setSpacing(ui::style::tokens::kMediumSpacing);

    auto* topBar = new QWidget(this);
    topBar->setObjectName(QStringLiteral("searchTopBar"));
    topBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* topBarLayout = new QVBoxLayout(topBar);
    topBarLayout->setContentsMargins(18, 14, 18, 14);
    topBarLayout->setSpacing(10);

    auto* titleBlock = new QWidget(topBar);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);

    auto* pageTitleLabel = new QLabel(QStringLiteral("搜索"), titleBlock);
    pageTitleLabel->setObjectName(QStringLiteral("searchPageTitle"));

    auto* pageSubtitleLabel =
        new QLabel(QStringLiteral("输入关键词后快速筛选，在右侧即时预览结论详情。"), titleBlock);
    pageSubtitleLabel->setObjectName(QStringLiteral("searchPageSubtitle"));
    pageSubtitleLabel->setWordWrap(true);

    titleLayout->addWidget(pageTitleLabel);
    titleLayout->addWidget(pageSubtitleLabel);
    topBarLayout->addWidget(titleBlock);

    auto* queryRow = new QHBoxLayout();
    queryRow->setContentsMargins(0, 0, 0, 0);
    queryRow->setSpacing(10);
    queryInput_ = new QLineEdit(topBar);
    queryInput_->setObjectName(QStringLiteral("searchInput"));
    queryInput_->setPlaceholderText(QStringLiteral("输入结论关键词，例如：不等式、对数、导数"));
    searchButton_ = new QPushButton(QStringLiteral("搜索"), topBar);
    searchButton_->setObjectName(QStringLiteral("searchButton"));
    searchButton_->setCursor(Qt::PointingHandCursor);
    queryRow->addWidget(queryInput_, 1);
    queryRow->addWidget(searchButton_, 0);
    topBarLayout->addLayout(queryRow);

    suggestionList_ = new QListWidget(topBar);
    suggestionList_->setObjectName(QStringLiteral("searchSuggestionList"));
    suggestionList_->setMaximumHeight(150);
    suggestionList_->setVisible(false);
    topBarLayout->addWidget(suggestionList_);
    mainLayout->addWidget(topBar);

    auto* workbench = new QWidget(this);
    workbench->setObjectName(QStringLiteral("searchWorkbench"));
    workbench->setAttribute(Qt::WA_StyledBackground, true);
    auto* workbenchLayout = new QVBoxLayout(workbench);
    workbenchLayout->setContentsMargins(0, 0, 0, 0);
    workbenchLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, workbench);
    splitter->setObjectName(QStringLiteral("searchWorkbenchSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    auto* leftPanel = new QWidget(splitter);
    leftPanel->setObjectName(QStringLiteral("searchLeftColumn"));
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ui::style::tokens::kMediumSpacing);

    auto* filterPanel = new QWidget(leftPanel);
    filterPanel->setObjectName(QStringLiteral("searchFilterPanel"));
    filterPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto* filterPanelLayout = new QVBoxLayout(filterPanel);
    filterPanelLayout->setContentsMargins(14, 12, 14, 12);
    filterPanelLayout->setSpacing(10);

    auto* filterTitle = new QLabel(QStringLiteral("筛选与排序"), filterPanel);
    filterTitle->setObjectName(QStringLiteral("searchFilterTitle"));
    filterPanelLayout->addWidget(filterTitle);

    auto* filterLayout = new QFormLayout();
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setHorizontalSpacing(10);
    filterLayout->setVerticalSpacing(8);
    moduleFilterCombo_ = new QComboBox(filterPanel);
    categoryFilterCombo_ = new QComboBox(filterPanel);
    tagFilterCombo_ = new QComboBox(filterPanel);
    sortCombo_ = new QComboBox(filterPanel);
    moduleFilterCombo_->setObjectName(QStringLiteral("searchFilterCombo"));
    categoryFilterCombo_->setObjectName(QStringLiteral("searchFilterCombo"));
    tagFilterCombo_->setObjectName(QStringLiteral("searchFilterCombo"));
    sortCombo_->setObjectName(QStringLiteral("searchFilterCombo"));
    clearFiltersButton_ = new QPushButton(QStringLiteral("清空筛选"), filterPanel);
    clearFiltersButton_->setObjectName(QStringLiteral("searchClearFiltersButton"));
    clearFiltersButton_->setCursor(Qt::PointingHandCursor);

    sortCombo_->addItem(QStringLiteral("按相关度"), static_cast<int>(SortMode::ScoreDesc));
    sortCombo_->addItem(QStringLiteral("按标题 A-Z"), static_cast<int>(SortMode::TitleAsc));
    sortCombo_->addItem(QStringLiteral("按难度 低到高"), static_cast<int>(SortMode::DifficultyAsc));
    sortCombo_->addItem(QStringLiteral("按难度 高到低"), static_cast<int>(SortMode::DifficultyDesc));

    filterLayout->addRow(QStringLiteral("模块"), moduleFilterCombo_);
    filterLayout->addRow(QStringLiteral("分类"), categoryFilterCombo_);
    filterLayout->addRow(QStringLiteral("标签"), tagFilterCombo_);
    filterLayout->addRow(QStringLiteral("排序"), sortCombo_);
    filterLayout->addRow(clearFiltersButton_);
    filterPanelLayout->addLayout(filterLayout);
    leftLayout->addWidget(filterPanel);

    auto* resultPanel = new QWidget(leftPanel);
    resultPanel->setObjectName(QStringLiteral("searchResultsPanel"));
    resultPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(14, 12, 14, 12);
    resultLayout->setSpacing(8);

    auto* resultTitleLabel = new QLabel(QStringLiteral("搜索结果"), resultPanel);
    resultTitleLabel->setObjectName(QStringLiteral("searchResultsTitle"));
    resultLayout->addWidget(resultTitleLabel);

    statusLabel_ = new QLabel(resultPanel);
    statusLabel_->setObjectName(QStringLiteral("searchStatusLabel"));
    statusLabel_->setWordWrap(true);
    summaryLabel_ = new QLabel(resultPanel);
    summaryLabel_->setObjectName(QStringLiteral("searchSummaryLabel"));
    summaryLabel_->setWordWrap(true);
    resultLayout->addWidget(statusLabel_);
    resultLayout->addWidget(summaryLabel_);

    resultList_ = new QListWidget(resultPanel);
    resultList_->setObjectName(QStringLiteral("resultList"));
    resultList_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultList_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultList_->setWordWrap(true);
    resultLayout->addWidget(resultList_, 1);

    resultEmptyState_ = new QWidget(resultPanel);
    resultEmptyState_->setObjectName(QStringLiteral("emptyState"));
    auto* resultEmptyLayout = new QVBoxLayout(resultEmptyState_);
    resultEmptyLayout->setContentsMargins(12, 16, 12, 16);
    resultEmptyLayout->setSpacing(6);
    resultEmptyLayout->addStretch(1);

    resultEmptyTitleLabel_ = new QLabel(QStringLiteral("开始搜索"), resultEmptyState_);
    resultEmptyTitleLabel_->setObjectName(QStringLiteral("emptyStateTitle"));
    resultEmptyTitleLabel_->setAlignment(Qt::AlignHCenter);
    resultEmptyDescriptionLabel_ = new QLabel(QStringLiteral("输入关键词后在这里查看匹配结果。"), resultEmptyState_);
    resultEmptyDescriptionLabel_->setObjectName(QStringLiteral("emptyStateDescription"));
    resultEmptyDescriptionLabel_->setAlignment(Qt::AlignHCenter);
    resultEmptyDescriptionLabel_->setWordWrap(true);

    resultEmptyLayout->addWidget(resultEmptyTitleLabel_);
    resultEmptyLayout->addWidget(resultEmptyDescriptionLabel_);
    resultEmptyLayout->addStretch(2);
    resultLayout->addWidget(resultEmptyState_, 1);
    resultEmptyState_->setVisible(false);

    leftLayout->addWidget(resultPanel, 1);

    auto* detailShell = new QWidget(splitter);
    detailShell->setObjectName(QStringLiteral("detailShell"));
    detailShell->setAttribute(Qt::WA_StyledBackground, true);
    auto* rightLayout = new QVBoxLayout(detailShell);
    rightLayout->setContentsMargins(14, 12, 14, 12);
    rightLayout->setSpacing(10);

    auto* detailHeader = new QWidget(detailShell);
    detailHeader->setObjectName(QStringLiteral("detailShellHeader"));
    auto* detailHeaderLayout = new QHBoxLayout(detailHeader);
    detailHeaderLayout->setContentsMargins(0, 0, 0, 0);
    detailHeaderLayout->setSpacing(8);

    auto* detailHeaderLeft = new QWidget(detailHeader);
    auto* detailHeaderLeftLayout = new QVBoxLayout(detailHeaderLeft);
    detailHeaderLeftLayout->setContentsMargins(0, 0, 0, 0);
    detailHeaderLeftLayout->setSpacing(2);

    auto* detailTitle = new QLabel(QStringLiteral("详情预览"), detailHeaderLeft);
    detailTitle->setObjectName(QStringLiteral("detailShellTitle"));
    detailMetaLabel_ = new QLabel(QStringLiteral("等待选择结果"), detailHeaderLeft);
    detailMetaLabel_->setObjectName(QStringLiteral("detailShellMeta"));
    detailMetaLabel_->setProperty("tone", QStringLiteral("neutral"));
    detailMetaLabel_->setWordWrap(true);
    detailHeaderLeftLayout->addWidget(detailTitle);
    detailHeaderLeftLayout->addWidget(detailMetaLabel_);

    detailTimingLabel_ = new QLabel(detailHeader);
    detailTimingLabel_->setObjectName(QStringLiteral("detailPerfLabel"));
    detailTimingLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    detailTimingLabel_->setProperty("timingState", QStringLiteral("idle"));

    favoriteButton_ = new QPushButton(QStringLiteral("收藏当前结论"), detailHeader);
    favoriteButton_->setObjectName(QStringLiteral("searchClearFiltersButton"));
    favoriteButton_->setCursor(Qt::PointingHandCursor);
    favoriteButton_->setEnabled(false);

    auto* detailHeaderRight = new QWidget(detailHeader);
    auto* detailHeaderRightLayout = new QVBoxLayout(detailHeaderRight);
    detailHeaderRightLayout->setContentsMargins(0, 0, 0, 0);
    detailHeaderRightLayout->setSpacing(4);
    detailHeaderRightLayout->addWidget(favoriteButton_, 0, Qt::AlignRight);
    detailHeaderRightLayout->addWidget(detailTimingLabel_, 0, Qt::AlignRight);

    detailHeaderLayout->addWidget(detailHeaderLeft, 1);
    detailHeaderLayout->addWidget(detailHeaderRight, 0, Qt::AlignTop);
    rightLayout->addWidget(detailHeader);

    auto* detailBody = new QWidget(detailShell);
    detailBody->setObjectName(QStringLiteral("detailShellBody"));
    detailBody->setAttribute(Qt::WA_StyledBackground, true);
    auto* detailBodyLayout = new QVBoxLayout(detailBody);
    detailBodyLayout->setContentsMargins(10, 10, 10, 10);
    detailBodyLayout->setSpacing(0);

    detailWebView_ = new QWebEngineView(detailBody);
    detailWebView_->setObjectName(QStringLiteral("detailWebView"));
    detailWebView_->setVisible(false);
    detailBodyLayout->addWidget(detailWebView_, 1);

    detailBrowser_ = new QTextBrowser(detailBody);
    detailBrowser_->setObjectName(QStringLiteral("detailFallbackView"));
    detailBrowser_->setOpenExternalLinks(false);
    detailBrowser_->setVisible(false);
    detailBodyLayout->addWidget(detailBrowser_, 1);

    rightLayout->addWidget(detailBody, 1);
    updateDetailTimingLabel(kDetailTimingIdleText, kDetailTimingColorIdle);
    updateDetailShellMeta(QStringLiteral("等待选择结果"), QStringLiteral("neutral"));

    webDetailEnabled_ = detailHtmlRenderer_ != nullptr && detailHtmlRenderer_->isReady();
    if (webDetailEnabled_) {
        detailPane_ = std::make_unique<ui::detail::DetailPane>(detailWebView_, detailBrowser_, detailHtmlRenderer_.get());
        webDetailEnabled_ = detailPane_ != nullptr && detailPane_->isWebModeEnabled();
    }

    if (webDetailEnabled_) {
        detailWebView_->setVisible(true);
        LOG_DEBUG(LogCategory::WebViewKatex,
                  QStringLiteral("web_mode enabled detail_dir=%1 template=%2")
                      .arg(detailHtmlRenderer_->detailDirectory(), detailHtmlRenderer_->detailTemplatePath()));
        LOG_DEBUG(LogCategory::PerfWebView, QStringLiteral("event=web_mode_enabled mode=web"));
    } else {
        detailBrowser_->setVisible(true);
        LOG_WARN(LogCategory::WebViewKatex,
                 QStringLiteral("renderer unavailable mode=text_fallback reason=%1")
                     .arg(detailHtmlRenderer_ == nullptr ? QStringLiteral("detail renderer is null")
                                                         : detailHtmlRenderer_->lastError()));
    }

    splitter->addWidget(leftPanel);
    splitter->addWidget(detailShell);
    splitter->setStretchFactor(0, 10);
    splitter->setStretchFactor(1, 12);
    splitter->setSizes({600, 760});

    workbenchLayout->addWidget(splitter, 1);
    mainLayout->addWidget(workbench, 1);
}

void SearchPage::connectSignals()
{
    connect(queryInput_, &QLineEdit::textChanged, this, &SearchPage::onQueryTextChanged);
    connect(queryInput_, &QLineEdit::returnPressed, this, &SearchPage::onQueryReturnPressed);
    connect(searchButton_, &QPushButton::clicked, this, &SearchPage::onSearchButtonClicked);

    connect(suggestionList_, &QListWidget::itemClicked, this, &SearchPage::onSuggestionClicked);
    connect(suggestionList_, &QListWidget::itemActivated, this, &SearchPage::onSuggestionClicked);

    connect(resultList_,
            &QListWidget::currentItemChanged,
            this,
            [this](QListWidgetItem* current, QListWidgetItem*) { onResultSelectionChanged(current); });

    if (detailSelectionCoalesceTimer_ != nullptr) {
        connect(detailSelectionCoalesceTimer_, &QTimer::timeout, this, &SearchPage::flushPendingDetailRequest);
    }

    connect(moduleFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(categoryFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(tagFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(sortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onSortChanged);
    connect(clearFiltersButton_, &QPushButton::clicked, this, &SearchPage::onClearFiltersClicked);
    connect(favoriteButton_, &QPushButton::clicked, this, &SearchPage::onFavoriteButtonClicked);

    if (detailPane_ != nullptr) {
        connect(detailPane_.get(), &ui::detail::DetailPane::shellReadyChanged, this, [this](bool ready) {
            if (ready) {
                LOG_INFO_F(LogCategory::PerfWebView,
                           "SearchPage::onDetailShellLoaded",
                           QStringLiteral("event=detail_shell_loaded"));
            }
        });
        connect(detailPane_.get(), &ui::detail::DetailPane::webModeFailed, this, &SearchPage::activateTextFallbackMode);
        connect(detailPane_.get(),
                &ui::detail::DetailPane::perfPhase,
                this,
                [this](const QString& detailId,
                       quint64 requestId,
                       qint64 selectionTimestampMs,
                       qint64 atMs,
                       const QString& phase,
                       const QString& extra) {
                    logDetailPerf(detailId, requestId, selectionTimestampMs, phase, extra, atMs);
                    handleDetailPerfPhase(detailId, requestId, selectionTimestampMs, atMs, phase, extra);
                });
    }
}

void SearchPage::rebuildFilterOptions()
{
    if (moduleFilterCombo_ == nullptr || categoryFilterCombo_ == nullptr || tagFilterCombo_ == nullptr) {
        return;
    }

    const QString currentModule = selectedModuleFilter();
    const QString currentCategory = selectedCategoryFilter();
    const QString currentTag = selectedTagFilter();

    {
        QSignalBlocker blocker(moduleFilterCombo_);
        moduleFilterCombo_->clear();
        moduleFilterCombo_->addItem(QStringLiteral("全部模块"), QString());
        if (indexRepository_ != nullptr) {
            const QStringList modules = uniqueSortedCaseInsensitive(indexRepository_->modules());
            for (const QString& module : modules) {
                moduleFilterCombo_->addItem(module, module);
            }
        }

        const int restoreIndex = findComboDataIndex(moduleFilterCombo_, currentModule);
        moduleFilterCombo_->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 0);
    }

    {
        QSignalBlocker blocker(categoryFilterCombo_);
        categoryFilterCombo_->clear();
        categoryFilterCombo_->addItem(QStringLiteral("全部分类"), QString());
        const QStringList categories = collectCategoryOptions();
        for (const QString& category : categories) {
            categoryFilterCombo_->addItem(category, category);
        }

        const int restoreIndex = findComboDataIndex(categoryFilterCombo_, currentCategory);
        categoryFilterCombo_->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 0);
    }

    {
        QSignalBlocker blocker(tagFilterCombo_);
        tagFilterCombo_->clear();
        tagFilterCombo_->addItem(QStringLiteral("全部标签"), QString());
        if (contentRepository_ != nullptr) {
            const QStringList tags = uniqueSortedCaseInsensitive(contentRepository_->tags());
            for (const QString& tag : tags) {
                tagFilterCombo_->addItem(tag, tag);
            }
        }

        const int restoreIndex = findComboDataIndex(tagFilterCombo_, currentTag);
        tagFilterCombo_->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 0);
    }
}

void SearchPage::resetToEmptyState()
{
    updateStatusLine(QStringLiteral("请输入关键词开始搜索。"),
                     QStringLiteral("支持实时建议、模块/分类/标签筛选、结果详情联动。"));
    updateResultEmptyState(QStringLiteral("开始搜索"), QStringLiteral("输入关键词后在这里查看匹配结果。"));
    resetDetailTimingSessions(true);
    showDetailPlaceholder(QStringLiteral("左侧输入关键词后可查看搜索结果和详情。"));

    if (!webDetailEnabled_) {
        const QString reason = detailHtmlRenderer_ == nullptr ? QStringLiteral("detail renderer is null")
                                                              : detailHtmlRenderer_->lastError().trimmed();
        updateStatusLine(QStringLiteral("详情页已切换兼容模式。"),
                         QStringLiteral("请检查 app_resources/detail 与 app_resources/katex。"));
        updateDetailShellMeta(QStringLiteral("兼容模式：Web 资源不可用"), QStringLiteral("warning"));
        LOG_WARN(LogCategory::WebViewKatex,
                 QStringLiteral("detail empty_state fallback reason=%1")
                     .arg(reason.isEmpty() ? QStringLiteral("unknown") : reason));
    } else {
        updateDetailShellMeta(QStringLiteral("等待选择结果"), QStringLiteral("neutral"));
    }
}

void SearchPage::updateStatusLine(const QString& status, const QString& summary)
{
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(status);
    }
    if (summaryLabel_ != nullptr) {
        summaryLabel_->setText(summary);
    }
}

void SearchPage::updateResultEmptyState(const QString& title, const QString& description)
{
    if (resultEmptyTitleLabel_ != nullptr) {
        resultEmptyTitleLabel_->setText(title.trimmed().isEmpty() ? QStringLiteral("暂无结果") : title.trimmed());
    }
    if (resultEmptyDescriptionLabel_ != nullptr) {
        resultEmptyDescriptionLabel_->setText(description.trimmed().isEmpty()
                                                  ? QStringLiteral("请尝试调整关键词或筛选条件。")
                                                  : description.trimmed());
    }

    const bool shouldShowEmpty = (resultList_ != nullptr && resultList_->count() == 0);
    if (resultList_ != nullptr) {
        resultList_->setVisible(!shouldShowEmpty);
    }
    if (resultEmptyState_ != nullptr) {
        resultEmptyState_->setVisible(shouldShowEmpty);
    }
}

void SearchPage::updateDetailShellMeta(const QString& text, const QString& tone)
{
    if (detailMetaLabel_ == nullptr) {
        return;
    }

    detailMetaLabel_->setText(text.trimmed().isEmpty() ? QStringLiteral("等待选择结果") : text.trimmed());
    detailMetaLabel_->setProperty("tone", tone.trimmed().isEmpty() ? QStringLiteral("neutral") : tone.trimmed());
    repolishWidget(detailMetaLabel_);
}

void SearchPage::runSuggest(const QString& query)
{
    const QString normalizedQuery = query.trimmed();
    if (normalizedQuery.isEmpty()) {
        clearSuggestions();
        return;
    }

    if (!indexReady_ || suggestService_ == nullptr) {
        clearSuggestions();
        updateStatusLine(QStringLiteral("索引未就绪，无法生成建议。"));
        LOG_WARN(LogCategory::SearchEngine,
                 QStringLiteral("suggest skipped reason=backend_unavailable index_ready=%1")
                     .arg(indexReady_ ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }

    const QString signature = buildSuggestSignature(normalizedQuery);
    if (signature == lastSuggestSignature_) {
        return;
    }

    domain::models::SuggestOptions options;
    options.maxResults = 8;
    const QString moduleFilter = selectedModuleFilter();
    const QString categoryFilter = selectedCategoryFilter();
    const QString tagFilter = selectedTagFilter();
    if (!moduleFilter.isEmpty()) {
        options.moduleFilter.push_back(moduleFilter);
    }
    if (!categoryFilter.isEmpty()) {
        options.categoryFilter.push_back(categoryFilter);
    }
    if (!tagFilter.isEmpty()) {
        options.tagFilter.push_back(tagFilter);
    }

    QElapsedTimer timer;
    timer.start();
    const domain::models::SuggestionResult result = suggestService_->suggest(normalizedQuery, options);
    const qint64 elapsedMs = timer.elapsed();

    suggestionList_->clear();
    for (const domain::models::SuggestionItem& item : result.items) {
        if (item.text.trimmed().isEmpty()) {
            continue;
        }
        auto* row = new QListWidgetItem(item.text, suggestionList_);
        row->setData(kResultItemDocIdRole, item.text);
    }

    suggestionList_->setVisible(suggestionList_->count() > 0);
    updateStatusLine(QStringLiteral("建议已更新。"),
                     QStringLiteral("query=%1 | suggest=%2 | elapsed=%3ms")
                         .arg(normalizedQuery)
                         .arg(result.items.size())
                         .arg(elapsedMs));

    LOG_DEBUG(LogCategory::PerfSearch,
              QStringLiteral("event=suggest_done query=%1 total=%2 elapsed_ms=%3")
                  .arg(normalizedQuery)
                  .arg(result.items.size())
                  .arg(elapsedMs));

    lastSuggestSignature_ = signature;
}

void SearchPage::runSearch(const QString& query, const QString& triggerSource)
{
    const QString normalizedQuery = query.trimmed();
    if (normalizedQuery.isEmpty()) {
        currentHits_.clear();
        renderResults(currentHits_);
        clearSuggestions();
        resetToEmptyState();
        return;
    }

    if (!indexReady_ || searchService_ == nullptr) {
        updateStatusLine(QStringLiteral("索引未就绪，无法执行搜索。"),
                         QStringLiteral("请检查索引加载日志。"));
        showDetailError(QStringLiteral("索引未就绪，当前无法展示结果详情。"));
        LOG_ERROR(LogCategory::SearchEngine,
                  QStringLiteral("search failed reason=backend_unavailable query=%1").arg(normalizedQuery));
        return;
    }

    if (!isFeatureEnabled(license::Feature::BasicSearchPreview)
        && !isFeatureEnabled(license::Feature::FullSearch)) {
        updateStatusLine(QStringLiteral("当前授权不支持搜索。"), QStringLiteral("请先激活正式版。"));
        updateResultEmptyState(QStringLiteral("搜索未开放"), QStringLiteral("请先在激活页完成授权。"));
        showDetailPlaceholder(QStringLiteral("当前授权不支持详情查看。"));
        return;
    }

    if (shouldRecordSearchHistory(triggerSource)) {
        if (!historyRepository_.load()) {
            LOG_WARN(LogCategory::FileIo, QStringLiteral("history load failed before add query=%1").arg(normalizedQuery));
        }
        historyRepository_.addQuery(normalizedQuery, triggerSource);
        emit historyChanged();
    }

    const QString signature = buildSearchSignature(normalizedQuery);
    if (signature == lastSearchSignature_) {
        LOG_DEBUG(LogCategory::SearchEngine,
                  QStringLiteral("search skipped reason=duplicate_signature query=%1").arg(normalizedQuery));
        return;
    }

    domain::models::SearchOptions options;
    const bool fullSearchEnabled = isFeatureEnabled(license::Feature::FullSearch);
    const bool basicPreviewEnabled = isFeatureEnabled(license::Feature::BasicSearchPreview);
    const bool advancedFilterEnabled = isFeatureEnabled(license::Feature::AdvancedFilter);
    options.maxResults = fullSearchEnabled ? 120 : kTrialPreviewLimit;

    const QString moduleFilter = selectedModuleFilter();
    const QString categoryFilter = selectedCategoryFilter();
    const QString tagFilter = selectedTagFilter();
    if (advancedFilterEnabled && !moduleFilter.isEmpty()) {
        options.moduleFilter.push_back(moduleFilter);
    }
    if (advancedFilterEnabled && !categoryFilter.isEmpty()) {
        options.categoryFilter.push_back(categoryFilter);
    }
    if (advancedFilterEnabled && !tagFilter.isEmpty()) {
        options.tagFilter.push_back(tagFilter);
    }

    QElapsedTimer timer;
    timer.start();
    const domain::models::SearchResult result = searchService_->search(normalizedQuery, options);
    const qint64 elapsedMs = timer.elapsed();

    const int rawHitCount = result.hits.size();
    currentHits_ = result.hits;
    if (!fullSearchEnabled) {
        if (!basicPreviewEnabled) {
            currentHits_.clear();
        } else if (currentHits_.size() > kTrialPreviewLimit) {
            currentHits_.resize(kTrialPreviewLimit);
        }
    }
    applySort(&currentHits_);
    renderResults(currentHits_);
    clearSuggestions();
    lastSearchSignature_ = signature;

    const QString filterSummary =
        QStringLiteral("module=%1 | category=%2 | tag=%3")
            .arg(advancedFilterEnabled ? (moduleFilter.isEmpty() ? QStringLiteral("all") : moduleFilter)
                                       : QStringLiteral("locked"))
            .arg(advancedFilterEnabled ? (categoryFilter.isEmpty() ? QStringLiteral("all") : categoryFilter)
                                       : QStringLiteral("locked"))
            .arg(advancedFilterEnabled ? (tagFilter.isEmpty() ? QStringLiteral("all") : tagFilter)
                                       : QStringLiteral("locked"));

    if (currentHits_.isEmpty()) {
        updateStatusLine(QStringLiteral("没有找到相关结论。"),
                         QStringLiteral("query=%1 | total=0 | elapsed=%2ms | %3")
                             .arg(normalizedQuery)
                             .arg(elapsedMs)
                             .arg(filterSummary));
        updateResultEmptyState(QStringLiteral("没有匹配结果"),
                               QStringLiteral("建议尝试更短关键词，或清空筛选后重试。"));
        showDetailPlaceholder(QStringLiteral("没有找到相关结论。建议尝试更短关键词或清空筛选。"));

        LOG_INFO(LogCategory::PerfSearch,
                 QStringLiteral("event=search_done query=%1 total=0 elapsed_ms=%2 trigger=%3")
                     .arg(normalizedQuery)
                     .arg(elapsedMs)
                     .arg(triggerSource));
        return;
    }

    updateStatusLine(QStringLiteral("搜索完成。"),
                     QStringLiteral("query=%1 | total=%2 | elapsed=%3ms | %4")
                         .arg(normalizedQuery)
                         .arg(currentHits_.size())
                         .arg(elapsedMs)
                         .arg(filterSummary));

    if (!fullSearchEnabled) {
        const QString reason = featureDisabledReason(license::Feature::FullSearch);
        updateStatusLine(
            QStringLiteral("体验版仅展示前 %1 条结果（命中 %2 条）。").arg(kTrialPreviewLimit).arg(rawHitCount),
            reason.isEmpty() ? QStringLiteral("正式版解锁完整搜索。") : reason);
    }
    updateResultEmptyState(QString(), QString());

    LOG_INFO(LogCategory::PerfSearch,
             QStringLiteral("event=search_done query=%1 total=%2 elapsed_ms=%3 trigger=%4")
                 .arg(normalizedQuery)
                 .arg(currentHits_.size())
                 .arg(elapsedMs)
                 .arg(triggerSource));

    if (resultList_ != nullptr && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }
}

void SearchPage::clearSuggestions()
{
    if (suggestionList_ == nullptr) {
        return;
    }
    suggestionList_->clear();
    suggestionList_->setVisible(false);
}

void SearchPage::renderResults(const QVector<domain::models::SearchHit>& hits)
{
    if (resultList_ == nullptr) {
        return;
    }

    resultList_->clear();
    if (hits.isEmpty()) {
        updateResultEmptyState(QStringLiteral("暂无搜索结果"),
                               QStringLiteral("输入关键词后在这里查看匹配结论。"));
        return;
    }

    for (const domain::models::SearchHit& hit : hits) {
        const QString titleText = hit.title.trimmed().isEmpty() ? hit.docId : hit.title;
        const QString moduleText = hit.module.trimmed().isEmpty() ? QStringLiteral("未标注模块") : hit.module.trimmed();
        const QString categoryText = hit.category.trimmed().isEmpty() ? QStringLiteral("未标注分类") : hit.category.trimmed();
        const QString difficultyText = QStringLiteral("难度 %1").arg(QString::number(hit.difficulty, 'f', 1));
        const QString tagsText = hit.tags.isEmpty() ? QStringLiteral("-") : hit.tags.mid(0, 6).join(QStringLiteral(" / "));
        const QString lineText = QStringLiteral("%1\n%2 | %3 | %4\n标签: %5 | 相关度: %6")
                                     .arg(titleText,
                                          moduleText,
                                          categoryText,
                                          difficultyText,
                                          tagsText,
                                          QString::number(hit.score, 'f', 2));

        auto* item = new QListWidgetItem(lineText, resultList_);
        item->setData(kResultItemDocIdRole, hit.docId);
        item->setSizeHint(QSize(item->sizeHint().width(), 76));

        if (contentReady_ && contentRepository_ != nullptr) {
            const auto* record = contentRepository_->getById(hit.docId);
            if (record != nullptr) {
                const domain::adapters::ConclusionCardViewData cardView =
                    domain::adapters::ConclusionCardAdapter::toViewData(*record);
                const QString tooltip =
                    joinNonEmpty(
                        {cardView.summaryPlain,
                         cardView.formulaFallbackText.isEmpty() ? QString() : QStringLiteral("公式: %1").arg(cardView.formulaFallbackText)},
                        QStringLiteral("\n"));
                if (!tooltip.isEmpty()) {
                    item->setToolTip(tooltip);
                }
            }
        }
    }

    updateResultEmptyState(QString(), QString());
}

void SearchPage::enqueueDetailRenderRequest(const QString& docId)
{
    const QString normalizedDocId = docId.trimmed();
    if (normalizedDocId.isEmpty()) {
        return;
    }

    if (detailRenderCoordinator_ != nullptr && detailRenderCoordinator_->isSameAsRendered(normalizedDocId)
        && !hasPendingDetailRequest_ && (detailPane_ == nullptr || !detailPane_->hasPendingRequest())) {
        LOG_DEBUG(LogCategory::PerfDetail,
                  QStringLiteral("event=detail_select_skipped reason=already_rendered doc_id=%1").arg(normalizedDocId));
        return;
    }

    if (hasPendingDetailRequest_ && pendingDetailDocId_ == normalizedDocId) {
        return;
    }

    if (detailRenderCoordinator_ == nullptr) {
        return;
    }

    const ui::detail::DetailRenderRequestCreation creation = detailRenderCoordinator_->createRequest(normalizedDocId);
    const quint64 requestId = creation.request.requestId;
    const qint64 selectionTimestampMs = creation.request.selectionTimestampMs;
    if (requestId == 0 || selectionTimestampMs <= 0) {
        return;
    }

    startDetailTimingSession(normalizedDocId, requestId, selectionTimestampMs);

    if (creation.supersededRequestId > 0) {
        logDetailPerf(normalizedDocId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("request_superseded"),
                      QStringLiteral("superseded_req=%1 superseded_id=%2")
                          .arg(creation.supersededRequestId)
                          .arg(creation.supersededDetailId));
    }

    pendingDetailDocId_ = normalizedDocId;
    pendingDetailRequestId_ = requestId;
    pendingDetailSelectionTimestampMs_ = selectionTimestampMs;
    hasPendingDetailRequest_ = true;

    logDetailPerf(normalizedDocId, requestId, selectionTimestampMs, QStringLiteral("selection_received"));
    logDetailPerf(normalizedDocId, requestId, selectionTimestampMs, QStringLiteral("detail_request_created"));

    if (detailSelectionCoalesceTimer_ != nullptr) {
        detailSelectionCoalesceTimer_->start();
    } else {
        flushPendingDetailRequest();
    }
}

void SearchPage::flushPendingDetailRequest()
{
    if (!hasPendingDetailRequest_) {
        return;
    }

    const QString docId = pendingDetailDocId_;
    const quint64 requestId = pendingDetailRequestId_;
    const qint64 selectionTimestampMs = pendingDetailSelectionTimestampMs_;

    hasPendingDetailRequest_ = false;
    pendingDetailDocId_.clear();
    pendingDetailRequestId_ = 0;
    pendingDetailSelectionTimestampMs_ = 0;

    if (docId.isEmpty()) {
        return;
    }

    if (detailRenderCoordinator_ != nullptr && detailRenderCoordinator_->isRequestStale(requestId)) {
        // Selection changed while this request was waiting in the coalescing queue.
        logDetailPerf(docId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("request_superseded"),
                      QStringLiteral("reason=stale_before_render"));
        markDetailTimingStaleIgnored(docId,
                                     requestId,
                                     selectionTimestampMs,
                                     QStringLiteral("reason=stale_before_render"));
        return;
    }

    renderDetailForRequest(docId, requestId, selectionTimestampMs);
}

void SearchPage::renderDetailForRequest(const QString& docId, quint64 requestId, qint64 selectionTimestampMs)
{
    const QString normalizedDocId = docId.trimmed();
    if (normalizedDocId.isEmpty()) {
        showDetailError(QStringLiteral("当前结果缺少结论 ID，无法显示详情。"));
        logDetailPerf(QStringLiteral("-"),
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("request_failed"),
                      QStringLiteral("reason=empty_doc_id"));
        markDetailTimingFailed(QStringLiteral("-"),
                               requestId,
                               selectionTimestampMs,
                               QStringLiteral("reason=empty_doc_id"));
        return;
    }

    currentDetailDocId_ = normalizedDocId;
    refreshFavoriteButtonState(normalizedDocId);

    if (detailRenderCoordinator_ != nullptr && detailRenderCoordinator_->isRequestStale(requestId)) {
        // Drop stale work before touching repositories so rapid A/B/C/D switches stay responsive.
        logDetailPerf(normalizedDocId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("request_superseded"),
                      QStringLiteral("reason=stale_before_payload"));
        markDetailTimingStaleIgnored(normalizedDocId,
                                     requestId,
                                     selectionTimestampMs,
                                     QStringLiteral("reason=stale_before_payload"));
        return;
    }

    if (!contentReady_ || contentRepository_ == nullptr) {
        showDetailError(QStringLiteral("内容仓库未就绪，无法显示详情。"));
        LOG_WARN(LogCategory::DetailRender,
                 QStringLiteral("detail skipped reason=content_repo_unavailable doc_id=%1").arg(normalizedDocId));
        logDetailPerf(normalizedDocId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("request_failed"),
                      QStringLiteral("reason=content_repo_unavailable"));
        markDetailTimingFailed(normalizedDocId,
                               requestId,
                               selectionTimestampMs,
                               QStringLiteral("reason=content_repo_unavailable"));
        return;
    }

    QElapsedTimer dataPrepareTimer;
    dataPrepareTimer.start();

    domain::adapters::ConclusionDetailViewData detailView;
    QJsonObject contentPayload;
    bool cacheHit = lookupCachedDetail(normalizedDocId, &detailView, &contentPayload);

    if (!cacheHit) {
        const auto* record = contentRepository_->getById(normalizedDocId);
        if (record == nullptr) {
            showDetailError(QStringLiteral("内容仓库中未找到结论 ID: %1").arg(normalizedDocId));
            LOG_WARN(LogCategory::DetailRender,
                     QStringLiteral("detail skipped reason=content_record_not_found doc_id=%1").arg(normalizedDocId));
            logDetailPerf(normalizedDocId,
                          requestId,
                          selectionTimestampMs,
                          QStringLiteral("request_failed"),
                          QStringLiteral("reason=content_record_not_found"));
            markDetailTimingFailed(normalizedDocId,
                                   requestId,
                                   selectionTimestampMs,
                                   QStringLiteral("reason=content_record_not_found"));
            return;
        }

        detailView = domain::adapters::ConclusionDetailAdapter::toViewData(*record);
        if (!detailView.isValid) {
            const QString errorMessage = detailView.errorMessage.trimmed().isEmpty()
                                             ? QStringLiteral("详情数据暂时不可用")
                                             : detailView.errorMessage.trimmed();
            showDetailError(errorMessage);
            LOG_WARN(LogCategory::DetailRender,
                     QStringLiteral("detail skipped reason=invalid_view_data doc_id=%1 error=%2")
                         .arg(normalizedDocId, errorMessage));
            logDetailPerf(normalizedDocId,
                          requestId,
                          selectionTimestampMs,
                          QStringLiteral("request_failed"),
                          QStringLiteral("reason=invalid_view_data"));
            markDetailTimingFailed(normalizedDocId,
                                   requestId,
                                   selectionTimestampMs,
                                   QStringLiteral("reason=invalid_view_data"));
            return;
        }

        if (detailViewDataMapper_ != nullptr) {
            contentPayload = detailViewDataMapper_->buildContentPayload(detailView, 0);
        }
        cacheDetail(normalizedDocId, detailView, contentPayload);
    }

    const qint64 dataPrepareMs = dataPrepareTimer.elapsed();
    logDetailPerf(normalizedDocId,
                  requestId,
                  selectionTimestampMs,
                  QStringLiteral("detail_payload_ready"),
                  QStringLiteral("dt=%1ms cache=%2 sections=%3")
                      .arg(dataPrepareMs)
                      .arg(cacheHit ? QStringLiteral("hit") : QStringLiteral("miss"))
                      .arg(detailView.sections.size()));
    logDetailPerf(normalizedDocId,
                  requestId,
                  selectionTimestampMs,
                  QStringLiteral("data_ready"),
                  QStringLiteral("cache=%1").arg(cacheHit ? QStringLiteral("hit") : QStringLiteral("miss")));

    if (detailRenderCoordinator_ != nullptr && detailRenderCoordinator_->isRequestStale(requestId)) {
        // Payload is ready but already obsolete; keep only the newest request alive.
        logDetailPerf(normalizedDocId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("request_superseded"),
                      QStringLiteral("reason=stale_after_payload"));
        markDetailTimingStaleIgnored(normalizedDocId,
                                     requestId,
                                     selectionTimestampMs,
                                     QStringLiteral("reason=stale_after_payload"));
        return;
    }

    const ui::detail::DetailRenderPath renderPath = ui::detail::DetailRenderPathResolver::resolve(
        isFeatureEnabled(license::Feature::FullDetail), webDetailEnabled_, detailPane_ != nullptr, detailViewDataMapper_ != nullptr);

    if (renderPath == ui::detail::DetailRenderPath::TrialPreview) {
        showTrialDetailPreview(detailView, normalizedDocId);
        if (detailRenderCoordinator_ != nullptr) {
            detailRenderCoordinator_->markRendered(normalizedDocId, requestId);
        }
        logDetailPerf(normalizedDocId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("total"),
                      QStringLiteral("dt=%1ms mode=trial_preview").arg(detailElapsedMs(selectionTimestampMs)));
        markDetailTimingSuccess(normalizedDocId, requestId, selectionTimestampMs);
        return;
    }

    if (renderPath == ui::detail::DetailRenderPath::Web) {
        QJsonObject payload = contentPayload;
        if (payload.isEmpty()) {
            payload = detailViewDataMapper_->buildContentPayload(detailView, 0);
        }
        payload.insert(QStringLiteral("requestId"), static_cast<qint64>(requestId));
        payload.insert(QStringLiteral("detailId"), normalizedDocId);

        dispatchPayloadToWeb(payload, normalizedDocId, requestId, selectionTimestampMs);
        if (detailRenderCoordinator_ != nullptr) {
            detailRenderCoordinator_->markRendered(normalizedDocId, requestId);
        }
        return;
    }

    renderDetailInFallbackBrowser(detailView);
    if (detailRenderCoordinator_ != nullptr) {
        detailRenderCoordinator_->markRendered(normalizedDocId, requestId);
    }
    logDetailPerf(normalizedDocId,
                  requestId,
                  selectionTimestampMs,
                  QStringLiteral("total"),
                  QStringLiteral("dt=%1ms mode=text_fallback").arg(detailElapsedMs(selectionTimestampMs)));
    markDetailTimingSuccess(normalizedDocId, requestId, selectionTimestampMs);
}

void SearchPage::renderDetailInFallbackBrowser(const domain::adapters::ConclusionDetailViewData& detailView)
{
    if (detailBrowser_ == nullptr) {
        return;
    }

    detailBrowser_->setHtml(ui::detail::DetailFallbackContentBuilder::buildFallbackHtml(detailView));
    detailBrowser_->setVisible(true);
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
}

void SearchPage::showDetailPlaceholder(const QString& message)
{
    const QString fallbackMessage = message.trimmed().isEmpty()
                                        ? QStringLiteral("请选择一条结果查看详情。")
                                        : message.trimmed();
    resetDetailTimingSessions(true);
    updateDetailShellMeta(detailMetaTextForPlaceholder(fallbackMessage), QStringLiteral("neutral"));
    if (detailRenderCoordinator_ != nullptr) {
        detailRenderCoordinator_->clearRenderedDetail();
    }
    currentDetailDocId_.clear();
    refreshFavoriteButtonState();

    if (webDetailEnabled_ && detailViewDataMapper_ != nullptr) {
        const QJsonObject payload = detailViewDataMapper_->buildEmptyPayload(fallbackMessage);
        dispatchPayloadToWeb(payload);
        return;
    }

    if (detailBrowser_ == nullptr) {
        return;
    }

    detailBrowser_->setHtml(QStringLiteral("<p style=\"color:#666;\">%1</p>").arg(fallbackMessage.toHtmlEscaped()));
    detailBrowser_->setVisible(true);
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
}

void SearchPage::showDetailError(const QString& message)
{
    const QString fallbackMessage = message.trimmed().isEmpty()
                                        ? QStringLiteral("详情暂时无法显示。")
                                        : message.trimmed();
    updateDetailShellMeta(fallbackMessage, QStringLiteral("error"));
    if (detailRenderCoordinator_ != nullptr) {
        detailRenderCoordinator_->clearRenderedDetail();
    }
    currentDetailDocId_.clear();
    refreshFavoriteButtonState();

    if (webDetailEnabled_ && detailViewDataMapper_ != nullptr) {
        const QJsonObject payload = detailViewDataMapper_->buildErrorPayload(fallbackMessage);
        dispatchPayloadToWeb(payload);
        return;
    }

    if (detailBrowser_ == nullptr) {
        return;
    }

    detailBrowser_->setHtml(QStringLiteral("<p style=\"color:#9a3412;\">%1</p>").arg(fallbackMessage.toHtmlEscaped()));
    detailBrowser_->setVisible(true);
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
}

void SearchPage::ensureDetailShellLoaded()
{
    if (!webDetailEnabled_ || detailPane_ == nullptr) {
        return;
    }
    detailPane_->ensureShellLoaded();
}

void SearchPage::dispatchPayloadToWeb(const QJsonObject& payload,
                                      const QString& docId,
                                      quint64 requestId,
                                      qint64 selectionTimestampMs)
{
    if (!webDetailEnabled_ || detailPane_ == nullptr) {
        return;
    }
    if (detailBrowser_ != nullptr) {
        detailBrowser_->setVisible(false);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(true);
    }
    ui::detail::DetailPane::RequestContext requestContext;
    requestContext.payload = payload;
    requestContext.detailId = docId.trimmed().isEmpty() ? payload.value(QStringLiteral("detailId")).toString().trimmed()
                                                        : docId.trimmed();
    requestContext.requestId = requestId;
    requestContext.selectionTimestampMs = selectionTimestampMs;
    detailPane_->renderDetail(requestContext);
}

void SearchPage::startDetailTimingSession(const QString& docId, quint64 requestId, qint64 selectionTimestampMs)
{
    const QString normalizedDocId = docId.trimmed();
    if (normalizedDocId.isEmpty() || requestId == 0 || selectionTimestampMs <= 0) {
        return;
    }

    if (activeDetailTimingRequestId_ > 0 && activeDetailTimingRequestId_ != requestId) {
        auto activeIt = detailTimingSessions_.find(activeDetailTimingRequestId_);
        if (activeIt != detailTimingSessions_.end() && activeIt->status == DetailTimingStatus::Loading) {
            activeIt->status = DetailTimingStatus::Stale;
        }
    }

    DetailTimingSession session;
    session.requestId = requestId;
    session.detailId = normalizedDocId;
    session.selectionTimestampMs = selectionTimestampMs;
    session.elapsedTimer.start();
    session.status = DetailTimingStatus::Loading;

    detailTimingSessions_.insert(requestId, session);
    activeDetailTimingRequestId_ = requestId;
    updateDetailTimingLabel(kDetailTimingLoadingText, kDetailTimingColorLoading);
    updateDetailShellMeta(QStringLiteral("正在加载详情..."), QStringLiteral("loading"));

    logDetailPerf(normalizedDocId, requestId, selectionTimestampMs, QStringLiteral("request_start"));

    const quint64 keepAfterRequestId = requestId > 96 ? requestId - 96 : 0;
    for (auto it = detailTimingSessions_.begin(); it != detailTimingSessions_.end();) {
        if (it.key() < keepAfterRequestId && it.key() != activeDetailTimingRequestId_) {
            it = detailTimingSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void SearchPage::markDetailTimingFailed(const QString& docId,
                                        quint64 requestId,
                                        qint64 selectionTimestampMs,
                                        const QString& reason)
{
    if (requestId == 0) {
        return;
    }

    auto it = detailTimingSessions_.find(requestId);
    if (it == detailTimingSessions_.end()) {
        DetailTimingSession session;
        session.requestId = requestId;
        session.detailId = docId.trimmed();
        session.selectionTimestampMs = selectionTimestampMs;
        session.elapsedTimer.start();
        session.status = DetailTimingStatus::Loading;
        detailTimingSessions_.insert(requestId, session);
        it = detailTimingSessions_.find(requestId);
    }

    if (it == detailTimingSessions_.end()) {
        return;
    }

    if (!docId.trimmed().isEmpty()) {
        it->detailId = docId.trimmed();
    }
    if (it->selectionTimestampMs <= 0 && selectionTimestampMs > 0) {
        it->selectionTimestampMs = selectionTimestampMs;
    }
    if (!it->elapsedTimer.isValid()) {
        it->elapsedTimer.start();
    }

    const qint64 elapsedMs = it->selectionTimestampMs > 0 ? detailElapsedMs(it->selectionTimestampMs)
                                                           : std::max<qint64>(0, it->elapsedTimer.elapsed());
    it->finalElapsedMs = elapsedMs;
    it->status = DetailTimingStatus::Failed;

    if (activeDetailTimingRequestId_ != requestId) {
        it->status = DetailTimingStatus::Stale;
        return;
    }

    Q_UNUSED(reason);
    updateDetailTimingLabel(kDetailTimingFailedText, kDetailTimingColorFailed);
    updateDetailShellMeta(QStringLiteral("详情加载失败"), QStringLiteral("error"));
}

void SearchPage::markDetailTimingSuccess(const QString& docId, quint64 requestId, qint64 selectionTimestampMs)
{
    if (requestId == 0) {
        return;
    }

    auto it = detailTimingSessions_.find(requestId);
    if (it == detailTimingSessions_.end()) {
        DetailTimingSession session;
        session.requestId = requestId;
        session.detailId = docId.trimmed();
        session.selectionTimestampMs = selectionTimestampMs;
        session.elapsedTimer.start();
        session.status = DetailTimingStatus::Loading;
        detailTimingSessions_.insert(requestId, session);
        it = detailTimingSessions_.find(requestId);
    }

    if (it == detailTimingSessions_.end()) {
        return;
    }

    if (!docId.trimmed().isEmpty()) {
        it->detailId = docId.trimmed();
    }
    if (it->selectionTimestampMs <= 0 && selectionTimestampMs > 0) {
        it->selectionTimestampMs = selectionTimestampMs;
    }
    if (!it->elapsedTimer.isValid()) {
        it->elapsedTimer.start();
    }

    const qint64 elapsedMs = it->selectionTimestampMs > 0 ? detailElapsedMs(it->selectionTimestampMs)
                                                           : std::max<qint64>(0, it->elapsedTimer.elapsed());
    it->finalElapsedMs = elapsedMs;
    if (it->jsRenderDoneMs < 0) {
        it->jsRenderDoneMs = elapsedMs;
    }

    // Only the active request is allowed to update UI timing text.
    if (activeDetailTimingRequestId_ != requestId
        || (detailRenderCoordinator_ != nullptr && detailRenderCoordinator_->isRequestStale(requestId))) {
        it->status = DetailTimingStatus::Stale;
        logDetailPerf(it->detailId,
                      requestId,
                      it->selectionTimestampMs,
                      QStringLiteral("request_stale_ignored"),
                      QStringLiteral("reason=completion_for_non_active_request active=%1").arg(activeDetailTimingRequestId_));
        return;
    }

    it->status = DetailTimingStatus::Success;

    QString statusText = QStringLiteral("详情耗时：%1 ms").arg(elapsedMs);
#ifndef NDEBUG
    if (it->dispatchToWebStartMs >= 0 && it->jsRenderDoneMs >= it->dispatchToWebStartMs) {
        const qint64 renderMs = std::max<qint64>(0, it->jsRenderDoneMs - it->dispatchToWebStartMs);
        statusText = QStringLiteral("详情耗时：%1 ms（Web: %2 ms，Render: %3 ms）")
                         .arg(elapsedMs)
                         .arg(it->dispatchToWebStartMs)
                         .arg(renderMs);
    }
#endif
    updateDetailTimingLabel(statusText, kDetailTimingColorSuccess);
    updateDetailShellMeta(QStringLiteral("详情已就绪"), QStringLiteral("success"));

    logDetailPerf(it->detailId,
                  requestId,
                  it->selectionTimestampMs,
                  QStringLiteral("detail_display_done"),
                  QStringLiteral("dt=%1ms").arg(elapsedMs));
}

void SearchPage::markDetailTimingStaleIgnored(const QString& docId,
                                              quint64 requestId,
                                              qint64 selectionTimestampMs,
                                              const QString& reason)
{
    if (requestId == 0) {
        return;
    }

    auto it = detailTimingSessions_.find(requestId);
    if (it == detailTimingSessions_.end()) {
        DetailTimingSession session;
        session.requestId = requestId;
        session.detailId = docId.trimmed();
        session.selectionTimestampMs = selectionTimestampMs;
        session.elapsedTimer.start();
        session.status = DetailTimingStatus::Stale;
        detailTimingSessions_.insert(requestId, session);
    } else {
        it->status = DetailTimingStatus::Stale;
        if (!docId.trimmed().isEmpty()) {
            it->detailId = docId.trimmed();
        }
        if (it->selectionTimestampMs <= 0 && selectionTimestampMs > 0) {
            it->selectionTimestampMs = selectionTimestampMs;
        }
    }

    logDetailPerf(docId.trimmed().isEmpty() ? QStringLiteral("-") : docId.trimmed(),
                  requestId,
                  selectionTimestampMs,
                  QStringLiteral("request_stale_ignored"),
                  reason.trimmed().isEmpty() ? QStringLiteral("reason=stale_request") : reason.trimmed());
}

void SearchPage::handleDetailPerfPhase(const QString& detailId,
                                       quint64 requestId,
                                       qint64 selectionTimestampMs,
                                       qint64 phaseAtMs,
                                       const QString& phase,
                                       const QString& extra)
{
    const QString normalizedPhase = phase.trimmed();
    if (requestId == 0 || normalizedPhase.isEmpty()) {
        return;
    }

    auto it = detailTimingSessions_.find(requestId);
    if (it == detailTimingSessions_.end()) {
        DetailTimingSession session;
        session.requestId = requestId;
        session.detailId = detailId.trimmed();
        session.selectionTimestampMs = selectionTimestampMs;
        session.elapsedTimer.start();
        session.status = DetailTimingStatus::Loading;
        detailTimingSessions_.insert(requestId, session);
        it = detailTimingSessions_.find(requestId);
    }
    if (it == detailTimingSessions_.end()) {
        return;
    }

    if (!detailId.trimmed().isEmpty()) {
        it->detailId = detailId.trimmed();
    }
    if (it->selectionTimestampMs <= 0 && selectionTimestampMs > 0) {
        it->selectionTimestampMs = selectionTimestampMs;
    }
    if (!it->elapsedTimer.isValid()) {
        it->elapsedTimer.start();
    }

    const qint64 elapsedMs = phaseAtMs >= 0
                                 ? phaseAtMs
                                 : (it->selectionTimestampMs > 0 ? detailElapsedMs(it->selectionTimestampMs)
                                                                  : std::max<qint64>(0, it->elapsedTimer.elapsed()));

    if (normalizedPhase == QStringLiteral("dispatch_to_web_start")) {
        it->dispatchToWebStartMs = elapsedMs;
        return;
    }
    if (normalizedPhase == QStringLiteral("web_load_finished")) {
        it->webLoadFinishedMs = elapsedMs;
        return;
    }
    if (normalizedPhase == QStringLiteral("js_render_start")) {
        it->jsRenderStartMs = elapsedMs;
        return;
    }
    if (normalizedPhase == QStringLiteral("js_render_done")) {
        it->jsRenderDoneMs = elapsedMs;
        markDetailTimingSuccess(it->detailId, requestId, it->selectionTimestampMs);
        return;
    }
    if (normalizedPhase == QStringLiteral("render_complete")) {
        // Backward-compatible fallback for builds where js_render_done is not emitted.
        if (it->jsRenderDoneMs < 0) {
            it->jsRenderDoneMs = elapsedMs;
            markDetailTimingSuccess(it->detailId, requestId, it->selectionTimestampMs);
        }
        return;
    }
    if (normalizedPhase == QStringLiteral("request_failed") || normalizedPhase == QStringLiteral("js_render_failed")) {
        markDetailTimingFailed(it->detailId, requestId, it->selectionTimestampMs, extra);
        return;
    }
    if (normalizedPhase == QStringLiteral("request_stale_ignored")
        || normalizedPhase == QStringLiteral("request_superseded")
        || normalizedPhase == QStringLiteral("render_aborted_due_to_newer_request")) {
        it->status = DetailTimingStatus::Stale;
    }
}

void SearchPage::resetDetailTimingSessions(bool clearLabelToIdle)
{
    detailTimingSessions_.clear();
    detailPerfAggregator_.clear();
    activeDetailTimingRequestId_ = 0;
    if (clearLabelToIdle) {
        updateDetailTimingLabel(kDetailTimingIdleText, kDetailTimingColorIdle);
    }
}

void SearchPage::updateDetailTimingLabel(const QString& text, const QString& colorHex)
{
    if (detailTimingLabel_ == nullptr) {
        return;
    }

    detailTimingLabel_->setText(text);
    detailTimingLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    const QString normalizedColor = colorHex.trimmed().isEmpty() ? kDetailTimingColorIdle : colorHex.trimmed();
    QString timingState = QStringLiteral("idle");
    if (normalizedColor == kDetailTimingColorLoading) {
        timingState = QStringLiteral("loading");
    } else if (normalizedColor == kDetailTimingColorSuccess) {
        timingState = QStringLiteral("success");
    } else if (normalizedColor == kDetailTimingColorFailed) {
        timingState = QStringLiteral("failed");
    }

    detailTimingLabel_->setProperty("timingState", timingState);
    repolishWidget(detailTimingLabel_);
}

bool SearchPage::lookupCachedDetail(const QString& docId,
                                    domain::adapters::ConclusionDetailViewData* detailView,
                                    QJsonObject* contentPayload)
{
    if (detailView == nullptr || contentPayload == nullptr) {
        return false;
    }

    const auto detailIt = detailViewCache_.constFind(docId);
    const auto payloadIt = detailPayloadCache_.constFind(docId);
    if (detailIt == detailViewCache_.constEnd() || payloadIt == detailPayloadCache_.constEnd()) {
        return false;
    }

    *detailView = detailIt.value();
    *contentPayload = payloadIt.value();
    touchDetailCacheKey(docId);
    return true;
}

void SearchPage::cacheDetail(const QString& docId,
                             const domain::adapters::ConclusionDetailViewData& detailView,
                             const QJsonObject& contentPayload)
{
    if (docId.trimmed().isEmpty()) {
        return;
    }

    detailViewCache_.insert(docId, detailView);
    detailPayloadCache_.insert(docId, contentPayload);
    touchDetailCacheKey(docId);

    while (detailCacheLru_.size() > kDetailCacheCapacity) {
        const QString evictedDocId = detailCacheLru_.front();
        detailCacheLru_.pop_front();
        detailViewCache_.remove(evictedDocId);
        detailPayloadCache_.remove(evictedDocId);
    }
}

void SearchPage::touchDetailCacheKey(const QString& docId)
{
    if (docId.isEmpty()) {
        return;
    }
    detailCacheLru_.removeAll(docId);
    detailCacheLru_.push_back(docId);
}

void SearchPage::clearDetailCaches()
{
    detailViewCache_.clear();
    detailPayloadCache_.clear();
    detailCacheLru_.clear();
}

void SearchPage::logDetailPerf(const QString& docId,
                               quint64 requestId,
                               qint64 selectionTimestampMs,
                               const QString& phase,
                               const QString& extra,
                               qint64 phaseAtMs)
{
    const QString normalizedPhase = phase.trimmed();
    if (requestId == 0 || normalizedPhase.isEmpty()) {
        return;
    }

    const QString normalizedDocId = docId.trimmed().isEmpty() ? QStringLiteral("-") : docId.trimmed();
    const QString displayPhase = ui::detail::detailperf::toDisplayPhase(normalizedPhase);
    const bool beginPhase = normalizedPhase == QStringLiteral("request_start")
                            || normalizedPhase == QStringLiteral("selection_received")
                            || normalizedPhase == QStringLiteral("detail_request_created");
    const bool finishPhase = ui::detail::detailperf::isFinishPhase(displayPhase);
    const bool cancelPhase = ui::detail::detailperf::isCancelPhase(displayPhase);
    const bool knownPhase = ui::detail::detailperf::isKnownPhase(normalizedPhase);

    if (!beginPhase && !finishPhase && !cancelPhase && !knownPhase && !detailPerfAggregator_.hasActiveRequest(requestId)) {
        return;
    }

    const qint64 fallbackAtMs = selectionTimestampMs > 0 ? detailElapsedMs(selectionTimestampMs) : 0;
    const qint64 atMs = phaseAtMs >= 0 ? phaseAtMs : fallbackAtMs;

    if (beginPhase) {
        detailPerfAggregator_.beginRequest(normalizedDocId, requestId);
    }

    if (displayPhase == QStringLiteral("superseded")) {
        const QVariantMap extras = ui::detail::detailperf::parsePerfExtra(extra);
        const quint64 oldRequestId = static_cast<quint64>(std::max<qint64>(0, extras.value(QStringLiteral("superseded_req")).toLongLong()));
        const QString oldDetailId = extras.value(QStringLiteral("superseded_id")).toString().trimmed();
        if (oldRequestId > 0) {
            detailPerfAggregator_.markSuperseded(oldRequestId, oldDetailId, requestId, normalizedDocId);
        } else {
            detailPerfAggregator_.cancelRequest(normalizedDocId, requestId, QStringLiteral("superseded"));
        }
        return;
    }

    if (cancelPhase) {
        const QVariantMap extras = ui::detail::detailperf::parsePerfExtra(extra);
        QString reason = extras.value(QStringLiteral("reason")).toString().trimmed();
        if (reason.isEmpty()) {
            reason = displayPhase == QStringLiteral("aborted_stale") ? QStringLiteral("superseded") : displayPhase;
        }
        detailPerfAggregator_.cancelRequest(normalizedDocId, requestId, reason);
        return;
    }

    if (finishPhase) {
        detailPerfAggregator_.finishRequest(normalizedDocId, requestId, normalizedPhase, atMs, extra);
        return;
    }

    detailPerfAggregator_.recordPhase(normalizedDocId, requestId, normalizedPhase, atMs, extra);
}

qint64 SearchPage::detailElapsedMs(qint64 selectionTimestampMs) const
{
    if (selectionTimestampMs <= 0) {
        return 0;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    return std::max<qint64>(0, nowMs - selectionTimestampMs);
}

void SearchPage::activateTextFallbackMode(const QString& reason)
{
    if (!webDetailEnabled_) {
        return;
    }

    webDetailEnabled_ = false;
    if (detailPane_ != nullptr) {
        detailPane_->disableWebMode();
    }

    LOG_ERROR(LogCategory::WebViewKatex,
              QStringLiteral("web_mode disabled fallback=text_browser reason=%1")
                  .arg(reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason.trimmed()));
    LOG_INFO(LogCategory::PerfWebView,
             QStringLiteral("event=web_mode_disabled reason=%1")
                 .arg(reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason.trimmed()));

    if (activeDetailTimingRequestId_ > 0) {
        const auto timingIt = detailTimingSessions_.constFind(activeDetailTimingRequestId_);
        const QString activeDetailId = timingIt == detailTimingSessions_.constEnd() ? QString() : timingIt->detailId;
        const qint64 activeSelectionTs = timingIt == detailTimingSessions_.constEnd() ? 0 : timingIt->selectionTimestampMs;
        logDetailPerf(activeDetailId.trimmed().isEmpty() ? QStringLiteral("-") : activeDetailId,
                      activeDetailTimingRequestId_,
                      activeSelectionTs,
                      QStringLiteral("request_failed"),
                      QStringLiteral("reason=web_mode_disabled"));
        markDetailTimingFailed(activeDetailId,
                               activeDetailTimingRequestId_,
                               activeSelectionTs,
                               QStringLiteral("reason=web_mode_disabled"));
    }

    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
    if (detailBrowser_ != nullptr) {
        detailBrowser_->setVisible(true);
    }

    const QString userMessage = webFallbackUserMessage(reason);
    updateStatusLine(QStringLiteral("详情 Web 渲染异常，已自动切换兼容模式。"), userMessage);
    showDetailError(userMessage);
}

bool SearchPage::isFeatureEnabled(license::Feature feature) const
{
    return featureGate_ == nullptr ? true : featureGate_->isEnabled(feature);
}

QString SearchPage::featureDisabledReason(license::Feature feature) const
{
    return featureGate_ == nullptr ? QString() : featureGate_->disabledReason(feature);
}

void SearchPage::applyFeatureGate()
{
    const bool advancedFilterEnabled = isFeatureEnabled(license::Feature::AdvancedFilter);
    if (!advancedFilterEnabled && moduleFilterCombo_ != nullptr && categoryFilterCombo_ != nullptr && tagFilterCombo_ != nullptr) {
        QSignalBlocker moduleBlocker(moduleFilterCombo_);
        QSignalBlocker categoryBlocker(categoryFilterCombo_);
        QSignalBlocker tagBlocker(tagFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(0);
        categoryFilterCombo_->setCurrentIndex(0);
        tagFilterCombo_->setCurrentIndex(0);
        lastSuggestSignature_.clear();
        lastSearchSignature_.clear();
    }

    if (moduleFilterCombo_ != nullptr) {
        moduleFilterCombo_->setEnabled(advancedFilterEnabled);
    }
    if (categoryFilterCombo_ != nullptr) {
        categoryFilterCombo_->setEnabled(advancedFilterEnabled);
    }
    if (tagFilterCombo_ != nullptr) {
        tagFilterCombo_->setEnabled(advancedFilterEnabled);
    }
    if (clearFiltersButton_ != nullptr) {
        clearFiltersButton_->setEnabled(advancedFilterEnabled);
        if (advancedFilterEnabled) {
            clearFiltersButton_->setToolTip(QString());
        } else {
            clearFiltersButton_->setToolTip(featureDisabledReason(license::Feature::AdvancedFilter));
        }
    }

    refreshFavoriteButtonState();
}

void SearchPage::refreshFavoriteButtonState(const QString& docId)
{
    if (favoriteButton_ == nullptr) {
        return;
    }

    const QString targetId = docId.trimmed().isEmpty() ? currentDetailDocId_.trimmed() : docId.trimmed();
    if (targetId.isEmpty()) {
        favoriteButton_->setText(QStringLiteral("收藏当前结论"));
        favoriteButton_->setEnabled(false);
        favoriteButton_->setToolTip(QStringLiteral("请先选择一条结论。"));
        return;
    }

    if (!isFeatureEnabled(license::Feature::Favorites)) {
        favoriteButton_->setText(QStringLiteral("收藏（正式版）"));
        favoriteButton_->setEnabled(false);
        favoriteButton_->setToolTip(featureDisabledReason(license::Feature::Favorites));
        return;
    }

    if (!favoritesRepository_.load()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("favorites load failed while refreshing button"));
    }

    const bool alreadyFavorited = favoritesRepository_.contains(targetId);
    favoriteButton_->setText(alreadyFavorited ? QStringLiteral("取消收藏") : QStringLiteral("收藏当前结论"));
    favoriteButton_->setEnabled(true);
    favoriteButton_->setToolTip(alreadyFavorited ? QStringLiteral("点击将当前结论从收藏中移除。")
                                                 : QStringLiteral("点击将当前结论加入收藏。"));
}

void SearchPage::showTrialDetailPreview(const domain::adapters::ConclusionDetailViewData& detailView, const QString& docId)
{
    const QString reason = featureDisabledReason(license::Feature::FullDetail);
    updateDetailShellMeta(QStringLiteral("详情预览（体验版）"), QStringLiteral("warning"));

    if (detailBrowser_ != nullptr) {
        detailBrowser_->setHtml(
            ui::detail::DetailFallbackContentBuilder::buildTrialPreviewHtml(detailView, docId, reason, 220));
        detailBrowser_->setVisible(true);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
}

void SearchPage::applySort(QVector<domain::models::SearchHit>* hits) const
{
    if (hits == nullptr || hits->isEmpty()) {
        return;
    }

    const SortMode sortMode = currentSortMode();
    switch (sortMode) {
    case SortMode::ScoreDesc:
        std::sort(hits->begin(), hits->end(), [](const domain::models::SearchHit& lhs, const domain::models::SearchHit& rhs) {
            if (std::fabs(lhs.score - rhs.score) > 1e-9) {
                return lhs.score > rhs.score;
            }
            const int titleCompare = lhs.title.compare(rhs.title, Qt::CaseInsensitive);
            if (titleCompare != 0) {
                return titleCompare < 0;
            }
            return lhs.docId < rhs.docId;
        });
        return;

    case SortMode::TitleAsc:
        std::sort(hits->begin(), hits->end(), [](const domain::models::SearchHit& lhs, const domain::models::SearchHit& rhs) {
            const int titleCompare = lhs.title.compare(rhs.title, Qt::CaseInsensitive);
            if (titleCompare != 0) {
                return titleCompare < 0;
            }
            if (std::fabs(lhs.score - rhs.score) > 1e-9) {
                return lhs.score > rhs.score;
            }
            return lhs.docId < rhs.docId;
        });
        return;

    case SortMode::DifficultyAsc:
        std::sort(hits->begin(), hits->end(), [](const domain::models::SearchHit& lhs, const domain::models::SearchHit& rhs) {
            if (std::fabs(lhs.difficulty - rhs.difficulty) > 1e-9) {
                return lhs.difficulty < rhs.difficulty;
            }
            if (std::fabs(lhs.score - rhs.score) > 1e-9) {
                return lhs.score > rhs.score;
            }
            return lhs.docId < rhs.docId;
        });
        return;

    case SortMode::DifficultyDesc:
        std::sort(hits->begin(), hits->end(), [](const domain::models::SearchHit& lhs, const domain::models::SearchHit& rhs) {
            if (std::fabs(lhs.difficulty - rhs.difficulty) > 1e-9) {
                return lhs.difficulty > rhs.difficulty;
            }
            if (std::fabs(lhs.score - rhs.score) > 1e-9) {
                return lhs.score > rhs.score;
            }
            return lhs.docId < rhs.docId;
        });
        return;
    }
}

SearchPage::SortMode SearchPage::currentSortMode() const
{
    if (sortCombo_ == nullptr) {
        return SortMode::ScoreDesc;
    }

    bool ok = false;
    const int rawValue = sortCombo_->currentData().toInt(&ok);
    if (!ok) {
        return SortMode::ScoreDesc;
    }
    return static_cast<SortMode>(rawValue);
}

QString SearchPage::selectedModuleFilter() const
{
    return moduleFilterCombo_ == nullptr ? QString() : moduleFilterCombo_->currentData().toString().trimmed();
}

QString SearchPage::selectedCategoryFilter() const
{
    return categoryFilterCombo_ == nullptr ? QString() : categoryFilterCombo_->currentData().toString().trimmed();
}

QString SearchPage::selectedTagFilter() const
{
    return tagFilterCombo_ == nullptr ? QString() : tagFilterCombo_->currentData().toString().trimmed();
}

QString SearchPage::filtersSignature() const
{
    return QStringLiteral("module=%1|category=%2|tag=%3")
        .arg(selectedModuleFilter(), selectedCategoryFilter(), selectedTagFilter());
}

QString SearchPage::buildSuggestSignature(const QString& query) const
{
    return QStringLiteral("suggest|q=%1|%2")
        .arg(domain::models::normalizeQueryText(query), filtersSignature());
}

QString SearchPage::buildSearchSignature(const QString& query) const
{
    return QStringLiteral("search|q=%1|%2")
        .arg(domain::models::normalizeQueryText(query), filtersSignature());
}

QStringList SearchPage::collectCategoryOptions() const
{
    QStringList categories;
    if (contentRepository_ == nullptr) {
        return categories;
    }

    contentRepository_->forEachRecord([&categories](const QString&, const domain::models::ConclusionRecord& record) {
        const QString category = record.meta.category.trimmed();
        if (!category.isEmpty()) {
            categories.push_back(category);
        }
    });

    return uniqueSortedCaseInsensitive(categories);
}

QStringList SearchPage::uniqueSortedCaseInsensitive(const QStringList& values)
{
    QSet<QString> deduped;
    deduped.reserve(values.size());
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            deduped.insert(trimmed);
        }
    }

    QStringList sorted = deduped.values();
    std::sort(sorted.begin(), sorted.end(), [](const QString& lhs, const QString& rhs) {
        const int compare = lhs.compare(rhs, Qt::CaseInsensitive);
        return compare == 0 ? lhs < rhs : compare < 0;
    });
    return sorted;
}

QString SearchPage::joinNonEmpty(const QStringList& values, const QString& separator)
{
    QStringList filtered;
    filtered.reserve(values.size());
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            filtered.push_back(trimmed);
        }
    }
    return filtered.join(separator);
}
