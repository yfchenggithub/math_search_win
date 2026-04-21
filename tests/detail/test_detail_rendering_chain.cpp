#include "core/logging/logger.h"
#include "domain/adapters/conclusion_detail_adapter.h"
#include "ui/detail/detail_fallback_content_builder.h"
#include "ui/detail/detail_html_renderer.h"
#include "ui/detail/detail_perf_aggregator.h"
#include "ui/detail/detail_render_coordinator.h"
#include "ui/detail/detail_render_path_resolver.h"
#include "ui/detail/detail_view_data_mapper.h"

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>

namespace {

class ScopedCurrentDir final {
public:
    explicit ScopedCurrentDir(const QString& newCurrentPath)
        : previousCurrentPath_(QDir::currentPath())
    {
        QDir::setCurrent(newCurrentPath);
    }

    ~ScopedCurrentDir()
    {
        if (!previousCurrentPath_.trimmed().isEmpty()) {
            QDir::setCurrent(previousCurrentPath_);
        }
    }

private:
    QString previousCurrentPath_;
};

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
    {
        const QString baseDir = QDir::current().filePath(QStringLiteral(".tmp_detail_render_chain_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("sandbox_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
        }
    }

    ~ScopedSandboxRoot()
    {
        if (!rootPath_.isEmpty()) {
            QDir(rootPath_).removeRecursively();
        }
    }

    bool isValid() const
    {
        return !rootPath_.isEmpty() && QDir(rootPath_).exists();
    }

    QString path() const
    {
        return rootPath_;
    }

private:
    QString rootPath_;
};

bool writeUtf8File(const QString& path, const QString& content)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    const qint64 bytes = file.write(content.toUtf8());
    file.close();
    return bytes >= 0;
}

bool setupRendererSandbox(const QString& rootPath,
                          bool includeDetailTemplate,
                          bool includeDetailCss,
                          bool includeDetailJs,
                          bool includeKatexCss,
                          bool includeKatexJs,
                          bool includeKatexAutoRenderJs)
{
    QDir root(rootPath);
    if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("resources/detail"))
        || !root.mkpath(QStringLiteral("resources/katex/contrib"))) {
        return false;
    }

    const QString detailTemplatePath = root.filePath(QStringLiteral("resources/detail/detail_template.html"));
    const QString detailCssPath = root.filePath(QStringLiteral("resources/detail/detail.css"));
    const QString detailJsPath = root.filePath(QStringLiteral("resources/detail/detail.js"));
    const QString katexCssPath = root.filePath(QStringLiteral("resources/katex/katex.min.css"));
    const QString katexJsPath = root.filePath(QStringLiteral("resources/katex/katex.min.js"));
    const QString katexAutoRenderPath = root.filePath(QStringLiteral("resources/katex/contrib/auto-render.min.js"));

    if (includeDetailTemplate
        && !writeUtf8File(detailTemplatePath,
                          QStringLiteral("<html><body><script>window.__DETAIL_INITIAL_DATA__ = {};</script></body></html>"))) {
        return false;
    }
    if (includeDetailCss && !writeUtf8File(detailCssPath, QStringLiteral("body{font-family:sans-serif;}"))) {
        return false;
    }
    if (includeDetailJs && !writeUtf8File(detailJsPath, QStringLiteral("window.DetailRuntime={};"))) {
        return false;
    }
    if (includeKatexCss && !writeUtf8File(katexCssPath, QStringLiteral("/* katex css */"))) {
        return false;
    }
    if (includeKatexJs && !writeUtf8File(katexJsPath, QStringLiteral("window.katex={};"))) {
        return false;
    }
    if (includeKatexAutoRenderJs && !writeUtf8File(katexAutoRenderPath, QStringLiteral("window.renderMathInElement=function(){};"))) {
        return false;
    }

    return true;
}

domain::adapters::ConclusionDetailViewData makeBaselineDetailView()
{
    domain::adapters::ConclusionDetailViewData detail;
    detail.conclusionId = QStringLiteral("I042");
    detail.title = QStringLiteral("Quadratic Core Fact");
    detail.module = QStringLiteral("Algebra");
    detail.category = QStringLiteral("Function");
    detail.tags = {QStringLiteral("key"), QStringLiteral(""), QStringLiteral("exam")};
    detail.summary = QStringLiteral("f(x)=x^2+bx+c has axis x=-b/(2a).");
    detail.conditionText = QStringLiteral("a != 0");
    detail.remarkText = QStringLiteral("Keep coefficient sign consistent.");
    detail.isValid = true;

    domain::adapters::DetailVariableViewData variable;
    variable.name = QStringLiteral("a");
    variable.latex = QStringLiteral("a");
    variable.description = QStringLiteral("quadratic coefficient");
    variable.required = true;
    detail.variables.push_back(variable);

    detail.sections = {
        {QStringLiteral("statement"), QStringLiteral("Conclusion"), QStringLiteral("<p>y=x^2</p>"), QString(), QStringLiteral("normal"), true},
        {QStringLiteral("intuition"), QStringLiteral("Intuition"), QStringLiteral("<p>Parabola opens up when a>0.</p>"), QString(), QStringLiteral("normal"), true},
        {QStringLiteral("proof"), QStringLiteral("Proof"), QStringLiteral("<p>Complete the square.</p>"), QString(), QStringLiteral("normal"), true},
        {QStringLiteral("pitfall"), QStringLiteral("Pitfalls"), QString(), QStringLiteral("Do not forget domain assumptions."), QStringLiteral("pitfall"), true},
        {QStringLiteral("usage"), QStringLiteral("Usage"), QString(), QStringLiteral("Use for vertex and range checks."), QStringLiteral("normal"), true},
        {QStringLiteral("summary"), QStringLiteral("Summary"), QStringLiteral("<p>Formula first, then interpretation.</p>"), QString(), QStringLiteral("summary"), true},
    };

    return detail;
}

QJsonObject findSectionById(const QJsonArray& sections, const QString& sectionId)
{
    for (const QJsonValue& sectionValue : sections) {
        const QJsonObject section = sectionValue.toObject();
        if (section.value(QStringLiteral("id")).toString() == sectionId) {
            return section;
        }
    }
    return {};
}

}  // namespace

class DetailRenderingChainTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void coordinator_newRequestSupersedesOldRequest();
    void coordinator_staleRequestIgnored_andRequestOrderMonotonic();

    void mapper_contentPayloadShapeAndSectionMapping();
    void mapper_missingFieldsFallback_andRichHtmlCompatibility();

    void resources_missingDetailFiles_routesToFallback();
    void resources_missingKatexFiles_routesToFallback();
    void resources_complete_routesToWeb();

    void fallbackBuilder_outputsCoreContent_andSectionOrder();
    void fallbackBuilder_trialPreviewSnippet_andTruncation();

    void perfAggregator_keyPhasesRecorded_andLifecycleStable();
};

void DetailRenderingChainTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void DetailRenderingChainTest::coordinator_newRequestSupersedesOldRequest()
{
    ui::detail::DetailRenderCoordinator coordinator;

    const ui::detail::DetailRenderRequestCreation first = coordinator.createRequest(QStringLiteral(" A001 "));
    QCOMPARE(first.request.requestId, static_cast<quint64>(1));
    QCOMPARE(first.request.detailId, QStringLiteral("A001"));
    QCOMPARE(first.supersededRequestId, static_cast<quint64>(0));
    QVERIFY(first.supersededDetailId.isEmpty());

    const ui::detail::DetailRenderRequestCreation second = coordinator.createRequest(QStringLiteral("B002"));
    QCOMPARE(second.request.requestId, static_cast<quint64>(2));
    QCOMPARE(second.request.detailId, QStringLiteral("B002"));
    QCOMPARE(second.supersededRequestId, static_cast<quint64>(1));
    QCOMPARE(second.supersededDetailId, QStringLiteral("A001"));
    QVERIFY(coordinator.isRequestStale(first.request.requestId));
    QVERIFY(!coordinator.isRequestStale(second.request.requestId));
}

void DetailRenderingChainTest::coordinator_staleRequestIgnored_andRequestOrderMonotonic()
{
    ui::detail::DetailRenderCoordinator coordinator;

    const ui::detail::DetailRenderRequestCreation first = coordinator.createRequest(QStringLiteral("R1"));
    const ui::detail::DetailRenderRequestCreation second = coordinator.createRequest(QStringLiteral("R2"));
    const ui::detail::DetailRenderRequestCreation third = coordinator.createRequest(QStringLiteral("R3"));

    QVERIFY(first.request.requestId < second.request.requestId);
    QVERIFY(second.request.requestId < third.request.requestId);
    QVERIFY(coordinator.isRequestStale(first.request.requestId));
    QVERIFY(coordinator.isRequestStale(second.request.requestId));
    QVERIFY(!coordinator.isRequestStale(third.request.requestId));

    coordinator.markRendered(first.request.detailId, first.request.requestId);
    QVERIFY(!coordinator.isSameAsRendered(QStringLiteral("R1")));
    coordinator.markRendered(second.request.detailId, second.request.requestId);
    QVERIFY(!coordinator.isSameAsRendered(QStringLiteral("R2")));
    coordinator.markRendered(third.request.detailId, third.request.requestId);
    QVERIFY(coordinator.isSameAsRendered(QStringLiteral("R3")));
}

void DetailRenderingChainTest::mapper_contentPayloadShapeAndSectionMapping()
{
    const domain::adapters::ConclusionDetailViewData detail = makeBaselineDetailView();

    ui::detail::DetailViewDataMapper mapper;
    const QJsonObject payload = mapper.buildContentPayload(detail, 77);

    QCOMPARE(payload.value(QStringLiteral("state")).toString(), QStringLiteral("content"));
    QCOMPARE(payload.value(QStringLiteral("requestId")).toInteger(), 77);
    QCOMPARE(payload.value(QStringLiteral("detailId")).toString(), QStringLiteral("I042"));
    QCOMPARE(payload.value(QStringLiteral("title")).toString(), QStringLiteral("Quadratic Core Fact"));

    const QJsonArray tags = payload.value(QStringLiteral("tags")).toArray();
    QCOMPARE(tags.size(), 2);
    QCOMPARE(tags.at(0).toString(), QStringLiteral("key"));
    QCOMPARE(tags.at(1).toString(), QStringLiteral("exam"));

    QCOMPARE(payload.value(QStringLiteral("statementHtml")).toString(), QStringLiteral("<p>y=x^2</p>"));
    QCOMPARE(payload.value(QStringLiteral("coreHtml")).toString(), QStringLiteral("<p>y=x^2</p>"));
    QVERIFY(payload.value(QStringLiteral("conditionHtml")).toString().contains(QStringLiteral("a != 0")));
    QVERIFY(payload.value(QStringLiteral("remarkHtml")).toString().contains(QStringLiteral("Keep coefficient sign consistent.")));

    const QJsonArray vars = payload.value(QStringLiteral("vars")).toArray();
    QCOMPARE(vars.size(), 1);
    QCOMPARE(vars.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("a"));

    const QJsonArray sections = payload.value(QStringLiteral("sections")).toArray();
    QCOMPARE(sections.size(), 5);
    QCOMPARE(sections.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("intuition"));
    QCOMPARE(sections.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("proof"));
    QCOMPARE(sections.at(2).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("pitfalls"));
    QCOMPARE(sections.at(3).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("usage"));
    QCOMPARE(sections.at(4).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("summary"));
}

void DetailRenderingChainTest::mapper_missingFieldsFallback_andRichHtmlCompatibility()
{
    domain::adapters::ConclusionDetailViewData detail;
    detail.conclusionId = QStringLiteral("I100");
    detail.title = QStringLiteral("Fallback Case");
    detail.summary = QStringLiteral("line1\nline2");
    detail.conditionText = QString();
    detail.remarkText = QString();
    detail.isValid = true;

    detail.sections = {
        {QStringLiteral("condition"), QStringLiteral("Condition"), QString(), QStringLiteral("x > 0"), QStringLiteral("normal"), true},
        {QStringLiteral("notes"), QStringLiteral("Note"), QStringLiteral("<p><em>rich note</em></p>"), QString(), QStringLiteral("note"), true},
        {QStringLiteral("proof"), QStringLiteral("Proof"), QStringLiteral("<div class=\"proof\"><b>Raw HTML</b></div>"), QStringLiteral("unused"), QStringLiteral("normal"), true},
    };

    ui::detail::DetailViewDataMapper mapper;
    const QJsonObject payload = mapper.buildContentPayload(detail, 9);

    QCOMPARE(payload.value(QStringLiteral("statementHtml")).toString(), QStringLiteral("<p>line1<br/>line2</p>"));
    QCOMPARE(payload.value(QStringLiteral("conditionHtml")).toString(), QStringLiteral("<p>x &gt; 0</p>"));
    QCOMPARE(payload.value(QStringLiteral("remarkHtml")).toString(), QStringLiteral("<p><em>rich note</em></p>"));
    QVERIFY(!payload.contains(QStringLiteral("displayVersion")));

    const QJsonObject proofSection = findSectionById(payload.value(QStringLiteral("sections")).toArray(), QStringLiteral("proof"));
    QVERIFY(!proofSection.isEmpty());
    QCOMPARE(proofSection.value(QStringLiteral("html")).toString(), QStringLiteral("<div class=\"proof\"><b>Raw HTML</b></div>"));
}

void DetailRenderingChainTest::resources_missingDetailFiles_routesToFallback()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY(setupRendererSandbox(
        sandbox.path(), true, true, false, true, true, true));

    ScopedCurrentDir cwdGuard(sandbox.path());
    ui::detail::DetailHtmlRenderer renderer;
    QVERIFY(!renderer.isReady());
    QVERIFY(renderer.lastError().contains(QStringLiteral("missing required detail resources")));
    QVERIFY(renderer.lastError().contains(QStringLiteral("detail.js")));

    const ui::detail::DetailRenderPath path = ui::detail::DetailRenderPathResolver::resolve(
        true, renderer.isReady(), renderer.isReady(), true);
    QCOMPARE(path, ui::detail::DetailRenderPath::FallbackText);
}

void DetailRenderingChainTest::resources_missingKatexFiles_routesToFallback()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY(setupRendererSandbox(
        sandbox.path(), true, true, true, false, true, true));

    ScopedCurrentDir cwdGuard(sandbox.path());
    ui::detail::DetailHtmlRenderer renderer;
    QVERIFY(!renderer.isReady());
    QVERIFY(renderer.lastError().contains(QStringLiteral("missing required detail resources")));
    QVERIFY(renderer.lastError().contains(QStringLiteral("katex.min.css")));

    const ui::detail::DetailRenderPath path = ui::detail::DetailRenderPathResolver::resolve(
        true, renderer.isReady(), renderer.isReady(), true);
    QCOMPARE(path, ui::detail::DetailRenderPath::FallbackText);
}

void DetailRenderingChainTest::resources_complete_routesToWeb()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");
    QVERIFY(setupRendererSandbox(
        sandbox.path(), true, true, true, true, true, true));

    ScopedCurrentDir cwdGuard(sandbox.path());
    ui::detail::DetailHtmlRenderer renderer;
    QVERIFY(renderer.isReady());
    QVERIFY(renderer.lastError().isEmpty());
    QVERIFY(renderer.detailTemplateUrl().isValid());

    const ui::detail::DetailRenderPath path = ui::detail::DetailRenderPathResolver::resolve(
        true, renderer.isReady(), true, true);
    QCOMPARE(path, ui::detail::DetailRenderPath::Web);
}

void DetailRenderingChainTest::fallbackBuilder_outputsCoreContent_andSectionOrder()
{
    domain::adapters::ConclusionDetailViewData detail = makeBaselineDetailView();
    detail.sections = {
        {QStringLiteral("statement"), QStringLiteral("Conclusion"), QString(), QStringLiteral("C"), QStringLiteral("normal"), true},
        {QStringLiteral("intuition"), QStringLiteral("Intuition"), QString(), QStringLiteral("I"), QStringLiteral("normal"), true},
        {QStringLiteral("proof"), QStringLiteral("Proof"), QString(), QStringLiteral("P"), QStringLiteral("normal"), true},
        {QStringLiteral("pitfalls"), QStringLiteral("Pitfalls"), QString(), QStringLiteral("Pi"), QStringLiteral("pitfall"), true},
        {QStringLiteral("usage"), QStringLiteral("Usage"), QString(), QStringLiteral("U"), QStringLiteral("normal"), true},
        {QStringLiteral("summary"), QStringLiteral("Summary"), QString(), QStringLiteral("S"), QStringLiteral("summary"), true},
    };

    const QString html = ui::detail::DetailFallbackContentBuilder::buildFallbackHtml(detail);
    QVERIFY(html.contains(QStringLiteral("Quadratic Core Fact")));
    QVERIFY(html.contains(QStringLiteral("I042")));
    QVERIFY(html.contains(QStringLiteral("Algebra")));
    QVERIFY(html.contains(QStringLiteral("f(x)=x^2+bx+c has axis")));

    const int idxConclusion = html.indexOf(QStringLiteral("<h4>Conclusion</h4>"));
    const int idxIntuition = html.indexOf(QStringLiteral("<h4>Intuition</h4>"));
    const int idxProof = html.indexOf(QStringLiteral("<h4>Proof</h4>"));
    const int idxPitfalls = html.indexOf(QStringLiteral("<h4>Pitfalls</h4>"));
    const int idxUsage = html.indexOf(QStringLiteral("<h4>Usage</h4>"));
    const int idxSummary = html.indexOf(QStringLiteral("<h4>Summary</h4>"));

    QVERIFY(idxConclusion >= 0);
    QVERIFY(idxIntuition > idxConclusion);
    QVERIFY(idxProof > idxIntuition);
    QVERIFY(idxPitfalls > idxProof);
    QVERIFY(idxUsage > idxPitfalls);
    QVERIFY(idxSummary > idxUsage);
}

void DetailRenderingChainTest::fallbackBuilder_trialPreviewSnippet_andTruncation()
{
    domain::adapters::ConclusionDetailViewData detail = makeBaselineDetailView();
    detail.sections.clear();
    detail.sections.push_back(
        {QStringLiteral("proof"),
         QStringLiteral("Proof"),
         QString(),
         QStringLiteral("This is a very long snippet used to verify truncation behavior in trial preview mode."),
         QStringLiteral("normal"),
         true});

    const QString html = ui::detail::DetailFallbackContentBuilder::buildTrialPreviewHtml(
        detail, QStringLiteral("I042"), QStringLiteral("Trial mode only"), 36);

    QVERIFY(html.contains(QStringLiteral("Quadratic Core Fact")));
    QVERIFY(html.contains(QStringLiteral("<b>ID:</b> I042")));
    QVERIFY(html.contains(QStringLiteral("Trial mode only")));
    QVERIFY(html.contains(QStringLiteral("This is a very long snippet used to...")));
}

void DetailRenderingChainTest::perfAggregator_keyPhasesRecorded_andLifecycleStable()
{
    ui::detail::DetailPerfAggregator aggregator;

    aggregator.beginRequest(QStringLiteral("I042"), 200);
    QVERIFY(aggregator.hasActiveRequest(200));
    aggregator.recordPhase(
        QStringLiteral("I042"), 200, QStringLiteral("detail_payload_ready"), 24, QStringLiteral("dt=24ms cache=hit sections=6"));
    aggregator.recordPhase(
        QStringLiteral("I042"), 200, QStringLiteral("dispatch_to_web_callback"), 39, QStringLiteral("dt=15ms accepted=true ok=true"));
    aggregator.recordPhase(
        QStringLiteral("I042"), 200, QStringLiteral("first_meaningful_paint_dispatch"), 80, QString());
    aggregator.recordPhase(
        QStringLiteral("I042"), 200, QStringLiteral("katex_visible_done"), 134, QStringLiteral("dt=54ms targets=4 katexMs=51"));
    aggregator.recordPhase(
        QStringLiteral("I042"), 200, QStringLiteral("render_heavy_sections_done"), 221, QStringLiteral("dt=87ms sections=6"));
    aggregator.recordPhase(
        QStringLiteral("I042"), 200, QStringLiteral("katex_deferred_done"), 289, QStringLiteral("dt=68ms targets=2 katexMs=66"));
    aggregator.finishRequest(QStringLiteral("I042"), 200, QStringLiteral("render_complete"), 301, QString());
    QVERIFY(!aggregator.hasActiveRequest(200));
}

QTEST_APPLESS_MAIN(DetailRenderingChainTest)

#include "test_detail_rendering_chain.moc"
