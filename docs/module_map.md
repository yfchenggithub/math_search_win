# 模块地图

## 1. 核心目录索引

| 目录 | 说明 |
|---|---|
| `src/ui` | 主窗口、页面、Widget、详情渲染桥接，业务入口最集中。 |
| `src/domain/services` | 搜索和建议算法核心。 |
| `src/infrastructure/data` | 索引与内容 JSON 加载和仓库封装。 |
| `src/domain/repositories` | 收藏/历史/设置业务仓库。 |
| `src/infrastructure/storage` | 本地 JSON 读写与原子写盘。 |
| `src/license` | 激活、license 校验、功能门控。 |
| `src/ui/detail` | 详情 Web 渲染协同（请求管理、payload 映射、shell 分发、性能聚合）。 |
| `resources/detail` | 详情模板、样式、JS 运行时。 |
| `resources/katex` | 数学公式渲染依赖。 |
| `data` | 运行时业务数据（内容 + 索引）。 |
| `cache` | 运行时持久化数据（收藏/历史/设置）。 |
| `tests` | 搜索、建议、存储、授权、页面 wiring、详情性能测试。 |

---

## 2. 核心类地图

### 2.1 主窗口 / 应用壳层

| 类 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `MainWindow` | `src/ui/main_window.h/.cpp` | 装配服务与页面、管理页面切换、跨页信号编排 | `main()` | `SearchService`/`SuggestService`/`LicenseService`/各 Page | `setupPages`、`loadSearchData`、`switchPageWithTrigger` |

### 2.2 页面类

| 类 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `SearchPage` | `src/ui/pages/search_page.h/.cpp` | 搜索、建议、结果、详情、收藏、历史写入、功能门控 | `MainWindow` | `SearchService`、`SuggestService`、`Conclusion*Repository`、`Detail*`、`FeatureGate` | `runSearch`、`runSuggest`、`renderDetailForRequest`、`onFavoriteButtonClicked` |
| `HomePage` | `src/ui/pages/home_page.h/.cpp` | 首页导航与最近/收藏预览 | `MainWindow` | `HistoryRepository`、`FavoritesRepository`、`ConclusionIndexRepository` | `reloadData`、`rebuildRecentPreview`、`rebuildFavoritesPreview` |
| `FavoritesPage` | `src/ui/pages/favorites_page.h/.cpp` | 收藏列表展示、取消收藏、打开详情 | `MainWindow` | `FavoritesRepository`、`ConclusionContentRepository`、`ConclusionIndexRepository` | `reloadData`、`rebuildCards`、`buildItemFromId` |
| `RecentSearchesPage` | `src/ui/pages/recent_searches_page.h/.cpp` | 历史展示、重搜、删除、清空 | `MainWindow` | `HistoryRepository` | `reloadData`、`handleSearchAgain`、`handleClearAll` |
| `SettingsPage` | `src/ui/pages/settings_page.h/.cpp` | 软件/授权/数据状态展示与帮助入口 | `MainWindow` | `LicenseService`、`Conclusion*Repository`、`AppPaths` | `reloadData`、`buildDataStatusText` |
| `ActivationPage` | `src/ui/pages/activation_page.h/.cpp` | 激活码输入、校验、写 license、刷新状态 | `MainWindow` | `ActivationCodeService`、`LicenseService`、`DeviceFingerprintService` | `onActivateClicked`、`reloadData`、`updateLicenseStateUi` |

### 2.3 Service 类

| 类 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `SearchService` | `src/domain/services/search_service.h/.cpp` | term/prefix 命中、过滤、打分、排序 | `SearchPage` | `ConclusionIndexRepository` | `search` |
| `SuggestService` | `src/domain/services/suggest_service.h/.cpp` | prefix/term 候选收集、评分、去重 | `SearchPage` | `ConclusionIndexRepository` | `suggest` |
| `LicenseService` | `src/license/license_service.h/.cpp` | 读写 `license.dat`、解析校验、状态机、发信号 | `MainWindow`、`ActivationPage`、`SettingsPage` | `DeviceFingerprintService`、`FeatureGate` | `initialize`、`reload`、`validateLicense`、`writeLicenseFile` |
| `ActivationCodeService` | `src/license/activation_code_service.h/.cpp` | 激活码解析、CRC/设备/过期/功能校验、生成 license 内容 | `ActivationPage` | `FeatureGate` | `parseActivationCode`、`validateActivationCode`、`buildLicenseFileContent` |
| `FeatureGate` | `src/license/feature_gate.h/.cpp` | LicenseState -> 功能启用矩阵 | `MainWindow`、`SearchPage`、`FavoritesPage` | `LicenseState` | `setLicenseState`、`isEnabled`、`disabledReason` |
| `DeviceFingerprintService` | `src/license/device_fingerprint_service.h/.cpp` | 生成设备指纹（机器信息 + SHA256） | `LicenseService`、`ActivationPage` | Qt 系统信息 | `deviceFingerprint`、`buildDeviceFingerprint` |

### 2.4 Repository / Data 类

| 类 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `ConclusionIndexRepository` | `src/infrastructure/data/conclusion_index_repository.h/.cpp` | 索引加载与检索 API | `MainWindow`、`SearchService`、`SuggestService`、页面 | `BackendSearchIndexLoader` | `loadFromFile`、`findTerm`、`findPrefix`、`getDocById` |
| `ConclusionContentRepository` | `src/infrastructure/data/conclusion_content_repository.h/.cpp` | 内容加载与按 ID 读取 | `MainWindow`、`SearchPage`、`FavoritesPage`、`SettingsPage` | `CanonicalContentLoader` | `loadFromFile`、`getById` |
| `BackendSearchIndexLoader` | `src/infrastructure/data/backend_search_index_loader.h/.cpp` | 解析索引 JSON，提供 diagnostics | `ConclusionIndexRepository` | `AppPaths` | `loadFromFile` |
| `CanonicalContentLoader` | `src/infrastructure/data/canonical_content_loader.h/.cpp` | 解析内容 JSON，提供 diagnostics | `ConclusionContentRepository` | 路径探测逻辑 | `loadFromFile` |
| `FavoritesRepository` | `src/domain/repositories/favorites_repository.h/.cpp` | 收藏 ID 读写与自动保存 | `SearchPage`、`FavoritesPage`、`HomePage` | `LocalStorageService` | `load`、`add`、`remove`、`allIds` |
| `HistoryRepository` | `src/domain/repositories/history_repository.h/.cpp` | 历史记录读写、去重、容量限制 | `SearchPage`、`RecentSearchesPage`、`HomePage` | `LocalStorageService` | `load`、`addQuery`、`removeQuery`、`recentItems` |
| `SettingsRepository` | `src/domain/repositories/settings_repository.h/.cpp` | 设置键值持久化 | 主要在 tests | `LocalStorageService`、`AppSettings` | `load`、`setValue`、`resetToDefaults` |
| `LocalStorageService` | `src/infrastructure/storage/local_storage_service.h/.cpp` | `cache` 路径管理 + JSON 原子写入 | 各业务仓库 + `FavoritesPage` | `AppPaths`、`QSaveFile` | `favoritesFilePath`、`historyFilePath`、`settingsFilePath`、`writeJsonFileAtomically` |

### 2.5 数据模型类

| 类/结构 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数/字段 |
|---|---|---|---|---|---|
| `ConclusionRecord` 及嵌套模型 | `src/domain/models/conclusion_record.h/.cpp` | 详情内容主数据结构（identity/meta/content/assets/ext） | `CanonicalContentLoader`、`ConclusionDetailAdapter` | Qt 容器 | `content.sections`、`content.plain` |
| `SearchOptions/SearchHit/SearchResult` | `src/domain/models/search_result_models.h/.cpp` | 搜索输入输出模型与 query 归一化 | `SearchService`、`SearchPage` | Qt 容器 | `normalizeQueryText` |
| `SuggestOptions/SuggestionResult` | `src/domain/models/search_result_models.h/.cpp` | Suggest 输入输出模型 | `SuggestService`、`SearchPage` | Qt 容器 | `maxResults/filters` |
| `BackendSearchIndex` 及相关结构 | `src/domain/models/search_index_models.h/.cpp` | 索引文档、posting、fieldMask 映射模型 | `BackendSearchIndexLoader`、`ConclusionIndexRepository` | Qt 容器 | `decodeFieldMask` |
| `AppSettings` | `src/domain/models/app_settings.h/.cpp` | 设置默认值与版本 | `SettingsRepository` | `QVariantMap` | `defaultValues`、`kVersion` |
| `SearchHistoryItem` | `src/domain/models/search_history_item.h` | 历史记录条目结构 | `HistoryRepository`、UI 页 | `QDateTime` | `query/source/searchedAt` |

### 2.6 授权类

| 类 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `LicenseService` | `src/license/license_service.h/.cpp` | license 文件读取、解析、校验与状态广播 | `MainWindow`、`ActivationPage`、`SettingsPage` | `DeviceFingerprintService`、`FeatureGate` | `initialize`、`reload`、`validateLicense` |
| `ActivationCodeService` | `src/license/activation_code_service.h/.cpp` | 激活码解析、CRC 校验、功能集解析、生成 license 文本 | `ActivationPage` | `FeatureGate` | `parseActivationCode`、`validateActivationCode` |
| `FeatureGate` | `src/license/feature_gate.h/.cpp` | 授权状态映射到功能开关 | `MainWindow`、`SearchPage`、`FavoritesPage` | `LicenseState` | `isEnabled`、`disabledReason` |
| `DeviceFingerprintService` | `src/license/device_fingerprint_service.h/.cpp` | 生成设备指纹 | `LicenseService`、`ActivationPage` | `QSysInfo`、`QCryptographicHash` | `deviceFingerprint` |
| `LicenseState` | `src/license/license_state.h/.cpp` | 授权状态 DTO 与状态码映射 | 授权服务与 UI | Qt 元类型系统 | `licenseStatusCode`、`licenseStatusDisplayText` |

### 2.7 工具类

| 类/模块 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `Logger` | `src/core/logging/logger.h/.cpp` | 统一日志输出（console + file） | 全模块 | Qt IO/时间/线程 | `initialize`、`log`、`shutdown` |
| `LogCategory` | `src/core/logging/log_categories.h` | 统一日志分类常量 | 全模块 | 无 | `AppStartup`、`SearchEngine` 等 |
| `AppPaths` | `src/shared/paths.h/.cpp` | 运行时根目录与 data/cache/license 路径解析 | data/storage/license/ui | Qt 路径 API | `appRoot`、`dataDir`、`cacheDir`、`licenseDir` |
| `UiConstants` | `src/shared/constants.h` | 页面索引与 UI 常量 | `MainWindow`、页面 | Qt 字符串 | `kPage*`、`kStatusVersion` |

### 2.8 Web 渲染相关类

| 类 | 文件 | 主要职责 | 上游调用方 | 下游依赖 | 重要函数 |
|---|---|---|---|---|---|
| `DetailHtmlRenderer` | `src/ui/detail/detail_html_renderer.h/.cpp` | 校验资源完整性，生成 JS init/render 脚本 | `SearchPage`、`DetailPane` | `resources/detail`、`resources/katex` | `isReady`、`buildInitScript`、`buildRenderScript` |
| `DetailRenderCoordinator` | `src/ui/detail/detail_render_coordinator.h/.cpp` | 请求 ID、stale 判断、已渲染去重 | `SearchPage` | 时间戳 | `createRequest`、`isRequestStale`、`markRendered` |
| `DetailViewDataMapper` | `src/ui/detail/detail_view_data_mapper.h/.cpp` | 详情 ViewData -> Web payload | `SearchPage` | `ConclusionDetailViewData` | `buildContentPayload`、`buildEmptyPayload`、`buildErrorPayload` |
| `DetailPane` | `src/ui/detail/detail_pane.h/.cpp` | Web shell 加载、payload 分发、perf 回传 | `SearchPage` | `QWebEngineView`、`DetailHtmlRenderer` | `ensureShellLoaded`、`renderDetail`、`dispatchNow` |
| `DetailPerfAggregator` | `src/ui/detail/detail_perf_aggregator.h/.cpp` | 渲染阶段归一化和统计日志聚合 | `SearchPage` | perf 事件流 | `recordPhase`、`finishRequest`、`cancelRequest` |

---

## 3. 关键文件速查表

| 文件 | 为什么重要 | 修改影响 |
|---|---|---|
| `src/main.cpp` | 进程入口与 probe 开关 | 影响启动行为和全局初始化 |
| `src/ui/main_window.cpp` | 系统装配中心 | 影响页面编排、跨页联动、加载顺序 |
| `src/ui/pages/search_page.cpp` | 最大业务聚合点 | 影响搜索/Suggest/详情/收藏/历史/门控 |
| `src/domain/services/search_service.cpp` | 搜索算法核心 | 影响命中质量和排序结果 |
| `src/domain/services/suggest_service.cpp` | 建议算法核心 | 影响输入联想与点击转化 |
| `src/ui/detail/detail_pane.cpp` | Web 渲染分发桥 | 影响详情是否展示、是否回退 |
| `resources/detail/detail.js` | 详情前端运行时 | 影响渲染性能与公式显示 |
| `src/infrastructure/storage/local_storage_service.cpp` | 统一落盘入口 | 影响收藏/历史/设置持久化稳定性 |
| `src/license/license_service.cpp` | 授权状态机 | 影响 trial/full 判定和功能开放 |
| `src/license/activation_code_service.cpp` | 激活码校验核心 | 影响激活成功率和安全性 |
| `src/shared/paths.cpp` | 运行路径解析 | 影响数据/资源/license 查找 |
| `CMakeLists.txt` | 编译单元清单 | 影响模块是否被实际编译进程序 |

---

## 4. 常见需求对应入口

### 改搜索逻辑
- 先看：`SearchPage::runSearch`
- 再看：`SearchService::search`
- 数据侧：`ConclusionIndexRepository`、`BackendSearchIndexLoader`

### 改 suggest
- 先看：`SearchPage::runSuggest`、`onSuggestionClicked`
- 再看：`SuggestService::suggest`

### 改详情模板与渲染
- C++ 调度：`SearchPage::renderDetailForRequest`、`DetailPane`
- payload 映射：`DetailViewDataMapper`
- 前端模板：`resources/detail/detail_template.html` / `detail.js` / `detail.css`

### 改收藏持久化
- 业务仓库：`FavoritesRepository`
- 持久化底层：`LocalStorageService`
- 页面联动：`SearchPage`、`FavoritesPage`、`HomePage`

### 改历史记录
- 写入触发：`SearchPage::runSearch`（triggerSource 条件）
- 仓库：`HistoryRepository`
- 页面：`RecentSearchesPage`

### 改设置项
- 模型与默认值：`AppSettings`
- 仓库：`SettingsRepository`
- 页面现状：`SettingsPage` 当前主要只读展示（注意未接线）

### 改授权/激活
- 页面：`ActivationPage`
- 激活码：`ActivationCodeService`
- 授权状态：`LicenseService`
- 功能开关：`FeatureGate`

### 查启动和装配问题
- `main.cpp`
- `MainWindow::MainWindow`
- `MainWindow::loadSearchData`
- `MainWindow::setupPages`

### 查日志和性能问题
- `core/logging/logger.h/.cpp`
- `LogCategory` 分类：`search.engine`、`detail.render`、`perf.*`、`file.io`
- 详情性能：`DetailPerfAggregator` + `resources/detail/detail.js` perf 事件
