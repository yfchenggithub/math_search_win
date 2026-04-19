#include "ui/pages/search_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "domain/adapters/conclusion_card_adapter.h"
#include "domain/adapters/conclusion_detail_adapter.h"
#include "domain/services/search_service.h"
#include "domain/services/suggest_service.h"
#include "infrastructure/data/conclusion_content_repository.h"
#include "infrastructure/data/conclusion_index_repository.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kResultItemDocIdRole = Qt::UserRole + 1;

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

QString toHtmlParagraph(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QStringLiteral("<p>%1</p>").arg(trimmed.toHtmlEscaped().replace('\n', QStringLiteral("<br/>")));
}

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        if (!value.trimmed().isEmpty()) {
            return value.trimmed();
        }
    }
    return {};
}

}  // namespace

SearchPage::SearchPage(domain::services::SearchService* searchService,
                       domain::services::SuggestService* suggestService,
                       const infrastructure::data::ConclusionContentRepository* contentRepository,
                       const infrastructure::data::ConclusionIndexRepository* indexRepository,
                       QWidget* parent)
    : QWidget(parent),
      searchService_(searchService),
      suggestService_(suggestService),
      contentRepository_(contentRepository),
      indexRepository_(indexRepository)
{
    indexReady_ = (indexRepository_ != nullptr && indexRepository_->docCount() > 0);
    contentReady_ = (contentRepository_ != nullptr && contentRepository_->size() > 0);

    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("SearchPage constructed searchServiceNull=%1 suggestServiceNull=%2")
                 .arg(searchService_ == nullptr ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(suggestService_ == nullptr ? QStringLiteral("true") : QStringLiteral("false")));

    buildUi();
    connectSignals();
    rebuildFilterOptions();
    resetToEmptyState();
}

void SearchPage::setBackendStatus(bool indexReady, bool contentReady)
{
    indexReady_ = indexReady;
    contentReady_ = contentReady;

    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("setBackendStatus indexReady=%1 contentReady=%2")
                 .arg(indexReady_ ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(contentReady_ ? QStringLiteral("true") : QStringLiteral("false")));

    rebuildFilterOptions();
    lastSuggestSignature_.clear();
    lastSearchSignature_.clear();

    if (!indexReady_) {
        updateStatusLine(QStringLiteral("索引未就绪，当前无法搜索。"),
                         QStringLiteral("请检查 data/backend_search_index.json 加载日志。"));
        showDetailPlaceholder(QStringLiteral("索引未加载，无法展示搜索结果。"));
        return;
    }

    if (!contentReady_) {
        updateStatusLine(QStringLiteral("内容库未就绪，搜索可用但详情可能缺失。"));
        showDetailPlaceholder(QStringLiteral("内容库未加载，选中结果时可能无法渲染详情。"));
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
                 QStringLiteral("setInitialModule ignored unknown module=%1").arg(trimmedModule));
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

    LOG_INFO(LogCategory::SearchEngine, QStringLiteral("suggestion clicked text=%1").arg(suggestionText));

    suppressSuggestRefresh_ = true;
    queryInput_->setText(suggestionText);
    suppressSuggestRefresh_ = false;
    runSearch(suggestionText, QStringLiteral("suggest_click"));
}

void SearchPage::onResultSelectionChanged()
{
    if (resultList_ == nullptr) {
        return;
    }

    QListWidgetItem* currentItem = resultList_->currentItem();
    if (currentItem == nullptr) {
        showDetailPlaceholder(QStringLiteral("请选择左侧结果查看详情。"));
        return;
    }

    const QString docId = currentItem->data(kResultItemDocIdRole).toString().trimmed();
    if (docId.isEmpty()) {
        LOG_WARN(LogCategory::DetailRender, QStringLiteral("result item missing doc id"));
        showDetailPlaceholder(QStringLiteral("当前结果缺少结论 ID，无法渲染详情。"));
        return;
    }

    LOG_DEBUG(LogCategory::DetailRender, QStringLiteral("result selected docId=%1").arg(docId));
    renderDetailForDocId(docId);
}

void SearchPage::onFilterChanged()
{
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

    LOG_INFO(LogCategory::SearchEngine, QStringLiteral("filters cleared"));
    onFilterChanged();
}

void SearchPage::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(10);

    mainLayout->addWidget(new QLabel(QStringLiteral("搜索"), this));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    auto* queryRow = new QHBoxLayout();
    queryInput_ = new QLineEdit(leftPanel);
    queryInput_->setPlaceholderText(QStringLiteral("输入结论关键词，例如：不等式、对数、导数"));
    searchButton_ = new QPushButton(QStringLiteral("搜索"), leftPanel);
    queryRow->addWidget(queryInput_, 1);
    queryRow->addWidget(searchButton_);
    leftLayout->addLayout(queryRow);

    suggestionList_ = new QListWidget(leftPanel);
    suggestionList_->setMaximumHeight(140);
    suggestionList_->setVisible(false);
    leftLayout->addWidget(suggestionList_);

    auto* filterBox = new QGroupBox(QStringLiteral("筛选与排序"), leftPanel);
    auto* filterLayout = new QFormLayout(filterBox);
    moduleFilterCombo_ = new QComboBox(filterBox);
    categoryFilterCombo_ = new QComboBox(filterBox);
    tagFilterCombo_ = new QComboBox(filterBox);
    sortCombo_ = new QComboBox(filterBox);
    clearFiltersButton_ = new QPushButton(QStringLiteral("清空筛选"), filterBox);

    sortCombo_->addItem(QStringLiteral("按相关度"), static_cast<int>(SortMode::ScoreDesc));
    sortCombo_->addItem(QStringLiteral("按标题 A-Z"), static_cast<int>(SortMode::TitleAsc));
    sortCombo_->addItem(QStringLiteral("按难度 低到高"), static_cast<int>(SortMode::DifficultyAsc));
    sortCombo_->addItem(QStringLiteral("按难度 高到低"), static_cast<int>(SortMode::DifficultyDesc));

    filterLayout->addRow(QStringLiteral("模块"), moduleFilterCombo_);
    filterLayout->addRow(QStringLiteral("分类"), categoryFilterCombo_);
    filterLayout->addRow(QStringLiteral("标签"), tagFilterCombo_);
    filterLayout->addRow(QStringLiteral("排序"), sortCombo_);
    filterLayout->addRow(clearFiltersButton_);
    leftLayout->addWidget(filterBox);

    statusLabel_ = new QLabel(leftPanel);
    summaryLabel_ = new QLabel(leftPanel);
    leftLayout->addWidget(statusLabel_);
    leftLayout->addWidget(summaryLabel_);

    auto* resultBox = new QGroupBox(QStringLiteral("搜索结果"), leftPanel);
    auto* resultLayout = new QVBoxLayout(resultBox);
    resultList_ = new QListWidget(resultBox);
    resultLayout->addWidget(resultList_);
    leftLayout->addWidget(resultBox, 1);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    rightLayout->addWidget(new QLabel(QStringLiteral("详情（QTextBrowser 降级渲染）"), rightPanel));
    detailBrowser_ = new QTextBrowser(rightPanel);
    detailBrowser_->setOpenExternalLinks(false);
    rightLayout->addWidget(detailBrowser_, 1);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({560, 760});

    mainLayout->addWidget(splitter, 1);
}

void SearchPage::connectSignals()
{
    connect(queryInput_, &QLineEdit::textChanged, this, &SearchPage::onQueryTextChanged);
    connect(queryInput_, &QLineEdit::returnPressed, this, &SearchPage::onQueryReturnPressed);
    connect(searchButton_, &QPushButton::clicked, this, &SearchPage::onSearchButtonClicked);

    connect(suggestionList_, &QListWidget::itemClicked, this, &SearchPage::onSuggestionClicked);
    connect(suggestionList_, &QListWidget::itemActivated, this, &SearchPage::onSuggestionClicked);

    connect(resultList_, &QListWidget::itemSelectionChanged, this, &SearchPage::onResultSelectionChanged);

    connect(moduleFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(categoryFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(tagFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(sortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onSortChanged);
    connect(clearFiltersButton_, &QPushButton::clicked, this, &SearchPage::onClearFiltersClicked);
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
    showDetailPlaceholder(QStringLiteral("左侧输入关键词后可查看搜索结果和详情。"));
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
        LOG_WARN(LogCategory::SearchEngine, QStringLiteral("runSuggest skipped indexReady=%1").arg(indexReady_));
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

    LOG_DEBUG(LogCategory::SearchEngine,
              QStringLiteral("suggest done query=%1 total=%2 elapsed=%3ms")
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
        showDetailPlaceholder(QStringLiteral("索引未就绪，当前无法展示结果详情。"));
        LOG_ERROR(LogCategory::SearchEngine,
                  QStringLiteral("runSearch failed due to missing search backend query=%1").arg(normalizedQuery));
        return;
    }

    const QString signature = buildSearchSignature(normalizedQuery);
    if (signature == lastSearchSignature_) {
        LOG_DEBUG(LogCategory::SearchEngine, QStringLiteral("runSearch skipped duplicate signature query=%1").arg(normalizedQuery));
        return;
    }

    domain::models::SearchOptions options;
    options.maxResults = 120;

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
    const domain::models::SearchResult result = searchService_->search(normalizedQuery, options);
    const qint64 elapsedMs = timer.elapsed();

    currentHits_ = result.hits;
    applySort(&currentHits_);
    renderResults(currentHits_);
    clearSuggestions();
    lastSearchSignature_ = signature;

    const QString filterSummary =
        QStringLiteral("module=%1 | category=%2 | tag=%3")
            .arg(moduleFilter.isEmpty() ? QStringLiteral("all") : moduleFilter)
            .arg(categoryFilter.isEmpty() ? QStringLiteral("all") : categoryFilter)
            .arg(tagFilter.isEmpty() ? QStringLiteral("all") : tagFilter);

    if (currentHits_.isEmpty()) {
        updateStatusLine(QStringLiteral("没有找到相关结论。"),
                         QStringLiteral("query=%1 | total=0 | elapsed=%2ms | %3")
                             .arg(normalizedQuery)
                             .arg(elapsedMs)
                             .arg(filterSummary));
        showDetailPlaceholder(QStringLiteral("没有找到相关结论。建议尝试更短关键词或清空筛选。"));

        LOG_INFO(LogCategory::SearchEngine,
                 QStringLiteral("search done query=%1 total=0 elapsed=%2ms trigger=%3")
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

    LOG_INFO(LogCategory::SearchEngine,
             QStringLiteral("search done query=%1 total=%2 elapsed=%3ms trigger=%4")
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
    for (const domain::models::SearchHit& hit : hits) {
        const QString titleText = hit.title.trimmed().isEmpty() ? hit.docId : hit.title;
        const QString tagsText = hit.tags.isEmpty() ? QStringLiteral("-") : hit.tags.mid(0, 6).join(QStringLiteral(", "));
        const QString lineText =
            QStringLiteral("%1\nmodule=%2 | category=%3 | tags=%4 | score=%5")
                .arg(titleText,
                     hit.module,
                     hit.category,
                     tagsText,
                     QString::number(hit.score, 'f', 2));

        auto* item = new QListWidgetItem(lineText, resultList_);
        item->setData(kResultItemDocIdRole, hit.docId);

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
}

void SearchPage::renderDetailForDocId(const QString& docId)
{
    if (docId.trimmed().isEmpty()) {
        showDetailPlaceholder(QStringLiteral("当前结果缺少结论 ID，无法渲染详情。"));
        return;
    }

    if (!contentReady_ || contentRepository_ == nullptr) {
        showDetailPlaceholder(QStringLiteral("内容库未就绪，无法渲染详情。"));
        LOG_WARN(LogCategory::DetailRender,
                 QStringLiteral("detail skipped because content repo unavailable docId=%1").arg(docId));
        return;
    }

    const auto* record = contentRepository_->getById(docId);
    if (record == nullptr) {
        showDetailPlaceholder(QStringLiteral("内容库中不存在该结论 ID：%1").arg(docId));
        LOG_WARN(LogCategory::DetailRender, QStringLiteral("content record not found docId=%1").arg(docId));
        return;
    }

    const domain::adapters::ConclusionDetailViewData detailView =
        domain::adapters::ConclusionDetailAdapter::toViewData(*record);
    const domain::adapters::ConclusionCardViewData cardView = domain::adapters::ConclusionCardAdapter::toViewData(*record);

    const QString effectiveTitle = firstNonEmpty({detailView.title, cardView.title, docId});
    const QString effectiveSummary =
        firstNonEmpty({detailView.summary, detailView.plainSummary, detailView.plainStatement, cardView.summaryPlain});

    QStringList html;
    html.push_back(QStringLiteral("<h2>%1</h2>").arg(effectiveTitle.toHtmlEscaped()));
    html.push_back(QStringLiteral("<p><b>ID:</b> %1<br/><b>模块:</b> %2<br/><b>分类:</b> %3<br/><b>难度:</b> %4</p>")
                       .arg(detailView.id.toHtmlEscaped(),
                            detailView.module.toHtmlEscaped(),
                            detailView.category.toHtmlEscaped(),
                            QString::number(detailView.difficulty)));

    if (!detailView.tags.isEmpty()) {
        html.push_back(QStringLiteral("<p><b>标签:</b> %1</p>").arg(detailView.tags.join(QStringLiteral(" / ")).toHtmlEscaped()));
    }
    if (!detailView.aliases.isEmpty()) {
        html.push_back(QStringLiteral("<p><b>别名:</b> %1</p>").arg(detailView.aliases.join(QStringLiteral(" / ")).toHtmlEscaped()));
    }

    if (!effectiveSummary.isEmpty()) {
        html.push_back(QStringLiteral("<h3>摘要</h3>"));
        html.push_back(toHtmlParagraph(effectiveSummary));
    }

    if (!detailView.primaryFormula.trimmed().isEmpty()) {
        html.push_back(QStringLiteral("<h3>核心公式（文本）</h3>"));
        html.push_back(QStringLiteral("<pre>%1</pre>").arg(detailView.primaryFormula.toHtmlEscaped()));
    }

    const QString plainStatement =
        firstNonEmpty({detailView.plainStatement, detailView.plainExplanation, detailView.plainProof, detailView.plainExamples});
    if (!plainStatement.isEmpty()) {
        html.push_back(QStringLiteral("<h3>内容摘录</h3>"));
        html.push_back(toHtmlParagraph(plainStatement));
    }

    if (!detailView.sections.isEmpty()) {
        html.push_back(QStringLiteral("<h3>结构化章节</h3>"));
        html.push_back(QStringLiteral("<ul>"));
        for (const domain::adapters::DetailSectionViewData& section : detailView.sections) {
            const QString sectionTitle = firstNonEmpty({section.title, section.key, QStringLiteral("未命名章节")});
            html.push_back(QStringLiteral("<li>%1（blocks=%2）</li>")
                               .arg(sectionTitle.toHtmlEscaped())
                               .arg(section.blocks.size()));
        }
        html.push_back(QStringLiteral("</ul>"));
    }

    html.push_back(QStringLiteral("<hr/><p><i>当前为 QTextBrowser 降级渲染，后续可替换为 WebEngine 正式渲染。</i></p>"));

    detailBrowser_->setHtml(html.join(QString()));
    LOG_DEBUG(LogCategory::DetailRender, QStringLiteral("detail rendered docId=%1").arg(docId));
}

void SearchPage::showDetailPlaceholder(const QString& message)
{
    if (detailBrowser_ == nullptr) {
        return;
    }

    detailBrowser_->setHtml(
        QStringLiteral("<p style=\"color:#666;\">%1</p>").arg(message.trimmed().toHtmlEscaped()));
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
