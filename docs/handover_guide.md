# 接手指南

## 1. 项目一句话说明

这是一个 `C++17 + Qt 6 Widgets + QWebEngine` 的 Windows 本地离线桌面应用，当前已实现“搜索 -> 详情查看 -> 收藏/历史 -> 离线激活门控”的可运行闭环。

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
- 搜索关键词，确认结果列表变化。
- 搜索框右侧清空按钮可用（非空时显示，点击后清空）。
- 点击结果，确认右侧详情显示（Web 或 fallback）。
- 点击详情区 `Aa` 按钮，确认小/中/大三档字体循环并即时生效。
- 收藏/取消收藏一条，确认 `cache/favorites.json` 有变化。
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
- 算法在 `SearchService::search()`：
  - `termIndex + prefixIndex` 合并评分
  - module/category/tag 过滤
  - score 排序
- 数据源在 `ConclusionIndexRepository`，文件是 `data/backend_search_index.json`。
- 历史写入仅在 `button/return/suggest_click` 三类触发中执行（`HistoryRepository::addQuery`）。

## 5. 如何理解详情渲染系统

- 主流程：`onResultSelectionChanged()` -> `enqueueDetailRenderRequest()` -> `renderDetailForRequest()`。
- 数据准备：
  - `ConclusionContentRepository::getById()` 读 `data/canonical_content_v2.json`
  - `ConclusionDetailAdapter::toViewData()`
  - `DetailViewDataMapper::buildContentPayload()`
- 分支决策：
  - `DetailRenderPathResolver::resolve()` 决定 `TrialPreview / Web / FallbackText`
- 渲染模式：
  - Web 模式：`DetailPane` + `app_resources/detail/detail_template.html` + `detail.js` + `katex`
  - 回退模式：`QTextBrowser`（`renderDetailInFallbackBrowser` + `DetailFallbackContentBuilder::buildFallbackHtml`）
  - Trial 预览：`showTrialDetailPreview` + `DetailFallbackContentBuilder::buildTrialPreviewHtml`
- 并发与抖动控制：
  - `DetailRenderCoordinator` 管 requestId/stale
  - `detailSelectionCoalesceTimer_` 18ms 合并快速切换

## 6. 如何理解本地存储系统

- 底层：`LocalStorageService`（`cache` 目录、原子写盘 `QSaveFile`）。
- 业务仓库：
  - 收藏：`FavoritesRepository` -> `cache/favorites.json`
  - 历史：`HistoryRepository` -> `cache/history.json`
  - 设置：`SettingsRepository` -> `cache/settings.json`
- 重要现状：`SettingsRepository` 已被 `SearchPage` 用于详情字体档位持久化（`detail_font_scale_level`）；`SettingsPage` 仍是只读状态页（含日志目录、README 打开入口）。

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

### 点击结果详情不出来怎么办
- 断点 `onResultSelectionChanged`、`flushPendingDetailRequest`、`renderDetailForRequest`。
- 看 `contentReady_`、`contentRepository_->getById(docId)` 是否命中。
- 看 `detailRenderCoordinator_->isRequestStale(requestId)` 是否持续淘汰请求。

### KaTeX / WebEngine 渲染异常怎么办
- 先看 `DetailHtmlRenderer::isReady/lastError`。
- 确认 `app_resources/detail/*` 与 `app_resources/katex/*` 存在。
- 断点 `DetailPane::onShellLoadFinished`、`dispatchNow`。
- 看 `SearchPage::activateTextFallbackMode` 是否被触发（说明已回退文本模式）。

### 收藏保存失败怎么办
- 断点 `SearchPage::onFavoriteButtonClicked`。
- 检查 `FavoritesRepository::load/save` 返回值。
- 检查 `LocalStorageService::ensureCacheDirExists` 与 `writeJsonFileAtomically`。
- 检查 `cache/favorites.json` 是否可写、是否被外部占用。

### 设置不持久化怎么办
- 当前并非“完全不持久化”：`SearchPage` 的详情字体档位已写入 `SettingsRepository`。
- 若要把“设置/关于”扩展成完整可编辑设置页，仍需在 `SettingsPage` 增加交互并调用 `SettingsRepository::setValue/save`。

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
  - 现实：`FavoritesRepository` 默认只写 `ids`，`FavoritesPage` 读取 `items` 只是兼容。
- 误解 3：激活链路已经是完整安全方案。
  - 现实：签名/解密校验是 TODO stub。
- 误解 4：输入时会自动实时搜索。
  - 现实：输入实时触发的是 suggest；搜索主触发仍是回车/按钮/建议点击。

## 10. 建议后续整理顺序

1. 先补授权安全链路（签名/解密/到期策略）。
2. 统一收藏文件 schema（`ids` vs `items`）。
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

