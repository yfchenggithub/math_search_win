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
#include "shared/paths.h"
#include "ui/detail/detail_fallback_content_builder.h"
#include "ui/detail/detail_html_renderer.h"
#include "ui/detail/detail_pane.h"
#include "ui/detail/detail_render_coordinator.h"
#include "ui/detail/detail_render_path_resolver.h"
#include "ui/detail/detail_view_data_mapper.h"
#include "ui/style/app_style.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QRegularExpression>
#include <QShortcut>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QScrollBar>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

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
constexpr int kDetailFontScaleMinLevel = 0;
constexpr int kDetailFontScaleDefaultLevel = 1;
constexpr int kDetailFontScaleMaxLevel = 2;
constexpr int kDetailFontWheelTicksDefault = 0;
constexpr int kDetailWheelDeltaUnit = 120;
constexpr qreal kDetailWheelZoomStepRatio = 1.08;
constexpr qreal kDetailPdfZoomMinFactor = 0.08;
constexpr qreal kDetailPdfZoomMaxFactor = 32.0;
constexpr qreal kDetailWebZoomMinFactor = 0.25;
constexpr qreal kDetailWebZoomMaxFactor = 5.0;
const QString kDetailFullscreenEnterText = QStringLiteral("全屏");
const QString kDetailFullscreenExitText = QStringLiteral("退出全屏");

struct DetailZoomSnapshot {
    QString fontScaleToken;
    qreal pdfZoomFactor = 1.0;
    qreal webZoomFactor = 1.0;
};

int clampDetailFontScaleLevel(int level)
{
    return std::clamp(level, kDetailFontScaleMinLevel, kDetailFontScaleMaxLevel);
}

QString normalizeModuleCode(const QString& rawModule)
{
    QString normalized = rawModule.trimmed().toLower();
    int start = 0;
    while (start < normalized.size() && normalized.at(start).isDigit()) {
        ++start;
    }
    while (start < normalized.size()) {
        const QChar ch = normalized.at(start);
        if (ch == QChar('_') || ch == QChar('-') || ch.isSpace()) {
            ++start;
            continue;
        }
        break;
    }
    if (start > 0 && start < normalized.size()) {
        normalized = normalized.mid(start);
    }
    return normalized;
}

QString moduleDisplayName(const QString& rawModule)
{
    const QString trimmed = rawModule.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QString code = normalizeModuleCode(trimmed);
    if (code == QStringLiteral("set")) {
        return QStringLiteral("集合");
    }
    if (code == QStringLiteral("algebra")) {
        return QStringLiteral("代数");
    }
    if (code == QStringLiteral("function")) {
        return QStringLiteral("函数");
    }
    if (code == QStringLiteral("sequence")) {
        return QStringLiteral("数列");
    }
    if (code == QStringLiteral("conic")) {
        return QStringLiteral("圆锥曲线");
    }
    if (code == QStringLiteral("vector")) {
        return QStringLiteral("向量");
    }
    if (code == QStringLiteral("geometry")) {
        return QStringLiteral("几何");
    }
    if (code == QStringLiteral("geometry-plane")) {
        return QStringLiteral("平面几何");
    }
    if (code == QStringLiteral("plane-geometry")) {
        return QStringLiteral("平面几何");
    }
    if (code == QStringLiteral("geometry-solid")) {
        return QStringLiteral("立体几何");
    }
    if (code == QStringLiteral("solid-geometry")) {
        return QStringLiteral("立体几何");
    }
    if (code == QStringLiteral("inequality")) {
        return QStringLiteral("不等式");
    }
    if (code == QStringLiteral("probability-stat")) {
        return QStringLiteral("概率统计");
    }
    if (code == QStringLiteral("trigonometry")) {
        return QStringLiteral("三角函数");
    }
    if (code == QStringLiteral("final")) {
        return QStringLiteral("综合");
    }

    return trimmed;
}

QString detailFontScaleKey()
{
    return QString::fromLatin1(domain::models::AppSettingKeys::DetailFontScaleLevel);
}

QString detailFontWheelTicksKey()
{
    return QString::fromLatin1(domain::models::AppSettingKeys::DetailFontWheelTicks);
}

QString detailRenderModeKey()
{
    return QString::fromLatin1(domain::models::AppSettingKeys::DetailRenderMode);
}

QString detailRenderModeToken(ui::detail::DetailRenderMode mode)
{
    switch (mode) {
    case ui::detail::DetailRenderMode::Auto:
        return QStringLiteral("auto");
    case ui::detail::DetailRenderMode::Web:
        return QStringLiteral("web");
    default:
        return QStringLiteral("pdf");
    }
}

bool tryParseDetailRenderMode(const QString& raw, ui::detail::DetailRenderMode* parsed)
{
    if (parsed == nullptr) {
        return false;
    }

    const QString normalized = raw.trimmed().toLower();
    if (normalized == QStringLiteral("pdf")) {
        *parsed = ui::detail::DetailRenderMode::Pdf;
        return true;
    }
    if (normalized == QStringLiteral("web")) {
        *parsed = ui::detail::DetailRenderMode::Web;
        return true;
    }
    if (normalized == QStringLiteral("auto")) {
        *parsed = ui::detail::DetailRenderMode::Auto;
        return true;
    }
    return false;
}

QString detailPdfDirectoryFromEnvOrDefault()
{
    const QString envPath = qEnvironmentVariable("MATH_SEARCH_DETAIL_PDF_DIR").trimmed();
    if (envPath.isEmpty()) {
        return QDir(AppPaths::dataDir()).filePath(QStringLiteral("conclusion_pdfs"));
    }

    const QFileInfo envInfo(envPath);
    if (envInfo.isAbsolute()) {
        return QDir::cleanPath(QDir::fromNativeSeparators(envPath));
    }

    return QDir(AppPaths::appRoot()).filePath(QDir::cleanPath(QDir::fromNativeSeparators(envPath)));
}

qreal detailZoomFactorForLevel(int level)
{
    switch (clampDetailFontScaleLevel(level)) {
    case 0:
        return 1.14;
    case 1:
        return 1.30;
    case 2:
        return 1.46;
    default:
        return 1.30;
    }
}

QString detailFontScaleTokenForLevel(int level)
{
    switch (clampDetailFontScaleLevel(level)) {
    case 0:
        return QStringLiteral("small");
    case 2:
        return QStringLiteral("large");
    default:
        return QStringLiteral("medium");
    }
}

QString detailFontButtonTextForLevel(int level)
{
    switch (clampDetailFontScaleLevel(level)) {
    case 0:
        return QStringLiteral("Aa-");
    case 2:
        return QStringLiteral("Aa+");
    default:
        return QStringLiteral("Aa");
    }
}

DetailZoomSnapshot computeDetailZoomSnapshot(int detailFontScaleLevel, int detailFontWheelTicks)
{
    DetailZoomSnapshot snapshot;
    snapshot.fontScaleToken = detailFontScaleTokenForLevel(detailFontScaleLevel);

    const qreal baseZoomFactor = detailZoomFactorForLevel(detailFontScaleLevel);
    const qreal wheelZoomFactor = std::pow(kDetailWheelZoomStepRatio, static_cast<qreal>(detailFontWheelTicks));
    const qreal combinedZoomFactor = (std::isfinite(wheelZoomFactor) && wheelZoomFactor > 0.0)
                                         ? (baseZoomFactor * wheelZoomFactor)
                                         : (detailFontWheelTicks >= 0 ? std::numeric_limits<qreal>::max()
                                                                      : std::numeric_limits<qreal>::min());
    snapshot.pdfZoomFactor = std::clamp(combinedZoomFactor, kDetailPdfZoomMinFactor, kDetailPdfZoomMaxFactor);
    snapshot.webZoomFactor = std::clamp(combinedZoomFactor, kDetailWebZoomMinFactor, kDetailWebZoomMaxFactor);
    return snapshot;
}

QString detailFontButtonTipForLevel(int level)
{
    switch (clampDetailFontScaleLevel(level)) {
    case 0:
        return QStringLiteral("详情字体：小（非全屏默认，Ctrl+滚轮连续缩放）");
    case 2:
        return QStringLiteral("详情字体：大（全屏默认，Ctrl+滚轮连续缩放）");
    default:
        return QStringLiteral("详情字体：中（Ctrl+滚轮连续缩放）");
    }
}

int detailFontScaleLevelForFullscreen(bool fullscreen)
{
    return fullscreen ? kDetailFontScaleMaxLevel : kDetailFontScaleMinLevel;
}

int nextDetailFontScaleLevelByCycle(int currentLevel)
{
    const int clampedLevel = clampDetailFontScaleLevel(currentLevel);
    return clampedLevel <= kDetailFontScaleMinLevel ? kDetailFontScaleMaxLevel : (clampedLevel - 1);
}

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

bool isDevModeEnvEnabled()
{
    const QString appEnv = qEnvironmentVariable("APP_ENV").trimmed().toLower();
    return appEnv == QStringLiteral("dev")
        || appEnv == QStringLiteral("debug")
        || appEnv == QStringLiteral("development");
}

QStringList extractHighlightTerms(const QString& query)
{
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        return {};
    }

    QStringList terms = trimmedQuery.split(QRegularExpression(QStringLiteral("[\\s,，;；、]+")), Qt::SkipEmptyParts);
    QSet<QString> dedupe;
    QStringList uniqueTerms;
    uniqueTerms.reserve(terms.size());
    for (const QString& term : terms) {
        const QString normalized = term.trimmed();
        if (normalized.isEmpty() || dedupe.contains(normalized)) {
            continue;
        }
        dedupe.insert(normalized);
        uniqueTerms.push_back(normalized);
    }
    terms = uniqueTerms;
    std::sort(terms.begin(), terms.end(), [](const QString& lhs, const QString& rhs) {
        if (lhs.size() != rhs.size()) {
            return lhs.size() > rhs.size();
        }
        return lhs < rhs;
    });
    return terms;
}

QString limitTagText(const QStringList& tags, int maxVisible, const QString& separator)
{
    if (tags.isEmpty() || maxVisible <= 0) {
        return QStringLiteral("—");
    }

    QStringList normalizedTags;
    normalizedTags.reserve(tags.size());
    for (const QString& raw : tags) {
        const QString trimmed = raw.trimmed();
        if (!trimmed.isEmpty()) {
            normalizedTags.push_back(trimmed);
        }
    }
    if (normalizedTags.isEmpty()) {
        return QStringLiteral("—");
    }

    const int shownCount = std::min(maxVisible, static_cast<int>(normalizedTags.size()));
    QString text = normalizedTags.mid(0, shownCount).join(separator);
    const int hiddenCount = normalizedTags.size() - shownCount;
    if (hiddenCount > 0) {
        text.append(QStringLiteral(" +%1").arg(hiddenCount));
    }
    return text;
}

QStringList buildUsageTerms(const domain::models::SearchHit& hit,
                            const domain::adapters::ConclusionCardViewData* cardView)
{
    QStringList terms;
    QSet<QString> dedupe;

    const auto addTerm = [&terms, &dedupe](const QString& value) {
        const QString normalized = value.trimmed();
        if (normalized.isEmpty() || dedupe.contains(normalized)) {
            return;
        }
        dedupe.insert(normalized);
        terms.push_back(normalized);
    };

    if (cardView != nullptr) {
        for (const QString& keyword : cardView->searchKeywords) {
            addTerm(keyword);
        }
        for (const QString& alias : cardView->aliases) {
            addTerm(alias);
        }
        for (const QString& tag : cardView->tags) {
            addTerm(tag);
        }
    }

    for (const QString& tag : hit.tags) {
        addTerm(tag);
    }

    if (!hit.category.trimmed().isEmpty()) {
        addTerm(hit.category);
    }

    if (!hit.module.trimmed().isEmpty()) {
        addTerm(moduleDisplayName(hit.module));
    }

    while (terms.size() > 4) {
        terms.removeLast();
    }
    return terms;
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
    isDevMode_ = isDevModeEnvEnabled();
    detailPdfDirectory_ = detailPdfDirectoryFromEnvOrDefault();
    loadDetailFontScaleSetting();
    loadDetailRenderModeSetting();
    indexReady_ = (indexRepository_ != nullptr && indexRepository_->docCount() > 0);
    contentReady_ = (contentRepository_ != nullptr && contentRepository_->size() > 0);
    detailHtmlRenderer_ = std::make_unique<ui::detail::DetailHtmlRenderer>();
    detailRenderCoordinator_ = std::make_unique<ui::detail::DetailRenderCoordinator>();
    detailViewDataMapper_ = std::make_unique<ui::detail::DetailViewDataMapper>();

    LOG_DEBUG(LogCategory::SearchEngine,
              QStringLiteral("page constructed name=search search_service_null=%1 suggest_service_null=%2")
                  .arg(searchService_ == nullptr ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(suggestService_ == nullptr ? QStringLiteral("true") : QStringLiteral("false")));

    const bool mapLoaded = conclusionPdfMapLoader_.loadFromFile();
    if (!mapLoaded) {
        LOG_WARN(LogCategory::DetailRender,
                 QStringLiteral("pdf map load failed path=%1 reason=%2")
                     .arg(conclusionPdfMapLoader_.diagnostics().filePath,
                          conclusionPdfMapLoader_.diagnostics().fatalError));
    } else {
        LOG_INFO(LogCategory::DetailRender,
                 QStringLiteral("pdf map ready path=%1 entries=%2")
                     .arg(conclusionPdfMapLoader_.activeFilePath())
                     .arg(conclusionPdfMapLoader_.diagnostics().loadedEntryCount));
    }
    LOG_INFO(LogCategory::DetailRender,
             QStringLiteral("detail render mode=%1 pdf_dir=%2")
                 .arg(detailRenderModeToken(detailRenderMode_), detailPdfDirectory_));

    detailSelectionCoalesceTimer_ = new QTimer(this);
    detailSelectionCoalesceTimer_->setSingleShot(true);
    detailSelectionCoalesceTimer_->setInterval(kDetailSelectionCoalesceMs);

    buildUi();
    connectSignals();
    detailFontScaleLevel_ = detailFontScaleLevelForFullscreen(false);
    detailFontWheelTicks_ = kDetailFontWheelTicksDefault;
    applyDetailFontScale();
    persistDetailFontScaleSetting();
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

void SearchPage::hideEvent(QHideEvent* event)
{
    if (detailPaneFullscreen_) {
        leaveDetailFullscreen();
    }
    QWidget::hideEvent(event);
}

bool SearchPage::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::Wheel) {
        QObject* detailPdfViewport = detailPdfView_ == nullptr ? nullptr : detailPdfView_->viewport();
        QObject* detailBrowserViewport = detailBrowser_ == nullptr ? nullptr : detailBrowser_->viewport();
        const bool isDetailWheelTarget = watched == detailPdfView_
                                         || watched == detailWebView_
                                         || watched == detailBrowser_
                                         || watched == detailPdfViewport
                                         || watched == detailBrowserViewport;
        if (isDetailWheelTarget) {
            const auto* wheelEvent = static_cast<QWheelEvent*>(event);
            int deltaY = wheelEvent->angleDelta().y();
            if (deltaY == 0) {
                deltaY = wheelEvent->pixelDelta().y();
            }
            if (tryAdjustDetailFontScaleByWheelDelta(deltaY, wheelEvent->modifiers())) {
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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

    if (moduleFilterCombo_ != nullptr) {
        QSignalBlocker moduleBlocker(moduleFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(0);
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
    lastSearchQuery_ = queryInput_ == nullptr ? QString() : queryInput_->text().trimmed();
    renderResults(currentHits_);

    if (resultList_ != nullptr && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    } else {
        enqueueDetailRenderRequest(normalizedId);
    }

    const QString moduleText = doc->module.trimmed().isEmpty() ? QStringLiteral("未标注模块")
                                                                : moduleDisplayName(doc->module);
    if (isDevMode()) {
        updateStatusLine(QStringLiteral("已打开收藏结论。"),
                         QStringLiteral("conclusionId=%1 | module=%2").arg(normalizedId, moduleText));
    } else {
        updateStatusLine(QStringLiteral("已打开收藏结论。"),
                         QStringLiteral("模块：%1").arg(moduleText));
    }

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
        if (detailPaneFullscreen_) {
            leaveDetailFullscreen();
        }
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
        setDetailEmptyState(QStringLiteral("请先在左侧搜索并选择一个结论。\n"
                                           "选中后这里会显示结论详情、公式说明和高清 PDF。"));
        return;
    }

    const QString docId = currentItem->data(kResultItemDocIdRole).toString().trimmed();
    if (docId.isEmpty()) {
        LOG_WARN(LogCategory::DetailRender, QStringLiteral("detail select_failed reason=missing_doc_id"));
        showDetailError(QStringLiteral("当前结果缺少结论 ID，无法显示详情。"));
        return;
    }

    currentDetailDocId_ = docId;
    updateDetailToolbarState();
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
        updateStatusLine(QStringLiteral("筛选条件已更新。"), QStringLiteral("输入关键词后开始搜索。"));
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

    if (moduleFilterCombo_ == nullptr) {
        return;
    }

    {
        QSignalBlocker moduleBlocker(moduleFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(0);
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

void SearchPage::onDetailFontButtonClicked()
{
    detailFontScaleLevel_ = nextDetailFontScaleLevelByCycle(detailFontScaleLevel_);
    resetDetailWheelZoom();
    applyDetailFontScale();
    persistDetailFontScaleSetting();
}

bool SearchPage::tryAdjustDetailFontScaleByWheelDelta(int deltaY, Qt::KeyboardModifiers modifiers)
{
    if (!modifiers.testFlag(Qt::ControlModifier) || deltaY == 0) {
        return false;
    }

    int wheelSteps = deltaY / kDetailWheelDeltaUnit;
    if (wheelSteps == 0) {
        wheelSteps = deltaY > 0 ? 1 : -1;
    }

    const qint64 nextTicks64 = static_cast<qint64>(detailFontWheelTicks_) + static_cast<qint64>(wheelSteps);
    const qint64 clampedTicks64 = std::clamp(nextTicks64,
                                             static_cast<qint64>(std::numeric_limits<int>::min()),
                                             static_cast<qint64>(std::numeric_limits<int>::max()));
    const int nextTicks = static_cast<int>(clampedTicks64);
    if (nextTicks == detailFontWheelTicks_) {
        return true;
    }

    detailFontWheelTicks_ = nextTicks;
    applyDetailFontScale();
    persistDetailFontScaleSetting();
    return true;
}

void SearchPage::resetDetailWheelZoom()
{
    detailFontWheelTicks_ = kDetailFontWheelTicksDefault;
}

void SearchPage::onDetailFullscreenButtonClicked()
{
    if (currentDetailDocId_.trimmed().isEmpty()) {
        return;
    }

    if (detailPaneFullscreen_) {
        leaveDetailFullscreen();
        return;
    }
    enterDetailFullscreen();
}

void SearchPage::enterDetailFullscreen()
{
    if (detailPaneFullscreen_) {
        syncDetailFullscreenButtonState();
        return;
    }

    if (searchWorkbenchSplitter_ == nullptr || searchLeftColumn_ == nullptr || detailShell_ == nullptr
        || searchTopBar_ == nullptr) {
        LOG_WARN(LogCategory::DetailRender, QStringLiteral("detail pane fullscreen skipped reason=missing_ui_nodes"));
        return;
    }

    detailPaneNormalSplitterSizes_ = searchWorkbenchSplitter_->sizes();
    searchTopBar_->setVisible(false);
    searchLeftColumn_->setVisible(false);
    detailShell_->setVisible(true);
    searchWorkbenchSplitter_->setSizes({0, 1});

    detailPaneFullscreen_ = true;
    detailFontScaleLevel_ = detailFontScaleLevelForFullscreen(true);
    resetDetailWheelZoom();
    applyDetailFontScale();
    persistDetailFontScaleSetting();
    syncDetailFullscreenButtonState();
    LOG_INFO(LogCategory::DetailRender, QStringLiteral("detail pane fullscreen entered"));
}

void SearchPage::syncDetailFullscreenButtonState()
{
    if (detailFullscreenButton_ == nullptr) {
        return;
    }

    if (detailPaneFullscreen_) {
        detailFullscreenButton_->setText(kDetailFullscreenExitText);
        detailFullscreenButton_->setToolTip(QStringLiteral("退出详情全屏（Esc）"));
    } else {
        detailFullscreenButton_->setText(kDetailFullscreenEnterText);
        detailFullscreenButton_->setToolTip(QStringLiteral("详情区域全屏显示（F11）"));
    }
}

void SearchPage::leaveDetailFullscreen()
{
    if (!detailPaneFullscreen_) {
        syncDetailFullscreenButtonState();
        return;
    }

    if (searchTopBar_ != nullptr) {
        searchTopBar_->setVisible(true);
    }
    if (searchLeftColumn_ != nullptr) {
        searchLeftColumn_->setVisible(true);
    }
    if (detailShell_ != nullptr) {
        detailShell_->setVisible(true);
    }
    if (searchWorkbenchSplitter_ != nullptr) {
        if (detailPaneNormalSplitterSizes_.size() >= 2) {
            searchWorkbenchSplitter_->setSizes(detailPaneNormalSplitterSizes_);
        } else {
            searchWorkbenchSplitter_->setSizes({460, 1180});
        }
    }

    detailPaneFullscreen_ = false;
    detailFontScaleLevel_ = detailFontScaleLevelForFullscreen(false);
    resetDetailWheelZoom();
    applyDetailFontScale();
    persistDetailFontScaleSetting();
    syncDetailFullscreenButtonState();
    LOG_INFO(LogCategory::DetailRender, QStringLiteral("detail pane fullscreen exited"));
}

void SearchPage::onPdfPrevPageClicked()
{
    if (detailPdfView_ == nullptr || detailPdfView_->pageNavigator() == nullptr) {
        return;
    }

    jumpToPdfPage(detailPdfView_->pageNavigator()->currentPage() - 1);
}

void SearchPage::onPdfNextPageClicked()
{
    if (detailPdfView_ == nullptr || detailPdfView_->pageNavigator() == nullptr) {
        return;
    }

    jumpToPdfPage(detailPdfView_->pageNavigator()->currentPage() + 1);
}

void SearchPage::onPdfFitWidthClicked()
{
    applyPdfFitToWidth(false);
}

SearchPage::PdfExportCopyStatus SearchPage::exportPdfToPath(const QString& rawTargetPath, QString* normalizedTargetPath)
{
    const QString sourcePath = currentDetailPdfPath_.trimmed();
    const QFileInfo sourceInfo(sourcePath);
    if (sourcePath.isEmpty() || !sourceInfo.exists() || !sourceInfo.isFile()) {
        return PdfExportCopyStatus::MissingSource;
    }

    QString targetPath = rawTargetPath.trimmed();
    if (targetPath.isEmpty()) {
        return PdfExportCopyStatus::CopyFailed;
    }

    if (QFileInfo(targetPath).suffix().trimmed().isEmpty()) {
        targetPath.append(QStringLiteral(".pdf"));
    }

    const QString normalizedSource = sourceInfo.absoluteFilePath();
    const QString normalizedTarget = QFileInfo(targetPath).absoluteFilePath();
    if (normalizedTargetPath != nullptr) {
        *normalizedTargetPath = normalizedTarget;
    }

    if (QDir::cleanPath(normalizedSource) == QDir::cleanPath(normalizedTarget)) {
        return PdfExportCopyStatus::SourceTargetSame;
    }

    if (QFileInfo::exists(normalizedTarget) && !QFile::remove(normalizedTarget)) {
        return PdfExportCopyStatus::RemoveTargetFailed;
    }

    if (!QFile::copy(normalizedSource, normalizedTarget)) {
        return PdfExportCopyStatus::CopyFailed;
    }

    return PdfExportCopyStatus::Success;
}

#if defined(MATH_SEARCH_TESTS_SOURCE_DIR)
SearchPage::PdfExportCopyStatus SearchPage::exportPdfToPathForTest(const QString& rawTargetPath,
                                                                   QString* normalizedTargetPath)
{
    return exportPdfToPath(rawTargetPath, normalizedTargetPath);
}
#endif

void SearchPage::onPdfExportButtonClicked()
{
    const QString sourcePath = currentDetailPdfPath_.trimmed();
    if (sourcePath.isEmpty()) {
        updateStatusLine(QStringLiteral("当前无可导出的 PDF。"), QStringLiteral("请先打开一条 PDF 详情。"));
        updatePdfPageNavigationUi();
        return;
    }

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        updateStatusLine(QStringLiteral("导出失败：源 PDF 不存在。"), sourceInfo.absoluteFilePath());
        currentDetailPdfPath_.clear();
        updatePdfPageNavigationUi();
        return;
    }

    const QString docId = currentDetailDocId_.trimmed();
    const QString defaultName = docId.isEmpty() ? QStringLiteral("detail_export.pdf") : QStringLiteral("%1.pdf").arg(docId);
    QString targetPath = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("导出当前 PDF"),
                                                      QDir::home().filePath(defaultName),
                                                      QStringLiteral("PDF 文件 (*.pdf);;所有文件 (*.*)"));
    if (targetPath.trimmed().isEmpty()) {
        LOG_INFO(LogCategory::DetailRender,
                 QStringLiteral("pdf export canceled doc_id=%1").arg(docId.isEmpty() ? QStringLiteral("-") : docId));
        return;
    }

    QString normalizedTarget;
    const PdfExportCopyStatus exportStatus = exportPdfToPath(targetPath, &normalizedTarget);
    const auto statusToken = [exportStatus]() {
        switch (exportStatus) {
        case PdfExportCopyStatus::Success:
            return QStringLiteral("success");
        case PdfExportCopyStatus::MissingSource:
            return QStringLiteral("missing_source");
        case PdfExportCopyStatus::SourceTargetSame:
            return QStringLiteral("source_target_same");
        case PdfExportCopyStatus::RemoveTargetFailed:
            return QStringLiteral("remove_target_failed");
        case PdfExportCopyStatus::CopyFailed:
            return QStringLiteral("copy_failed");
        default:
            return QStringLiteral("unknown");
        }
    };

    if (exportStatus == PdfExportCopyStatus::MissingSource) {
        updateStatusLine(QStringLiteral("导出失败：源 PDF 不存在。"), sourceInfo.absoluteFilePath());
        currentDetailPdfPath_.clear();
        updatePdfPageNavigationUi();
        LOG_WARN(LogCategory::DetailRender,
                 QStringLiteral("pdf export failed status=%1 source=%2 target=%3")
                     .arg(statusToken(), sourceInfo.absoluteFilePath(), normalizedTarget));
        return;
    }
    if (exportStatus == PdfExportCopyStatus::SourceTargetSame) {
        updateStatusLine(QStringLiteral("PDF 已位于目标位置。"), normalizedTarget);
        LOG_INFO(LogCategory::DetailRender,
                 QStringLiteral("pdf export skipped status=%1 source=%2 target=%3")
                     .arg(statusToken(), sourceInfo.absoluteFilePath(), normalizedTarget));
        return;
    }
    if (exportStatus == PdfExportCopyStatus::RemoveTargetFailed) {
        updateStatusLine(QStringLiteral("导出失败：无法覆盖目标文件。"), normalizedTarget);
        LOG_WARN(LogCategory::DetailRender,
                 QStringLiteral("pdf export failed status=%1 source=%2 target=%3")
                     .arg(statusToken(), sourceInfo.absoluteFilePath(), normalizedTarget));
        return;
    }
    if (exportStatus == PdfExportCopyStatus::CopyFailed) {
        updateStatusLine(QStringLiteral("导出失败：文件复制失败。"), normalizedTarget);
        LOG_WARN(LogCategory::DetailRender,
                 QStringLiteral("pdf export failed status=%1 source=%2 target=%3")
                     .arg(statusToken(), sourceInfo.absoluteFilePath(), normalizedTarget));
        return;
    }

    LOG_INFO(LogCategory::DetailRender,
             QStringLiteral("pdf export finished status=%1 source=%2 target=%3")
                 .arg(statusToken(), sourceInfo.absoluteFilePath(), normalizedTarget));
    updateStatusLine(QStringLiteral("PDF 导出完成。"), normalizedTarget);
    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Information);
    messageBox.setWindowTitle(QStringLiteral("导出成功"));
    messageBox.setText(QStringLiteral("PDF 已导出到：\n%1").arg(QDir::toNativeSeparators(normalizedTarget)));

    QPushButton* openDirButton = messageBox.addButton(QStringLiteral("打开导出目录"), QMessageBox::ActionRole);
    QPushButton* okButton = messageBox.addButton(QStringLiteral("确定"), QMessageBox::AcceptRole);
    messageBox.setDefaultButton(okButton);
    messageBox.exec();

    if (messageBox.clickedButton() == openDirButton) {
        const QString exportDirectory = QFileInfo(normalizedTarget).absolutePath();
        const bool openOk = QDesktopServices::openUrl(QUrl::fromLocalFile(exportDirectory));
        if (!openOk) {
            updateStatusLine(QStringLiteral("PDF 导出完成，但无法打开导出目录。"),
                             QDir::toNativeSeparators(exportDirectory));
            QMessageBox::warning(this,
                                 QStringLiteral("打开目录失败"),
                                 QStringLiteral("无法打开目录：\n%1")
                                     .arg(QDir::toNativeSeparators(exportDirectory)));
        }
    }
}

void SearchPage::loadDetailFontScaleSetting()
{
    detailFontScaleLevel_ = kDetailFontScaleDefaultLevel;
    detailFontWheelTicks_ = kDetailFontWheelTicksDefault;

    const bool loaded = settingsRepository_.load();
    if (!loaded) {
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("detail font scale setting fallback to default reason=settings_load_failed"));
    }

    const QVariant savedValue = settingsRepository_.value(detailFontScaleKey(), kDetailFontScaleDefaultLevel);
    detailFontScaleLevel_ = clampDetailFontScaleLevel(savedValue.toInt());
    const QVariant savedWheelTicks = settingsRepository_.value(detailFontWheelTicksKey(), kDetailFontWheelTicksDefault);
    detailFontWheelTicks_ = savedWheelTicks.toInt();
}

void SearchPage::loadDetailRenderModeSetting()
{
    detailRenderMode_ = ui::detail::DetailRenderMode::Pdf;

    ui::detail::DetailRenderMode parsedMode = ui::detail::DetailRenderMode::Pdf;
    const QString envModeRaw = qEnvironmentVariable("MATH_SEARCH_DETAIL_RENDER_MODE").trimmed();
    if (!envModeRaw.isEmpty()) {
        if (tryParseDetailRenderMode(envModeRaw, &parsedMode)) {
            detailRenderMode_ = parsedMode;
            return;
        }

        LOG_WARN(LogCategory::Config,
                 QStringLiteral("invalid detail render mode from env value=%1 fallback=pdf").arg(envModeRaw));
    }

    const QString savedModeRaw =
        settingsRepository_.value(detailRenderModeKey(), QStringLiteral("pdf")).toString().trimmed();
    if (tryParseDetailRenderMode(savedModeRaw, &parsedMode)) {
        detailRenderMode_ = parsedMode;
        return;
    }

    if (!savedModeRaw.isEmpty()) {
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("invalid detail render mode from settings value=%1 fallback=pdf").arg(savedModeRaw));
    }
}

static QString detailPageIndicatorText(int currentPage, int pageCount)
{
    if (pageCount <= 0 || currentPage < 0) {
        return QStringLiteral("PDF --/--");
    }

    return QStringLiteral("PDF %1/%2").arg(currentPage + 1).arg(pageCount);
}

qreal computePdfFitWidthZoomFactor(const QPdfDocument* document, const QPdfView* view)
{
    if (document == nullptr || view == nullptr || document->pageCount() <= 0 || view->viewport() == nullptr) {
        return 1.0;
    }

    int pageIndex = 0;
    if (view->pageNavigator() != nullptr) {
        pageIndex = std::clamp(view->pageNavigator()->currentPage(), 0, document->pageCount() - 1);
    }

    const QSizeF pageSizePt = document->pagePointSize(pageIndex);
    const qreal pageWidthPt = pageSizePt.width();
    if (pageWidthPt <= 0.0) {
        return 1.0;
    }

    const QMargins margins = view->documentMargins();
    const int viewportWidth = view->viewport()->width();
    const int availableWidth = viewportWidth - margins.left() - margins.right();
    if (availableWidth <= 0) {
        return 1.0;
    }

    return static_cast<qreal>(availableWidth) / pageWidthPt;
}

void SearchPage::applyDetailFontScale()
{
    detailFontScaleLevel_ = clampDetailFontScaleLevel(detailFontScaleLevel_);
    const DetailZoomSnapshot zoomSnapshot = computeDetailZoomSnapshot(detailFontScaleLevel_, detailFontWheelTicks_);

    if (detailPdfView_ != nullptr) {
        qreal targetPdfZoom = zoomSnapshot.pdfZoomFactor;
        const bool canScalePdf = detailPdfDocument_ != nullptr && detailPdfDocument_->pageCount() > 0
                                 && detailPdfView_->isVisible();
        if (canScalePdf) {
            const qreal fitZoom = computePdfFitWidthZoomFactor(detailPdfDocument_, detailPdfView_);
            if (fitZoom > 0.0) {
                detailPdfFitWidthBaseZoom_ = fitZoom;
            }
            const qreal fitBase = std::clamp(detailPdfFitWidthBaseZoom_, kDetailPdfZoomMinFactor, kDetailPdfZoomMaxFactor);
            targetPdfZoom = std::clamp(fitBase * zoomSnapshot.pdfZoomFactor, kDetailPdfZoomMinFactor, kDetailPdfZoomMaxFactor);
        }
        detailPdfView_->setZoomMode(QPdfView::ZoomMode::Custom);
        detailPdfView_->setZoomFactor(targetPdfZoom);
    }

    if (detailWebView_ != nullptr) {
        detailWebView_->setZoomFactor(zoomSnapshot.webZoomFactor);
    }

    if (detailBrowser_ != nullptr) {
        detailBrowser_->setProperty("fontScale", zoomSnapshot.fontScaleToken);
        repolishWidget(detailBrowser_);

        const int browserTickDelta = detailFontWheelTicks_ - detailBrowserAppliedWheelTicks_;
        if (browserTickDelta > 0) {
            detailBrowser_->zoomIn(browserTickDelta);
            detailBrowserAppliedWheelTicks_ = detailFontWheelTicks_;
        } else if (browserTickDelta < 0) {
            detailBrowser_->zoomOut(-browserTickDelta);
            detailBrowserAppliedWheelTicks_ = detailFontWheelTicks_;
        }
    }

    if (detailFontButton_ != nullptr) {
        detailFontButton_->setProperty("fontScale", zoomSnapshot.fontScaleToken);
        detailFontButton_->setText(detailFontButtonTextForLevel(detailFontScaleLevel_));
        detailFontButton_->setToolTip(detailFontButtonTipForLevel(detailFontScaleLevel_));
        repolishWidget(detailFontButton_);
    }
}

void SearchPage::persistDetailFontScaleSetting()
{
    settingsRepository_.setValue(detailFontScaleKey(), clampDetailFontScaleLevel(detailFontScaleLevel_));
    settingsRepository_.setValue(detailFontWheelTicksKey(), detailFontWheelTicks_);
}

void SearchPage::buildUi()
{
    detailPaneFullscreen_ = false;
    detailPaneNormalSplitterSizes_.clear();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                   ui::style::tokens::kPageOuterMargin,
                                   ui::style::tokens::kPageOuterMargin,
                                   ui::style::tokens::kPageOuterMargin);
    mainLayout->setSpacing(ui::style::tokens::kMediumSpacing);

    auto* topBar = new QWidget(this);
    searchTopBar_ = topBar;
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
        new QLabel(QStringLiteral("输入关键词即可定位结论，本地离线可用，学习过程更省心。"), titleBlock);
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
    queryInput_->setClearButtonEnabled(true);
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
    searchWorkbenchSplitter_ = splitter;
    splitter->setObjectName(QStringLiteral("searchWorkbenchSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    auto* leftPanel = new QWidget(splitter);
    searchLeftColumn_ = leftPanel;
    leftPanel->setObjectName(QStringLiteral("searchLeftColumn"));
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ui::style::tokens::kMediumSpacing);

    auto* filterPanel = new QWidget(leftPanel);
    filterPanel->setObjectName(QStringLiteral("searchFilterPanel"));
    filterPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto* filterPanelLayout = new QVBoxLayout(filterPanel);
    filterPanelLayout->setContentsMargins(14, 12, 14, 12);
    filterPanelLayout->setSpacing(8);

    auto* filterHeaderRow = new QHBoxLayout();
    filterHeaderRow->setContentsMargins(0, 0, 0, 0);
    filterHeaderRow->setSpacing(8);

    auto* filterTitle = new QLabel(QStringLiteral("快速筛选"), filterPanel);
    filterTitle->setObjectName(QStringLiteral("searchFilterTitle"));
    filterHeaderRow->addWidget(filterTitle);
    filterHeaderRow->addStretch(1);

    clearFiltersButton_ = new QPushButton(QStringLiteral("重置"), filterPanel);
    clearFiltersButton_->setObjectName(QStringLiteral("searchFilterResetButton"));
    clearFiltersButton_->setCursor(Qt::PointingHandCursor);
    filterHeaderRow->addWidget(clearFiltersButton_, 0, Qt::AlignRight);
    filterPanelLayout->addLayout(filterHeaderRow);

    auto* filterHint = new QLabel(QStringLiteral("先选范围，再按排序浏览结果。"), filterPanel);
    filterHint->setObjectName(QStringLiteral("searchFilterHint"));
    filterHint->setWordWrap(true);
    filterPanelLayout->addWidget(filterHint);

    moduleFilterCombo_ = new QComboBox(filterPanel);
    sortCombo_ = new QComboBox(filterPanel);
    moduleFilterCombo_->setObjectName(QStringLiteral("searchFilterCombo"));
    sortCombo_->setObjectName(QStringLiteral("searchFilterCombo"));
    sortCombo_->setProperty("comboRole", QStringLiteral("sort"));

    sortCombo_->addItem(QStringLiteral("按相关度"), static_cast<int>(SortMode::ScoreDesc));
    sortCombo_->addItem(QStringLiteral("按标题 A-Z"), static_cast<int>(SortMode::TitleAsc));
    sortCombo_->addItem(QStringLiteral("按难度 低到高"), static_cast<int>(SortMode::DifficultyAsc));
    sortCombo_->addItem(QStringLiteral("按难度 高到低"), static_cast<int>(SortMode::DifficultyDesc));

    auto* filterGrid = new QGridLayout();
    filterGrid->setContentsMargins(0, 0, 0, 0);
    filterGrid->setHorizontalSpacing(8);
    filterGrid->setVerticalSpacing(8);

    auto createFilterCell = [filterPanel](const QString& labelText, QComboBox* combo) {
        auto* cell = new QWidget(filterPanel);
        cell->setObjectName(QStringLiteral("searchFilterCell"));
        cell->setAttribute(Qt::WA_StyledBackground, true);

        auto* cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(8, 7, 8, 7);
        cellLayout->setSpacing(4);

        auto* label = new QLabel(labelText, cell);
        label->setObjectName(QStringLiteral("searchFilterFieldLabel"));
        cellLayout->addWidget(label);
        cellLayout->addWidget(combo);
        return cell;
    };

    filterGrid->addWidget(createFilterCell(QStringLiteral("模块"), moduleFilterCombo_), 0, 0);
    filterGrid->addWidget(createFilterCell(QStringLiteral("排序"), sortCombo_), 0, 1);
    filterGrid->setColumnStretch(0, 1);
    filterGrid->setColumnStretch(1, 1);

    filterPanelLayout->addLayout(filterGrid);
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
    resultList_->setUniformItemSizes(false);
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
    detailShell_ = detailShell;
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
    detailFontButton_ = new QPushButton(detailHeader);
    detailFontButton_->setObjectName(QStringLiteral("detailFontSizeButton"));
    detailFontButton_->setCursor(Qt::PointingHandCursor);
    detailFontButton_->setText(detailFontButtonTextForLevel(detailFontScaleLevel_));
    detailFontButton_->setToolTip(detailFontButtonTipForLevel(detailFontScaleLevel_));
    detailFontButton_->setProperty("fontScale", detailFontScaleTokenForLevel(detailFontScaleLevel_));

    detailFullscreenButton_ = new QPushButton(kDetailFullscreenEnterText, detailHeader);
    detailFullscreenButton_->setObjectName(QStringLiteral("detailPdfNavButton"));
    detailFullscreenButton_->setCursor(Qt::PointingHandCursor);
    detailFullscreenButton_->setToolTip(QStringLiteral("详情区域全屏显示（F11）"));

    detailPdfPrevButton_ = new QPushButton(QStringLiteral("上一页"), detailHeader);
    detailPdfPrevButton_->setObjectName(QStringLiteral("detailPdfNavButton"));
    detailPdfPrevButton_->setCursor(Qt::PointingHandCursor);
    detailPdfPrevButton_->setEnabled(false);
    detailPdfPrevButton_->setToolTip(QStringLiteral("跳转到上一页 PDF"));

    detailPdfPageLabel_ = new QLabel(QStringLiteral("PDF --/--"), detailHeader);
    detailPdfPageLabel_->setObjectName(QStringLiteral("detailPdfPageLabel"));
    detailPdfPageLabel_->setAlignment(Qt::AlignCenter);

    detailPdfNextButton_ = new QPushButton(QStringLiteral("下一页"), detailHeader);
    detailPdfNextButton_->setObjectName(QStringLiteral("detailPdfNavButton"));
    detailPdfNextButton_->setCursor(Qt::PointingHandCursor);
    detailPdfNextButton_->setEnabled(false);
    detailPdfNextButton_->setToolTip(QStringLiteral("跳转到下一页 PDF"));

    detailPdfFitWidthButton_ = new QPushButton(QStringLiteral("适合宽度"), detailHeader);
    detailPdfFitWidthButton_->setObjectName(QStringLiteral("detailPdfNavButton"));
    detailPdfFitWidthButton_->setCursor(Qt::PointingHandCursor);
    detailPdfFitWidthButton_->setEnabled(false);
    detailPdfFitWidthButton_->setToolTip(QStringLiteral("将 PDF 调整为适合当前详情宽度"));

    detailPdfExportButton_ = new QPushButton(QStringLiteral("导出PDF"), detailHeader);
    detailPdfExportButton_->setObjectName(QStringLiteral("detailPdfNavButton"));
    detailPdfExportButton_->setCursor(Qt::PointingHandCursor);
    detailPdfExportButton_->setEnabled(false);
    detailPdfExportButton_->setToolTip(QStringLiteral("将当前 PDF 另存为文件"));

    favoriteButton_ = new QPushButton(QStringLiteral("收藏当前结论"), detailHeader);
    favoriteButton_->setObjectName(QStringLiteral("searchClearFiltersButton"));
    favoriteButton_->setCursor(Qt::PointingHandCursor);
    favoriteButton_->setEnabled(false);

    auto* detailHeaderRight = new QWidget(detailHeader);
    auto* detailHeaderRightLayout = new QVBoxLayout(detailHeaderRight);
    detailHeaderRightLayout->setContentsMargins(0, 0, 0, 0);
    detailHeaderRightLayout->setSpacing(4);

    auto* detailActionRow = new QHBoxLayout();
    detailActionRow->setContentsMargins(0, 0, 0, 0);
    detailActionRow->setSpacing(6);
    detailActionRow->addWidget(detailFontButton_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(detailFullscreenButton_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(detailPdfPrevButton_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(detailPdfPageLabel_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(detailPdfNextButton_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(detailPdfFitWidthButton_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(detailPdfExportButton_, 0, Qt::AlignVCenter);
    detailActionRow->addWidget(favoriteButton_, 0, Qt::AlignVCenter);
    detailHeaderRightLayout->addLayout(detailActionRow);
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

    detailPdfDocument_ = new QPdfDocument(detailBody);
    detailPdfView_ = new QPdfView(detailBody);
    detailPdfView_->setObjectName(QStringLiteral("detailPdfView"));
    detailPdfView_->setVisible(false);
    detailPdfView_->setDocument(detailPdfDocument_);
    detailPdfView_->setPageMode(QPdfView::PageMode::MultiPage);
    detailBodyLayout->addWidget(detailPdfView_, 1);

    detailWebView_ = new QWebEngineView(detailBody);
    detailWebView_->setObjectName(QStringLiteral("detailWebView"));
    detailWebView_->setVisible(false);
    detailBodyLayout->addWidget(detailWebView_, 1);

    detailBrowser_ = new QTextBrowser(detailBody);
    detailBrowser_->setObjectName(QStringLiteral("detailFallbackView"));
    detailBrowser_->setOpenExternalLinks(false);
    detailBrowser_->setVisible(false);
    detailBodyLayout->addWidget(detailBrowser_, 1);

    if (detailPdfView_ != nullptr) {
        detailPdfView_->installEventFilter(this);
        if (detailPdfView_->viewport() != nullptr) {
            detailPdfView_->viewport()->installEventFilter(this);
        }
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->installEventFilter(this);
    }
    if (detailBrowser_ != nullptr) {
        detailBrowser_->installEventFilter(this);
        if (detailBrowser_->viewport() != nullptr) {
            detailBrowser_->viewport()->installEventFilter(this);
        }
    }

    rightLayout->addWidget(detailBody, 1);
    updateDetailTimingLabel(kDetailTimingIdleText, kDetailTimingColorIdle);
    updateDetailShellMeta(QStringLiteral("等待选择结果"), QStringLiteral("neutral"));

    pdfDetailEnabled_ = (detailPdfView_ != nullptr && detailPdfDocument_ != nullptr);
    webDetailEnabled_ = detailHtmlRenderer_ != nullptr && detailHtmlRenderer_->isReady();
    if (webDetailEnabled_) {
        detailPane_ = std::make_unique<ui::detail::DetailPane>(detailWebView_, detailBrowser_, detailHtmlRenderer_.get());
        webDetailEnabled_ = detailPane_ != nullptr && detailPane_->isWebModeEnabled();
    }

    if (webDetailEnabled_ && detailRenderMode_ == ui::detail::DetailRenderMode::Web) {
        detailWebView_->setVisible(true);
        LOG_DEBUG(LogCategory::WebViewKatex,
                  QStringLiteral("web_mode enabled detail_dir=%1 template=%2")
                      .arg(detailHtmlRenderer_->detailDirectory(), detailHtmlRenderer_->detailTemplatePath()));
        LOG_DEBUG(LogCategory::PerfWebView, QStringLiteral("event=web_mode_enabled mode=web"));
    } else {
        detailBrowser_->setVisible(true);
        if (!webDetailEnabled_) {
            LOG_WARN(LogCategory::WebViewKatex,
                     QStringLiteral("renderer unavailable mode=text_fallback reason=%1")
                         .arg(detailHtmlRenderer_ == nullptr ? QStringLiteral("detail renderer is null")
                                                             : detailHtmlRenderer_->lastError()));
        }
    }

    splitter->addWidget(leftPanel);
    splitter->addWidget(detailShell);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 19);
    splitter->setSizes({460, 1180});

    workbenchLayout->addWidget(splitter, 1);
    mainLayout->addWidget(workbench, 1);

    detailFullscreenShortcut_ = new QShortcut(QKeySequence(Qt::Key_F11), this);
    detailFullscreenShortcut_->setContext(Qt::WidgetWithChildrenShortcut);
    detailExitFullscreenShortcut_ = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    detailExitFullscreenShortcut_->setContext(Qt::WidgetWithChildrenShortcut);

    updatePdfPageNavigationUi();
    syncDetailFullscreenButtonState();
    updateDetailToolbarState();
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
    connect(sortCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onSortChanged);
    connect(clearFiltersButton_, &QPushButton::clicked, this, &SearchPage::onClearFiltersClicked);
    connect(favoriteButton_, &QPushButton::clicked, this, &SearchPage::onFavoriteButtonClicked);
    connect(detailFontButton_, &QPushButton::clicked, this, &SearchPage::onDetailFontButtonClicked);
    connect(detailFullscreenButton_, &QPushButton::clicked, this, &SearchPage::onDetailFullscreenButtonClicked);
    connect(detailPdfPrevButton_, &QPushButton::clicked, this, &SearchPage::onPdfPrevPageClicked);
    connect(detailPdfNextButton_, &QPushButton::clicked, this, &SearchPage::onPdfNextPageClicked);
    connect(detailPdfFitWidthButton_, &QPushButton::clicked, this, &SearchPage::onPdfFitWidthClicked);
    connect(detailPdfExportButton_, &QPushButton::clicked, this, &SearchPage::onPdfExportButtonClicked);
    connect(detailFullscreenShortcut_, &QShortcut::activated, this, &SearchPage::onDetailFullscreenButtonClicked);
    connect(detailExitFullscreenShortcut_, &QShortcut::activated, this, [this]() {
        if (detailPaneFullscreen_) {
            leaveDetailFullscreen();
        }
    });

    if (detailPdfDocument_ != nullptr) {
        connect(detailPdfDocument_, &QPdfDocument::pageCountChanged, this, [this](int) { updatePdfPageNavigationUi(); });
    }
    if (detailPdfView_ != nullptr && detailPdfView_->pageNavigator() != nullptr) {
        connect(detailPdfView_->pageNavigator(),
                &QPdfPageNavigator::currentPageChanged,
                this,
                [this](int) { updatePdfPageNavigationUi(); });
    }

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
    if (moduleFilterCombo_ == nullptr) {
        return;
    }

    const QString currentModule = selectedModuleFilter();

    {
        QSignalBlocker blocker(moduleFilterCombo_);
        moduleFilterCombo_->clear();
        moduleFilterCombo_->addItem(QStringLiteral("全部模块"), QString());
        if (indexRepository_ != nullptr) {
            const QStringList modules = uniqueSortedCaseInsensitive(indexRepository_->modules());
            for (const QString& module : modules) {
                moduleFilterCombo_->addItem(moduleDisplayName(module), module);
            }
        }

        const int restoreIndex = findComboDataIndex(moduleFilterCombo_, currentModule);
        moduleFilterCombo_->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 0);
    }
}

void SearchPage::resetToEmptyState()
{
    lastSearchQuery_.clear();
    updateStatusLine(QStringLiteral("输入关键词或点击常用词开始搜索。"),
                     isDevMode() ? QStringLiteral("idle=true | suggest=ready | filters=module+sort")
                                 : QStringLiteral("支持模块筛选与排序，结果会自动联动详情。"));
    updateResultEmptyState(QStringLiteral("输入关键词或点击常用词开始搜索。"),
                           QStringLiteral("常用词示例：不等式、导数、椭圆、数列求和、柯西、均值。"));
    resetDetailTimingSessions(true);
    setDetailEmptyState(QStringLiteral("请先在左侧搜索并选择一个结论。\n"
                                       "选中后这里会显示结论详情、公式说明和高清 PDF。"));

    if (!webDetailEnabled_) {
        const QString reason = detailHtmlRenderer_ == nullptr ? QStringLiteral("detail renderer is null")
                                                              : detailHtmlRenderer_->lastError().trimmed();
        updateStatusLine(QStringLiteral("详情页已切换兼容模式。"),
                         isDevMode() ? QStringLiteral("fallback=text | check app_resources/detail + katex")
                                     : QStringLiteral("请检查 app_resources/detail 与 app_resources/katex。"));
        updateDetailShellMeta(QStringLiteral("兼容模式：Web 资源不可用"), QStringLiteral("warning"));
        LOG_WARN(LogCategory::WebViewKatex,
                 QStringLiteral("detail empty_state fallback reason=%1")
                     .arg(reason.isEmpty() ? QStringLiteral("unknown") : reason));
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
        updateStatusLine(QStringLiteral("索引未就绪，无法生成建议。"),
                         isDevMode() ? QStringLiteral("suggest=disabled | reason=backend_unavailable")
                                     : QStringLiteral("请检查离线索引是否加载完成。"));
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
    if (!moduleFilter.isEmpty()) {
        options.moduleFilter.push_back(moduleFilter);
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
    if (QScrollBar* scrollBar = suggestionList_->verticalScrollBar(); scrollBar != nullptr) {
        scrollBar->setValue(scrollBar->minimum());
    }
    if (suggestionList_->count() > 0) {
        suggestionList_->scrollToItem(suggestionList_->item(0), QAbstractItemView::PositionAtTop);
    }

    if (isDevMode()) {
        updateStatusLine(QStringLiteral("建议已更新。"),
                         QStringLiteral("query=%1 | suggest=%2 | elapsed=%3ms")
                             .arg(normalizedQuery)
                             .arg(result.items.size())
                             .arg(elapsedMs));
    } else {
        updateStatusLine(QStringLiteral("建议已更新。"),
                         QStringLiteral("可继续输入，或点击建议直接搜索。"));
    }

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
        lastSearchQuery_.clear();
        currentHits_.clear();
        renderResults(currentHits_);
        clearSuggestions();
        resetToEmptyState();
        return;
    }

    if (!indexReady_ || searchService_ == nullptr) {
        updateStatusLine(QStringLiteral("索引未就绪，无法执行搜索。"),
                         isDevMode() ? QStringLiteral("search=blocked | reason=backend_unavailable")
                                     : QStringLiteral("请检查离线索引是否加载完成。"));
        showDetailError(QStringLiteral("索引未就绪，当前无法展示结果详情。"));
        LOG_ERROR(LogCategory::SearchEngine,
                  QStringLiteral("search failed reason=backend_unavailable query=%1").arg(normalizedQuery));
        return;
    }

    if (!isFeatureEnabled(license::Feature::BasicSearchPreview)
        && !isFeatureEnabled(license::Feature::FullSearch)) {
        updateStatusLine(QStringLiteral("当前授权不支持搜索。"),
                         isDevMode() ? QStringLiteral("search=blocked | reason=license_gate")
                                     : QStringLiteral("请先激活正式版。"));
        updateResultEmptyState(QStringLiteral("搜索未开放"), QStringLiteral("请先在激活页完成授权。"));
        setDetailEmptyState(QStringLiteral("当前授权不支持详情查看。"));
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
    if (advancedFilterEnabled && !moduleFilter.isEmpty()) {
        options.moduleFilter.push_back(moduleFilter);
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
    lastSearchQuery_ = normalizedQuery;
    renderResults(currentHits_);
    clearSuggestions();
    lastSearchSignature_ = signature;
    const int displayedCount = resultList_ == nullptr ? currentHits_.size() : resultList_->count();

    if (currentHits_.isEmpty()) {
        updateResultSummary(normalizedQuery, 0, 0, elapsedMs, false);
        updateResultEmptyState(QStringLiteral("没有找到相关结论"),
                               QStringLiteral("可以尝试搜索：不等式、导数、椭圆、数列求和、柯西、均值。"));
        setDetailEmptyState(QStringLiteral("请先在左侧搜索并选择一个结论。\n"
                                           "选中后这里会显示结论详情、公式说明和高清 PDF。"));

        LOG_INFO(LogCategory::PerfSearch,
                 QStringLiteral("event=search_done query=%1 total=0 elapsed_ms=%2 trigger=%3")
                     .arg(normalizedQuery)
                     .arg(elapsedMs)
                     .arg(triggerSource));
        return;
    }

    const int totalCount = std::max(displayedCount, std::max(rawHitCount, result.total));
    updateResultSummary(normalizedQuery, displayedCount, totalCount, elapsedMs, true);

    if (!fullSearchEnabled) {
        const QString reason = featureDisabledReason(license::Feature::FullSearch);
        if (summaryLabel_ != nullptr) {
            const QString hint = reason.isEmpty() ? QStringLiteral("体验版最多显示前 %1 条结果。").arg(kTrialPreviewLimit)
                                                  : reason;
            summaryLabel_->setText(QStringLiteral("%1\n提示：%2").arg(summaryLabel_->text(), hint));
        }
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

void SearchPage::updateResultSummary(const QString& query,
                                     int displayedCount,
                                     int totalCount,
                                     qint64 elapsedMs,
                                     bool hasResults)
{
    const QString normalizedQuery = query.trimmed();
    const QString moduleFilter = selectedModuleFilter();
    const QString moduleText = moduleFilter.isEmpty() ? QStringLiteral("全部模块") : moduleDisplayName(moduleFilter);
    const QString sortText =
        sortCombo_ == nullptr ? QStringLiteral("按相关度") : sortCombo_->currentText().trimmed();
    const int safeDisplayed = std::max(0, displayedCount);
    const int safeTotal = std::max(safeDisplayed, std::max(0, totalCount));

    if (isDevMode()) {
        updateStatusLine(hasResults ? QStringLiteral("搜索完成。") : QStringLiteral("没有找到相关结论。"),
                         QStringLiteral("query=%1 | shown=%2 | total=%3 | elapsed=%4ms | module=%5 | sort=%6")
                             .arg(normalizedQuery.isEmpty() ? QStringLiteral("<empty>") : normalizedQuery)
                             .arg(safeDisplayed)
                             .arg(safeTotal)
                             .arg(elapsedMs)
                             .arg(moduleText)
                             .arg(sortText.isEmpty() ? QStringLiteral("按相关度") : sortText));
        return;
    }

    if (normalizedQuery.isEmpty()) {
        updateStatusLine(QStringLiteral("输入关键词或点击常用词开始搜索。"),
                         QStringLiteral("支持模块筛选与排序，结果会自动联动详情。"));
        return;
    }

    if (!hasResults || safeTotal <= 0) {
        updateStatusLine(QStringLiteral("没有找到相关结论"),
                         QStringLiteral("可以尝试搜索：不等式、导数、椭圆、数列求和、柯西、均值。"));
        return;
    }

    updateStatusLine(QStringLiteral("已显示 %1 / 共 %2 条相关结论").arg(safeDisplayed).arg(safeTotal),
                     QStringLiteral("关键词：%1\n筛选：%2\n排序：%3")
                         .arg(normalizedQuery)
                         .arg(moduleText)
                         .arg(sortText.isEmpty() ? QStringLiteral("按相关度") : sortText));
}

QString SearchPage::highlightKeyword(const QString& text, const QStringList& terms) const
{
    if (text.isEmpty()) {
        return QString();
    }

    if (terms.isEmpty()) {
        return text.toHtmlEscaped();
    }

    QVector<QPair<int, int>> ranges;
    ranges.reserve(16);
    for (const QString& term : terms) {
        const QString needle = term.trimmed();
        if (needle.isEmpty()) {
            continue;
        }
        int from = 0;
        while (from < text.size()) {
            const int index = text.indexOf(needle, from, Qt::CaseInsensitive);
            if (index < 0) {
                break;
            }
            ranges.push_back({index, index + needle.size()});
            from = index + needle.size();
        }
    }

    if (ranges.isEmpty()) {
        return text.toHtmlEscaped();
    }

    std::sort(ranges.begin(), ranges.end(), [](const QPair<int, int>& lhs, const QPair<int, int>& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second > rhs.second;
    });

    QVector<QPair<int, int>> merged;
    merged.reserve(ranges.size());
    for (const QPair<int, int>& range : ranges) {
        if (merged.isEmpty() || range.first > merged.back().second) {
            merged.push_back(range);
        } else {
            merged.back().second = std::max(merged.back().second, range.second);
        }
    }

    QString highlighted;
    highlighted.reserve(text.size() + merged.size() * 48);
    int cursor = 0;
    for (const QPair<int, int>& range : merged) {
        if (range.first > cursor) {
            highlighted.append(text.mid(cursor, range.first - cursor).toHtmlEscaped());
        }
        const QString matched = text.mid(range.first, range.second - range.first).toHtmlEscaped();
        highlighted.append(
            QStringLiteral("<span style=\"background:#fff2a8;color:#1d3557;font-weight:600;\">%1</span>").arg(matched));
        cursor = range.second;
    }
    if (cursor < text.size()) {
        highlighted.append(text.mid(cursor).toHtmlEscaped());
    }
    return highlighted;
}

QWidget* SearchPage::buildResultCard(const domain::models::SearchHit& hit,
                                     const QStringList& highlightTerms,
                                     int displayIndex,
                                     int totalCount,
                                     QWidget* parent) const
{
    domain::adapters::ConclusionCardViewData cardView;
    const domain::adapters::ConclusionCardViewData* cardViewPtr = nullptr;

    if (contentReady_ && contentRepository_ != nullptr) {
        if (const auto* record = contentRepository_->getById(hit.docId); record != nullptr) {
            cardView = domain::adapters::ConclusionCardAdapter::toViewData(*record);
            cardViewPtr = &cardView;
        }
    }

    const QString cardId = hit.docId.trimmed().isEmpty() ? QStringLiteral("—") : hit.docId.trimmed();
    const QString titleText = !cardView.title.trimmed().isEmpty() ? cardView.title.trimmed()
                              : !hit.title.trimmed().isEmpty()    ? hit.title.trimmed()
                                                                   : cardId;

    QString summaryText = !cardView.summaryPlain.trimmed().isEmpty() ? cardView.summaryPlain.trimmed()
                         : !hit.summary.trimmed().isEmpty()          ? hit.summary.trimmed()
                                                                      : QStringLiteral("点击右侧查看完整详情。");
    if (summaryText.isEmpty()) {
        summaryText = QStringLiteral("点击右侧查看完整详情。");
    }

    const QString moduleText =
        !hit.module.trimmed().isEmpty() ? moduleDisplayName(hit.module) : QStringLiteral("未标注模块");
    const QString categoryText = !hit.category.trimmed().isEmpty() ? hit.category.trimmed() : QStringLiteral("未标注分类");
    const QString difficultyText = QStringLiteral("难度 %1").arg(QString::number(hit.difficulty, 'f', 1));

    const QStringList effectiveTags = cardViewPtr == nullptr || cardViewPtr->tags.isEmpty() ? hit.tags : cardViewPtr->tags;
    const QString tagsText = limitTagText(effectiveTags, 3, QStringLiteral(" · "));
    const QString usageText = limitTagText(buildUsageTerms(hit, cardViewPtr), 3, QStringLiteral(" · "));

    auto* card = new QWidget(parent);
    card->setObjectName(QStringLiteral("searchResultCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(5);

    auto* titleLabel = new QLabel(card);
    titleLabel->setObjectName(QStringLiteral("searchResultCardTitle"));
    titleLabel->setWordWrap(true);
    titleLabel->setTextFormat(Qt::RichText);
    const int safeIndex = std::max(1, displayIndex);
    const int safeTotal = std::max(safeIndex, totalCount);
    const QString rankSuffix = safeTotal > 0
                                   ? QStringLiteral(" <span style=\"color:#6B7A90;font-weight:500;\">(%1/%2)</span>")
                                         .arg(safeIndex)
                                         .arg(safeTotal)
                                   : QString();
    const QString styledIdToken =
        QStringLiteral("<span style=\"display:inline-block;background:#e8f1ff;color:#1f4f86;"
                       "border-radius:6px;padding:1px 7px;font-size:12px;font-weight:620;\">%1</span>")
            .arg(cardId.toHtmlEscaped());
    titleLabel->setText(QStringLiteral("%1&nbsp;&nbsp;%2%3").arg(styledIdToken, highlightKeyword(titleText, highlightTerms), rankSuffix));

    auto* metaLabel = new QLabel(card);
    metaLabel->setObjectName(QStringLiteral("searchResultCardMeta"));
    metaLabel->setWordWrap(true);
    metaLabel->setText(QStringLiteral("%1 · %2 · %3").arg(moduleText, categoryText, difficultyText));

    auto* tagsLabel = new QLabel(card);
    tagsLabel->setObjectName(QStringLiteral("searchResultCardTags"));
    tagsLabel->setWordWrap(true);
    tagsLabel->setTextFormat(Qt::RichText);
    tagsLabel->setText(QStringLiteral("标签：%1").arg(highlightKeyword(tagsText, highlightTerms)));

    auto* usageLabel = new QLabel(card);
    usageLabel->setObjectName(QStringLiteral("searchResultCardUsage"));
    usageLabel->setWordWrap(true);
    usageLabel->setTextFormat(Qt::RichText);
    usageLabel->setText(QStringLiteral("适用：%1").arg(highlightKeyword(usageText, highlightTerms)));

    cardLayout->addWidget(titleLabel);
    cardLayout->addWidget(metaLabel);
    cardLayout->addWidget(tagsLabel);
    cardLayout->addWidget(usageLabel);

    const QString tooltip = joinNonEmpty(
        {titleText,
         summaryText,
         QStringLiteral("%1 · %2 · %3").arg(moduleText, categoryText, difficultyText)},
        QStringLiteral("\n"));
    if (!tooltip.isEmpty()) {
        card->setToolTip(tooltip);
    }

    return card;
}

int SearchPage::resultCardPreferredWidth() const
{
    if (resultList_ == nullptr || resultList_->viewport() == nullptr) {
        return 360;
    }

    const int viewportWidth = resultList_->viewport()->contentsRect().width();
    const int cardOuterPadding = 24;
    int preferredWidth = viewportWidth - cardOuterPadding;

    QScrollBar* verticalBar = resultList_->verticalScrollBar();
    if (verticalBar != nullptr && !verticalBar->isVisible()) {
        preferredWidth -= style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, resultList_);
    }

    return std::max(320, preferredWidth);
}

void SearchPage::relayoutResultCardsToViewport()
{
    if (resultList_ == nullptr || resultList_->count() <= 0) {
        return;
    }

    const int preferredWidth = resultCardPreferredWidth();
    for (int i = 0; i < resultList_->count(); ++i) {
        QListWidgetItem* item = resultList_->item(i);
        if (item == nullptr) {
            continue;
        }

        QWidget* cardWidget = resultList_->itemWidget(item);
        if (cardWidget == nullptr) {
            continue;
        }

        cardWidget->setMinimumWidth(preferredWidth);
        cardWidget->setMaximumWidth(preferredWidth);
        cardWidget->adjustSize();
        item->setSizeHint(QSize(preferredWidth, std::max(96, cardWidget->sizeHint().height() + 10)));
    }
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

    const QStringList highlightTerms = extractHighlightTerms(lastSearchQuery_);
    const int totalCount = hits.size();
    for (int i = 0; i < hits.size(); ++i) {
        const domain::models::SearchHit& hit = hits.at(i);
        auto* item = new QListWidgetItem();
        item->setData(kResultItemDocIdRole, hit.docId);
        QWidget* cardWidget = buildResultCard(hit, highlightTerms, i + 1, totalCount, resultList_);
        const int preferredWidth = std::max(360, resultList_->viewport()->width() - 24);
        cardWidget->setMinimumWidth(preferredWidth);
        cardWidget->adjustSize();
        item->setSizeHint(QSize(preferredWidth, std::max(96, cardWidget->sizeHint().height() + 10)));
        item->setToolTip(cardWidget->toolTip());
        resultList_->addItem(item);
        resultList_->setItemWidget(item, cardWidget);
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
        resetDetailViewportToTop();
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

    const bool fullDetailEnabled = isFeatureEnabled(license::Feature::FullDetail);
    const ui::detail::DetailRenderPath renderPath = ui::detail::DetailRenderPathResolver::resolveForMode(
        fullDetailEnabled,
        detailRenderMode_,
        pdfDetailEnabled_,
        webDetailEnabled_,
        detailPane_ != nullptr,
        detailViewDataMapper_ != nullptr);

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

    const auto dispatchViaWeb = [this, &detailView, &contentPayload, &normalizedDocId, requestId, selectionTimestampMs]() {
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
    };

    if (renderPath == ui::detail::DetailRenderPath::Pdf) {
        QString failureReason;
        if (renderDetailInPdfView(normalizedDocId, detailView, &failureReason)) {
            if (detailRenderCoordinator_ != nullptr) {
                detailRenderCoordinator_->markRendered(normalizedDocId, requestId);
            }
            logDetailPerf(normalizedDocId,
                          requestId,
                          selectionTimestampMs,
                          QStringLiteral("total"),
                          QStringLiteral("dt=%1ms mode=pdf").arg(detailElapsedMs(selectionTimestampMs)));
            markDetailTimingSuccess(normalizedDocId, requestId, selectionTimestampMs);
            return;
        }

        logDetailPerf(normalizedDocId,
                      requestId,
                      selectionTimestampMs,
                      QStringLiteral("pdf_unavailable"),
                      QStringLiteral("reason=%1").arg(failureReason.trimmed().isEmpty() ? QStringLiteral("unknown")
                                                                                         : failureReason.trimmed()));

        if (detailRenderMode_ == ui::detail::DetailRenderMode::Auto
            && ui::detail::DetailRenderPathResolver::resolve(
                   true, webDetailEnabled_, detailPane_ != nullptr, detailViewDataMapper_ != nullptr)
                   == ui::detail::DetailRenderPath::Web) {
            dispatchViaWeb();
            return;
        }
    }

    if (renderPath == ui::detail::DetailRenderPath::Web) {
        dispatchViaWeb();
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
    resetFallbackDetailViewportToTop();
    detailBrowser_->setVisible(true);
    if (detailPdfView_ != nullptr) {
        detailPdfView_->setVisible(false);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
    currentDetailPdfPath_.clear();
    updatePdfPageNavigationUi();
}

bool SearchPage::renderDetailInPdfView(const QString& docId,
                                       const domain::adapters::ConclusionDetailViewData& detailView,
                                       QString* failureReason)
{
    currentDetailPdfPath_.clear();
    const auto assignFailure = [failureReason](const QString& reason) {
        if (failureReason != nullptr) {
            *failureReason = reason;
        }
    };

    if (!pdfDetailEnabled_ || detailPdfView_ == nullptr || detailPdfDocument_ == nullptr) {
        assignFailure(QStringLiteral("pdf_view_not_ready"));
        return false;
    }

    const QString pdfPath = resolveDetailPdfPath(docId, detailView);
    if (pdfPath.trimmed().isEmpty()) {
        assignFailure(QStringLiteral("pdf_path_not_resolved"));
        return false;
    }

    const QFileInfo pdfInfo(pdfPath);
    if (!pdfInfo.exists() || !pdfInfo.isFile()) {
        assignFailure(QStringLiteral("pdf_file_missing"));
        return false;
    }

    const QPdfDocument::Error loadError = detailPdfDocument_->load(pdfInfo.absoluteFilePath());
    if (loadError != QPdfDocument::Error::None) {
        assignFailure(QStringLiteral("pdf_load_error_%1").arg(static_cast<int>(loadError)));
        return false;
    }

    detailPdfView_->setVisible(true);
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
    if (detailBrowser_ != nullptr) {
        detailBrowser_->setVisible(false);
    }

    currentDetailPdfPath_ = pdfInfo.absoluteFilePath();
    jumpToPdfPage(0);
    applyPdfFitToWidth(true);
    resetPdfDetailViewportToTop();
    updatePdfPageNavigationUi();
    updateDetailShellMeta(QStringLiteral("PDF 详情预览"), QStringLiteral("neutral"));
    return true;
}

QString SearchPage::resolveDetailPdfPath(const QString& docId,
                                         const domain::adapters::ConclusionDetailViewData& detailView) const
{
    const QString normalizedDocId = docId.trimmed();
    if (normalizedDocId.isEmpty()) {
        return {};
    }

    const QString mappedFile = conclusionPdfMapLoader_.mappedPdfFileName(normalizedDocId).trimmed();
    if (!mappedFile.isEmpty()) {
        const QFileInfo mapInfo(mappedFile);
        if (mapInfo.isAbsolute()) {
            return mapInfo.absoluteFilePath();
        }
        return QDir(detailPdfDirectory_).filePath(mappedFile);
    }

    const QString assetPdf = detailView.assetPdfName.trimmed();
    if (!assetPdf.isEmpty()) {
        const QFileInfo assetInfo(assetPdf);
        if (assetInfo.isAbsolute()) {
            return assetInfo.absoluteFilePath();
        }
        return QDir(detailPdfDirectory_).filePath(assetPdf);
    }

    return QDir(detailPdfDirectory_).filePath(QStringLiteral("%1.pdf").arg(normalizedDocId));
}

void SearchPage::jumpToPdfPage(int pageIndex)
{
    if (detailPdfDocument_ == nullptr || detailPdfView_ == nullptr || detailPdfView_->pageNavigator() == nullptr) {
        return;
    }

    const int pageCount = detailPdfDocument_->pageCount();
    if (pageCount <= 0) {
        return;
    }

    const int clampedPage = std::clamp(pageIndex, 0, pageCount - 1);
    detailPdfView_->pageNavigator()->jump(clampedPage, QPointF(), detailPdfView_->zoomFactor());
    updatePdfPageNavigationUi();
}

void SearchPage::applyPdfFitToWidth(bool silentStatus)
{
    const bool viewReady =
        (detailPdfDocument_ != nullptr && detailPdfView_ != nullptr && detailPdfView_->pageNavigator() != nullptr);
    const bool hasPdf = viewReady && detailPdfView_->isVisible() && detailPdfDocument_->pageCount() > 0;
    if (!hasPdf) {
        if (!silentStatus) {
            updateStatusLine(QStringLiteral("当前 PDF 暂不可用。"), QStringLiteral("请先选择可预览 PDF 的结论。"));
        }
        return;
    }

    if (detailPdfView_->viewport() != nullptr && detailPdfView_->viewport()->width() <= 0) {
        QTimer::singleShot(0, this, [this]() { applyPdfFitToWidth(true); });
    }

    const qreal fitZoom = computePdfFitWidthZoomFactor(detailPdfDocument_, detailPdfView_);
    detailPdfFitWidthBaseZoom_ =
        std::clamp(fitZoom > 0.0 ? fitZoom : 1.0, kDetailPdfZoomMinFactor, kDetailPdfZoomMaxFactor);
    detailPdfView_->setZoomMode(QPdfView::ZoomMode::Custom);
    detailPdfView_->setZoomFactor(detailPdfFitWidthBaseZoom_);
    if (!silentStatus) {
        updateStatusLine(QStringLiteral("PDF 已切换为适合宽度。"),
                         isDevMode() ? QStringLiteral("zoom_mode=custom_from_fit_width")
                                     : QStringLiteral("预览宽度已匹配详情区域。"));
    }
}

void SearchPage::updateDetailToolbarState()
{
    const bool hasSelection = !currentDetailDocId_.trimmed().isEmpty();

    if (detailFontButton_ != nullptr) {
        detailFontButton_->setEnabled(hasSelection);
    }
    if (detailFullscreenButton_ != nullptr) {
        detailFullscreenButton_->setEnabled(hasSelection);
    }
    if (!hasSelection) {
        if (detailPdfPrevButton_ != nullptr) {
            detailPdfPrevButton_->setEnabled(false);
        }
        if (detailPdfNextButton_ != nullptr) {
            detailPdfNextButton_->setEnabled(false);
        }
        if (detailPdfFitWidthButton_ != nullptr) {
            detailPdfFitWidthButton_->setEnabled(false);
        }
        if (detailPdfExportButton_ != nullptr) {
            detailPdfExportButton_->setEnabled(false);
        }
    }

    if (detailFullscreenShortcut_ != nullptr) {
        detailFullscreenShortcut_->setEnabled(hasSelection);
    }
    if (detailExitFullscreenShortcut_ != nullptr) {
        detailExitFullscreenShortcut_->setEnabled(hasSelection || detailPaneFullscreen_);
    }

    if (detailTimingLabel_ != nullptr) {
        detailTimingLabel_->setVisible(isDevMode());
    }

    refreshFavoriteButtonState(hasSelection ? currentDetailDocId_ : QString());
}

void SearchPage::setDetailEmptyState(const QString& message)
{
    showDetailPlaceholder(message);
    updateDetailToolbarState();
}

void SearchPage::setDetailReadyState()
{
    updateDetailShellMeta(QStringLiteral("详情已就绪"), QStringLiteral("success"));
    updateDetailToolbarState();
}

void SearchPage::updatePdfPageNavigationUi()
{
    const bool viewReady =
        (detailPdfDocument_ != nullptr && detailPdfView_ != nullptr && detailPdfView_->pageNavigator() != nullptr);
    const bool pdfVisible = viewReady && detailPdfView_->isVisible();
    const bool hasSelection = !currentDetailDocId_.trimmed().isEmpty();
    const int pageCount = viewReady ? detailPdfDocument_->pageCount() : 0;
    const QString exportSourcePath = currentDetailPdfPath_.trimmed();
    const bool canExport = pdfVisible && !exportSourcePath.isEmpty() && QFileInfo::exists(exportSourcePath);

    int currentPage = -1;
    if (pageCount > 0) {
        currentPage = std::clamp(detailPdfView_->pageNavigator()->currentPage(), 0, pageCount - 1);
    }

    if (detailPdfPageLabel_ != nullptr) {
        if (!hasSelection) {
            detailPdfPageLabel_->setText(QStringLiteral("PDF --/--"));
        } else if (!pdfVisible || pageCount <= 0) {
            detailPdfPageLabel_->setText(QStringLiteral("PDF 暂不可用"));
        } else {
            detailPdfPageLabel_->setText(detailPageIndicatorText(currentPage, pageCount));
        }
    }

    const bool canGoPrev = (hasSelection && pdfVisible && pageCount > 0 && currentPage > 0);
    const bool canGoNext = (hasSelection && pdfVisible && pageCount > 0 && currentPage < (pageCount - 1));
    const bool canFitWidth = (hasSelection && pdfVisible && pageCount > 0);
    const bool canUseExport = (hasSelection && canExport);
    if (detailPdfPrevButton_ != nullptr) {
        detailPdfPrevButton_->setEnabled(canGoPrev);
    }
    if (detailPdfNextButton_ != nullptr) {
        detailPdfNextButton_->setEnabled(canGoNext);
    }
    if (detailPdfFitWidthButton_ != nullptr) {
        detailPdfFitWidthButton_->setEnabled(canFitWidth);
        detailPdfFitWidthButton_->setToolTip(canFitWidth ? QStringLiteral("将 PDF 调整为适合当前详情宽度")
                                                         : QStringLiteral("请先加载可用的 PDF 详情"));
    }
    if (detailPdfExportButton_ != nullptr) {
        detailPdfExportButton_->setEnabled(canUseExport);
        detailPdfExportButton_->setToolTip(canUseExport ? QStringLiteral("将当前 PDF 另存为文件")
                                                     : QStringLiteral("请先加载可用的 PDF 详情"));
    }
    updateDetailToolbarState();
}

#if defined(MATH_SEARCH_TESTS_SOURCE_DIR)
QString SearchPage::resolveDetailPdfPathForTest(const QString& docId,
                                                const domain::adapters::ConclusionDetailViewData& detailView) const
{
    return resolveDetailPdfPath(docId, detailView);
}
#endif

void SearchPage::showDetailPlaceholder(const QString& message)
{
    const QString fallbackMessage = message.trimmed().isEmpty()
                                        ? QStringLiteral("请先在左侧搜索并选择一个结论。")
                                        : message.trimmed();
    resetDetailTimingSessions(true);
    updateDetailShellMeta(detailMetaTextForPlaceholder(fallbackMessage), QStringLiteral("neutral"));
    if (detailRenderCoordinator_ != nullptr) {
        detailRenderCoordinator_->clearRenderedDetail();
    }
    currentDetailDocId_.clear();
    currentDetailPdfPath_.clear();
    updateDetailToolbarState();

    if (shouldDispatchStateToWeb()) {
        const QJsonObject payload = detailViewDataMapper_->buildEmptyPayload(fallbackMessage);
        dispatchPayloadToWeb(payload);
        updatePdfPageNavigationUi();
        return;
    }

    if (detailBrowser_ == nullptr) {
        return;
    }

    const QString htmlMessage = fallbackMessage.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
    detailBrowser_->setHtml(QStringLiteral("<p style=\"color:#666;line-height:1.7;\">%1</p>").arg(htmlMessage));
    resetFallbackDetailViewportToTop();
    detailBrowser_->setVisible(true);
    if (detailPdfView_ != nullptr) {
        detailPdfView_->setVisible(false);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
    updatePdfPageNavigationUi();
    updateDetailToolbarState();
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
    currentDetailPdfPath_.clear();
    updateDetailToolbarState();

    if (shouldDispatchStateToWeb()) {
        const QJsonObject payload = detailViewDataMapper_->buildErrorPayload(fallbackMessage);
        dispatchPayloadToWeb(payload);
        updatePdfPageNavigationUi();
        return;
    }

    if (detailBrowser_ == nullptr) {
        return;
    }

    const QString htmlMessage = fallbackMessage.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
    detailBrowser_->setHtml(QStringLiteral("<p style=\"color:#9a3412;line-height:1.7;\">%1</p>").arg(htmlMessage));
    resetFallbackDetailViewportToTop();
    detailBrowser_->setVisible(true);
    if (detailPdfView_ != nullptr) {
        detailPdfView_->setVisible(false);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
    updatePdfPageNavigationUi();
    updateDetailToolbarState();
}

void SearchPage::resetWebDetailViewportToTop()
{
    if (!webDetailEnabled_ || detailPane_ == nullptr) {
        return;
    }
    detailPane_->resetViewportToTop();
}

void SearchPage::resetPdfDetailViewportToTop()
{
    if (detailPdfView_ == nullptr) {
        return;
    }

    QScrollBar* scrollBar = detailPdfView_->verticalScrollBar();
    if (scrollBar != nullptr) {
        scrollBar->setValue(scrollBar->minimum());
    }
}

void SearchPage::resetFallbackDetailViewportToTop()
{
    if (detailBrowser_ == nullptr) {
        return;
    }

    QScrollBar* scrollBar = detailBrowser_->verticalScrollBar();
    if (scrollBar != nullptr) {
        scrollBar->setValue(scrollBar->minimum());
    }

    QTimer::singleShot(0, detailBrowser_, [browser = detailBrowser_]() {
        if (browser == nullptr) {
            return;
        }
        QScrollBar* delayedScrollBar = browser->verticalScrollBar();
        if (delayedScrollBar != nullptr) {
            delayedScrollBar->setValue(delayedScrollBar->minimum());
        }
    });
}

void SearchPage::resetDetailViewportToTop()
{
    resetPdfDetailViewportToTop();
    resetWebDetailViewportToTop();
    resetFallbackDetailViewportToTop();
}

bool SearchPage::shouldDispatchStateToWeb() const
{
    return detailRenderMode_ == ui::detail::DetailRenderMode::Web && webDetailEnabled_ && detailViewDataMapper_ != nullptr;
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
    if (detailPdfView_ != nullptr) {
        detailPdfView_->setVisible(false);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(true);
    }
    currentDetailPdfPath_.clear();
    updatePdfPageNavigationUi();
    ui::detail::DetailPane::RequestContext requestContext;
    requestContext.payload = payload;
    requestContext.detailId = docId.trimmed().isEmpty() ? payload.value(QStringLiteral("detailId")).toString().trimmed()
                                                        : docId.trimmed();
    requestContext.requestId = requestId;
    requestContext.selectionTimestampMs = selectionTimestampMs;

    const QString normalizedDetailId =
        requestContext.detailId.trimmed().isEmpty() ? QStringLiteral("-") : requestContext.detailId.trimmed();
    LOG_DEBUG(LogCategory::PerfDetail,
              QStringLiteral("event=detail_viewport_reset request_id=%1 detail_id=%2 stage=before_dispatch")
                  .arg(requestContext.requestId)
                  .arg(normalizedDetailId));
    resetWebDetailViewportToTop();
    LOG_DEBUG(LogCategory::PerfDetail,
              QStringLiteral("event=detail_dispatch request_id=%1 detail_id=%2 stage=start")
                  .arg(requestContext.requestId)
                  .arg(normalizedDetailId));
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
    updateDetailToolbarState();

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
    setDetailReadyState();

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
    if (detailPdfView_ != nullptr) {
        detailPdfView_->setVisible(false);
    }
    if (detailBrowser_ != nullptr) {
        detailBrowser_->setVisible(true);
    }

    const QString userMessage = webFallbackUserMessage(reason);
    updateStatusLine(QStringLiteral("详情 Web 渲染异常，已自动切换兼容模式。"), userMessage);
    showDetailError(userMessage);
}

bool SearchPage::isDevMode() const
{
    return isDevMode_;
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
    if (!advancedFilterEnabled && moduleFilterCombo_ != nullptr) {
        QSignalBlocker moduleBlocker(moduleFilterCombo_);
        moduleFilterCombo_->setCurrentIndex(0);
        lastSuggestSignature_.clear();
        lastSearchSignature_.clear();
    }

    if (moduleFilterCombo_ != nullptr) {
        moduleFilterCombo_->setEnabled(advancedFilterEnabled);
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
    updateDetailToolbarState();
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
        resetFallbackDetailViewportToTop();
        detailBrowser_->setVisible(true);
    }
    if (detailPdfView_ != nullptr) {
        detailPdfView_->setVisible(false);
    }
    if (detailWebView_ != nullptr) {
        detailWebView_->setVisible(false);
    }
    currentDetailPdfPath_.clear();
    updatePdfPageNavigationUi();
    updateDetailToolbarState();
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

QString SearchPage::filtersSignature() const
{
    return QStringLiteral("module=%1").arg(selectedModuleFilter());
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
