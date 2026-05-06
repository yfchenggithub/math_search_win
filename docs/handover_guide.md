# 接手指南

## 1. 项目一句话说明

这是一个 `C++17 + Qt 6 Widgets + QWebEngine + QPdfView` 的 Windows 本地离线桌面应用，当前已实现“搜索 -> 详情查看 -> 收藏/历史 -> 离线激活门控”的可运行闭环。

## 2. 第一天如何快速跑起来

1. 准备依赖：Qt 6.11.0（`msvc2022_64`）与 CMake 3.21+。
2. 配置构建：

```powershell
cmake --preset msvc-debug
```

3. 编译：

```powershell
cmake --build --preset msvc-debug
```

4. 运行（仓库根目录）：

```powershell
powershell .\run-debug.ps1
```

5. 启动后先做 6 个烟测：
- 确认主窗口启动即最大化（非独占全屏，标题栏仍可见）。
- 首页检查：确认首屏存在“信任标签 + 立即开始搜索 + 价值证明卡片”。
- 搜索关键词，确认结果列表变化。
- 检查左侧“快速筛选”区域是否仅保留“模块 + 排序”两项，且顶部“重置”按钮可点击。
- 搜索框右侧清空按钮可用（非空时显示，点击后清空）。
- 点击结果，确认右侧详情显示（Web 或 fallback）。
- 未选中结果时，确认详情工具栏（`Aa-/全屏/上一页/下一页/适合宽度/导出PDF/收藏`）均禁用，页码显示 `PDF --/--`。
- 选中结果后，确认详情工具栏恢复可用；若当前非 PDF 分支，页码显示 `PDF 暂不可用`。
- 当使用 PDF 渲染时，确认详情区可滚动跨页，且“上一页/下一页”按钮可用（多页 PDF）。
- 当使用 PDF 渲染时，确认默认先执行一次“适合宽度”，并可点击“适合宽度”按钮再次贴合容器宽度。
- 当使用 PDF 渲染时，点击“导出PDF”并保存到临时路径，确认可生成副本文件；导出成功后弹出提示框，且可通过“打开导出目录”按钮直接定位文件目录。
- 点击详情区“全屏”，确认字体自动切换为大档；退出全屏后自动切回小档。
- 点击 `Aa` 按钮，确认可循环切换三档（`2 -> 1 -> 0 -> 2`）。
- 在详情区按住 `Ctrl` 滚动鼠标滚轮，确认字体可连续放大/缩小（不受三档限制）并即时生效。
- 点击详情区“全屏”，确认仅右侧详情区进入沉浸模式（顶部搜索栏和左侧结果栏隐藏）；按 `Esc` 或再次点击“退出全屏”可恢复。
- 收藏/取消收藏一条，确认 `cache/favorites.json` 有变化。
- 进入“我的收藏”页，点击“清空收藏”，确认列表清空且 `cache/favorites.json` 变为空集合。
- 进入“设置/关于”，点击“打开 README”，确认至少可通过默认程序或记事本打开。

## 3. 先读哪些文件最容易建立全局认识

按这个顺序读最快：
1. `src/main.cpp`
2. `src/ui/main_window.h/.cpp`
3. `src/ui/pages/search_page.h/.cpp`
4. `src/domain/services/search_service.cpp`
5. `src/domain/services/suggest_service.cpp`
6. `src/ui/detail/detail_pane.cpp` + `app_resources/detail/detail.js`
7. `src/domain/repositories/*_repository.cpp`
8. `src/license/license_service.cpp` + `src/ui/pages/activation_page.cpp`

## 4. 如何理解搜索系统

- UI 入口在 `SearchPage`：
  - 输入变化：`onQueryTextChanged()` -> `runSuggest()`
  - 执行搜索：`onQueryReturnPressed/onSearchButtonClicked/onSuggestionClicked` -> `runSearch()`
  - 结果摘要：`updateResultSummary()`（普通模式显示“已显示 X / 共 Y 条 / 关键词 / 筛选 / 排序”，开发模式保留 `query/shown/total/elapsed`）
  - 结果卡片：`buildResultCard()` + `highlightKeyword()`；标题后追加 `(当前条目/当前列表总条数)`
  - 快速筛选：仅 `module` 筛选 + `sort` 排序在左栏快速筛选卡，`clearFiltersButton_` 负责一键重置筛选条件
- 算法在 `SearchService::search()`：
  - `termIndex + prefixIndex` 合并评分
  - `fieldMaskWeight` 已包含 `intent/usage/knowledge_node`（对应 bit 存在时生效）
  - `enableIntentCrossBoost` 默认开启：同文档命中 `intent` 且命中 `title/alias/keyword` 时追加交叉加分
  - module 过滤
  - score 排序
- Suggest 在 `SuggestService::suggest()`：
  - 优先使用索引顶层 `suggestions` seed（`optionalSuggestions()`）
  - 候选不足时回退 `prefixIndex + termIndex`
  - 对 prefix/term 文本执行质量门：过滤未闭合括号半截候选，以及可被更长同义候选覆盖的语义截断前缀，减少 UI 脏词
- `domain_topic_map.json` 由 `ConclusionIndexRepository::loadDomainTopicMap()` 在启动阶段尝试加载；map 可用时 Suggest 会先走 domain/topic 扩展候选（`source=domain_topic`），缺失/损坏会降级为扁平候选。
- 数据源在 `ConclusionIndexRepository`，主文件为 `data/backend_search_index.json`，可选 `data/domain_topic_map.json`。
- 历史写入仅在 `button/return/suggest_click` 三类触发中执行（`HistoryRepository::addQuery`）。

## 5. 如何理解详情渲染系统

- 主流程：`onResultSelectionChanged()` -> `enqueueDetailRenderRequest()` -> `renderDetailForRequest()`。
- 数据准备：
  - `ConclusionContentRepository::getById()` 读 `data/canonical_content_v2.json`
  - `ConclusionDetailAdapter::toViewData()`
  - `DetailViewDataMapper::buildContentPayload()`
- 分支决策：
  - `DetailRenderPathResolver::resolveForMode()` 决定 `TrialPreview / Pdf / Web / FallbackText`
- 渲染模式：
  - PDF 模式（默认）：`QPdfView` + `QPdfDocument`，路径由 `resolveDetailPdfPath()` 解析
    - 视图模式：`QPdfView::PageMode::MultiPage`
    - 宽度贴合：`applyPdfFitToWidth()`（加载成功后默认执行；也可通过 `onPdfFitWidthClicked()` 手动触发）
    - 翻页入口：`onPdfPrevPageClicked` / `onPdfNextPageClicked` -> `jumpToPdfPage`
    - 状态刷新：`updatePdfPageNavigationUi`（`PDF --/--` / `PDF 暂不可用` / `PDF x/y` 与按钮可用性）
    - 导出入口：`onPdfExportButtonClicked`（读取 `currentDetailPdfPath_` 后执行“另存为”复制；成功后弹窗提示并提供“打开导出目录”）
    - 字体滚轮入口：`eventFilter` -> `tryAdjustDetailFontScaleByWheelDelta`（详情区 `Ctrl + 鼠标滚轮`）
    - 全屏入口：`onDetailFullscreenButtonClicked`；快捷键 `F11` 切换，`Esc` 退出（`leaveDetailFullscreen`）
    - 页面控制：`enterDetailFullscreen()` 隐藏 `searchTopBar_` 与 `searchLeftColumn_`，退出时恢复并还原 splitter 尺寸
  - Web 模式：`DetailPane` + `app_resources/detail/detail_template.html` + `detail.js` + `katex`
  - 回退模式：`QTextBrowser`（`renderDetailInFallbackBrowser` + `DetailFallbackContentBuilder::buildFallbackHtml`）
  - Trial 预览：`showTrialDetailPreview` + `DetailFallbackContentBuilder::buildTrialPreviewHtml`
- PDF 路径规则（已实现）：
  - map：`data/conclusion_pdf_map.json`（扁平 `id -> pdf`）
  - 目录：`data/conclusion_pdfs/`
  - 兜底顺序：map -> `record.assets.pdf` -> `<id>.pdf`
- 并发与抖动控制：
  - `DetailRenderCoordinator` 管 requestId/stale
  - `detailSelectionCoalesceTimer_` 18ms 合并快速切换

## 6. 如何理解本地存储系统

- 底层：`LocalStorageService`（`cache` 目录、原子写盘 `QSaveFile`）。
- 业务仓库：
  - 收藏：`FavoritesRepository` -> `cache/favorites.json`
  - 历史：`HistoryRepository` -> `cache/history.json`
  - 设置：`SettingsRepository` -> `cache/settings.json`
- 收藏 schema 现状：
  - 写入：统一输出 `ids + items`。
  - 兼容：`load()` 兼容历史文件（仅 `ids`、仅 `items` 或混合）。
  - 时间：`items` 可包含 `favoritedAt/updatedAt/createdAt`，用于排序信息保留。
- 重要现状：`SettingsRepository` 已被 `SearchPage` 用于详情渲染模式持久化（`detail_render_mode`）与详情字体状态缓存（`detail_font_scale_level` 三档 + `detail_font_wheel_ticks` 连续缩放偏移）；`SettingsPage` 仍是只读状态页（含日志目录、README 打开入口）。

## 7. 如何理解激活/授权系统

- 启动校验：`LicenseService::initialize/reload` 读取 `license/license.dat`。
- 状态传播：`licenseStateChanged` -> `MainWindow` -> `FeatureGate::setLicenseState` -> 各页面刷新。
- 激活入口：`ActivationPage::onActivateClicked()`。
- 激活流程：
  - `ActivationCodeService::parseActivationCode`
  - `validateActivationCode`
  - `buildLicenseFileContent`
  - `LicenseService::writeLicenseFile + reload`
- 重要风险：签名校验/解密当前为 TODO stub（非完整安全实现）。

## 8. 常见排错路径

### 搜不到结果怎么办
- 先断点 `SearchPage::runSearch`，看是否触发。
- 检查 `indexReady_`、`FeatureGate` 是否放行搜索。
- 检查 `ConclusionIndexRepository::loadFromFile` diagnostics。
- 查看日志分类：`search.index`、`search.engine`。

### suggest 不显示怎么办
- 断点 `onQueryTextChanged`、`runSuggest`。
- 检查 `suggestService_` 和 `indexReady_`。
- 检查 `lastSuggestSignature_` 是否误判重复。
- 查看 `SuggestionResult.items` 是否为空。
- 若你正在做层级推荐联调：先检查 `MainWindow::loadSearchData` 中 `loadDomainTopicMap` 告警日志，再在 `SuggestService::suggest` 里确认是否产生 `source=domain_topic` 候选。

### 点击结果详情不出来怎么办
- 断点 `onResultSelectionChanged`、`flushPendingDetailRequest`、`renderDetailForRequest`。
- 看 `contentReady_`、`contentRepository_->getById(docId)` 是否命中。
- 看 `detailRenderCoordinator_->isRequestStale(requestId)` 是否持续淘汰请求。

### KaTeX / WebEngine 渲染异常怎么办
- 先看 `DetailHtmlRenderer::isReady/lastError`。
- 确认 `app_resources/detail/*` 与 `app_resources/katex/*` 存在。
- 断点 `DetailPane::onShellLoadFinished`、`dispatchNow`。
- 看 `SearchPage::activateTextFallbackMode` 是否被触发（说明已回退文本模式）。

### PDF 详情不显示怎么办
- 先检查 `data/conclusion_pdf_map.json` 是否是扁平对象格式（仅支持 `{ "I028": "I028.pdf" }`）。
- 检查 `data/conclusion_pdfs/` 中目标文件是否存在。
- 断点 `SearchPage::resolveDetailPdfPath`、`renderDetailInPdfView`，确认失败原因。
- 查看 `detail.render` 日志里的 `pdf_unavailable` / `pdf map` 相关记录。

### PDF 只能看一页/不能翻页怎么办
- 先确认 `SearchPage::buildUi` 中 `detailPdfView_->setPageMode(QPdfView::PageMode::MultiPage)` 是否生效。
- 检查 `detailPdfPrevButton_` / `detailPdfNextButton_` 点击是否进入 `onPdfPrevPageClicked` / `onPdfNextPageClicked`。
- 断点 `jumpToPdfPage` 与 `updatePdfPageNavigationUi`，确认 `QPdfDocument::pageCount()` 与 `QPdfPageNavigator::currentPage()` 值变化。

### PDF 导出失败怎么办
- 检查 `detailPdfExportButton_` 是否处于可点击状态（仅当前展示 PDF 时可用）。
- 断点 `SearchPage::onPdfExportButtonClicked`，确认 `currentDetailPdfPath_` 非空且源文件存在。
- 检查目标路径是否可写、是否被同名文件占用（覆盖路径会先尝试删除旧文件）。

### 详情全屏无法退出怎么办
- 优先按 `Esc`；若无效，再按 `F11`（两者都走 `SearchPage` 的全屏切换链路）。
- 断点 `SearchPage::onDetailFullscreenButtonClicked`、`enterDetailFullscreen`、`leaveDetailFullscreen`。
- 检查 `detailPaneFullscreen_` 状态位，以及 `searchTopBar_` / `searchLeftColumn_` / `searchWorkbenchSplitter_` 是否可用。

### 收藏保存失败怎么办
- 断点 `SearchPage::onFavoriteButtonClicked`。
- 检查 `FavoritesRepository::load/save` 返回值。
- 检查 `LocalStorageService::ensureCacheDirExists` 与 `writeJsonFileAtomically`。
- 检查 `cache/favorites.json` 是否可写、是否被外部占用。

### 设置不持久化怎么办
- 当前并非“完全不持久化”：`SearchPage` 会写入 `detail_font_scale_level` 与 `detail_font_wheel_ticks`；运行时默认按详情全屏状态切换大/小档，`Aa` 支持三档循环，详情区 `Ctrl + 鼠标滚轮` 支持连续缩放。
- 若要把“设置/关于”扩展成完整可编辑设置页，仍需在 `SettingsPage` 增加交互并调用 `SettingsRepository::setValue/save`。

### 底部状态栏文案不一致怎么办
- 默认普通模式：`本地离线可用 · 已加载 N 条结论 · 数据就绪`。
- 若需显示详细调试串，设置环境变量：`APP_ENV=dev`（也支持 `debug/development`）。

### 设置页日志目录打不开怎么办
- 断点 `SettingsPage::buildDataInfoSection` 中 `openLogDirButton_` 的 clicked lambda。
- 检查 `logging::Logger::instance().logDirectory()` 是否为空。
- 检查 `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` 返回值。
- 查看 `config` / `file.io` 分类日志中 `open_log_dir` 相关记录。

### 设置页 README 打不开怎么办
- 断点 `SettingsPage::buildHelpSection` 中 `openReadmeButton_` 的 clicked lambda。
- 先确认 `README.md` 绝对路径是否存在（`QDir(AppPaths::appRoot()).filePath("README.md")`）。
- 检查 `cmd /c start "" <README绝对路径>` 是否成功。
- 若默认程序失败，检查 `notepad.exe <README绝对路径>` 是否可拉起。
- 查看 `config` / `file.io` 日志中的 `open_readme` 记录。

### 激活状态异常怎么办
- 断点 `ActivationPage::onActivateClicked` 和 `LicenseService::reload`。
- 检查 `license/license.dat` 内容格式（`format/product/edition/device/features`）。
- 检查 `DeviceFingerprintService::deviceFingerprint()` 与 license 中 `device` 是否一致。
- 检查过期字段 `expire_at` 是否已过期。

## 9. 新人最容易误解的点

- 误解 1：设置页已经是“设置中心”。
  - 现实：当前仍以状态展示为主；仅 `SearchPage` 的详情字体档位接线到了 `SettingsRepository`。
- 误解 2：收藏文件一定有完整 `items` 元数据。
  - 现实：仓库会统一写 `ids + items`，但历史文件可能缺失 `items` 或时间字段，读取时已做兼容与合并。
- 误解 3：激活链路已经是完整安全方案。
  - 现实：签名/解密校验是 TODO stub。
- 误解 4：输入时会自动实时搜索。
  - 现实：输入实时触发的是 suggest；搜索主触发仍是回车/按钮/建议点击。

## 10. 建议后续整理顺序

1. 先补授权安全链路（签名/解密/到期策略）。
2. 落地收藏页筛选能力（当前按钮为禁用占位态）。
3. 拆分 `SearchPage`（搜索编排、详情编排、状态同步分层）。
4. 明确 `SettingsPage` 定位并接线真实持久化。
5. 为 `DetailViewDataMapper` 和 `detail.js` 增加契约测试，降低跨语言改动风险。

---

## 11. 2026-04-21 Release Closure Notes

### 11.1 What changed

- Runtime path resolution and folder checks were centralized in `AppPaths`.
- Startup now runs explicit runtime layout checks and shows status in UI.
- WebEngine runtime cache/storage moved under `cache/webengine`.
- Detail rendering fallback messages are now user-visible for:
  - missing template/app_resources
  - shell load failure
  - JS runtime failure
- License reload now explicitly handles missing/invalid `license` directory path.

### 11.2 New primary file references

- `src/shared/paths.h`
- `src/shared/paths.cpp`
- `src/main.cpp`
- `src/ui/main_window.cpp`
- `src/ui/pages/search_page.cpp`
- `src/ui/detail/detail_html_renderer.cpp`
- `src/ui/detail/detail_pane.cpp`
- `src/license/license_service.cpp`

### 11.3 Handover cautions

- Packaging main entry is `release_tool.py` (commands: `deploy`, `verify`, `package`, `all`).
- Release output now has separate folders:
  - `resources/`: Qt WebEngine runtime files (from `windeployqt`)
  - `app_resources/`: project detail/katex static assets
- Runtime app style file in release output should be under:
  - `app_resources/styles/app.qss`
  - source path `src/ui/style/app.qss` is copied by `release_tool.py` as runtime asset, not as C++ source deployment
- `CMakeLists.txt` sets `WIN32_EXECUTABLE` and keeps `Debug` with `/SUBSYSTEM:CONSOLE`; packaged `Release` exe double-click startup is GUI mode (no cmd console window).
- Do not remove `app_resources/detail` or `app_resources/katex` after running `windeployqt`.
- License cryptographic verification is still not production-grade (TODO stubs remain).

