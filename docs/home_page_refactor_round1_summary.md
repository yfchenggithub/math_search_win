# 首页（HomePage）重构（第 1 轮）变更总结

更新时间：2026-04-20

## 1. 本轮目标与边界
本轮仅聚焦首页重构，目标是在不推翻现有工程结构的前提下，把首页从占位页升级为“主路径起点页”。

严格边界：
- 仅改动：
  - `HomePage / home_page`
  - `app.qss`（仅主页相关样式补充）
  - `MainWindow`（仅最小路由接线）
- 明确不改：
  - `SearchPage` 主体逻辑
  - `DetailPage` 主体逻辑
  - `RecentSearchesPage` 主体逻辑
  - `FavoritesPage` 主体逻辑
  - `SettingsPage` 主体逻辑
  - `ActivationPage` 主体逻辑
  - Repository 底层契约与存储结构

## 2. 仓库现状识别结论
- 已存在独立首页文件：
  - `src/ui/pages/home_page.h`
  - `src/ui/pages/home_page.cpp`
- 主路由位于：
  - `src/ui/main_window.h`
  - `src/ui/main_window.cpp`
  - 使用 `QStackedWidget`，页面索引由 `src/shared/constants.h` 定义
- 导航目标页均已存在：
  - 搜索页、最近搜索页、收藏页、设置页、激活页

结论：无需新增 `home_page.*`，在现有 `HomePage` 上最小侵入重构即可。

## 3. 改动文件清单（可追溯）
- `src/ui/pages/home_page.h`
- `src/ui/pages/home_page.cpp`
- `src/ui/style/app.qss`
- `src/ui/main_window.h`
- `src/ui/main_window.cpp`

## 4. 各文件改动说明

### 4.1 `src/ui/pages/home_page.h`
- 将占位类扩展为完整首页页面类。
- 新增导航信号：
  - `navigateToSearchRequested()`
  - `navigateToRecentRequested()`
  - `navigateToFavoritesRequested()`
  - `navigateToSettingsRequested()`
  - `navigateToActivationRequested()`
  - `searchRequested(const QString& query)`
  - `openConclusionRequested(const QString& conclusionId)`
- 新增页面职责方法：
  - `setupUi()`
  - `setupHeroSection()`
  - `setupQuickActionsSection()`
  - `setupPreviewSections()`
  - `setupFooterSection()`
  - `setupConnections()`
  - `reloadData()`
  - `rebuildRecentPreview()`
  - `rebuildFavoritesPreview()`
  - `updateActivationSummaryIfNeeded()`
- 新增首页结构成员（滚动容器、Hero、Quick Actions、Preview、Footer）。

### 4.2 `src/ui/pages/home_page.cpp`
- 完整重构首页 UI，采用：
  - `QWidget` 根容器
  - `QScrollArea`
  - `contentWidget + QVBoxLayout`
  - 各区块独立 section
- 新首页结构：
  1. Hero 区（主标题 + 副标题 + 主按钮“立即搜索” + 次按钮）
  2. Quick Actions（最近搜索、我的收藏、设置 3 张轻卡片）
  3. 轻量预览区（最近搜索预览、收藏预览，各最多 3 条）
  4. 页脚弱工具区（本地离线/计数摘要 + 激活弱入口）
- 数据接入策略：
  - 最近搜索：`HistoryRepository`
  - 收藏：`FavoritesRepository`
  - 收藏标题/模块补全：`ConclusionIndexRepository::getDocById()`
- 交互行为：
  - “立即搜索” -> 导航搜索页
  - 最近/收藏快捷入口 -> 导航对应页面
  - 设置入口 -> 导航设置页
  - 激活入口 -> 导航激活页（弱入口）
  - 最近预览项 -> 发出 `searchRequested(query)`
  - 收藏预览项 -> 发出 `openConclusionRequested(conclusionId)`
- 预览区降级策略：
  - 数据为空时显示轻量空态提示
  - 不新增复杂 model/view，不改底层 repository 结构

### 4.3 `src/ui/style/app.qss`
- 增加首页样式挂钩并保持统一视觉语言（浅灰背景、白卡片、轻边框、克制 hover）。
- 新增/补齐对象样式：
  - 页面根：`homePage`
  - Hero：`homeHero`、`homeHeroTitle`、`homeHeroSubtitle`、`homeHeroPrimaryButton`、`homeHeroSecondaryButton`
  - Quick Actions：`homeQuickActions`、`homeQuickActionCard`、`homeQuickActionTitle`、`homeQuickActionDescription`
  - 预览区：`homePreviewSection`、`homeSectionTitle`、`homeSectionActionButton`、`homePreviewItem`、`homePreviewItemTitle`、`homePreviewItemMeta`
  - 页脚：`homeFooterMeta`
- 保持“首页不是 dashboard”的克制风格，不使用重阴影/夸张渐变。

### 4.4 `src/ui/main_window.h`
- 新增成员：`HomePage* homePage_ = nullptr;`

### 4.5 `src/ui/main_window.cpp`
- 首页实例改为可持有指针：
  - `homePage_ = new HomePage(&indexRepository_, pageStack_);`
- 新增首页导航信号接线：
  - 首页 -> 搜索 / 最近 / 收藏 / 设置 / 激活
- 新增首页预览项行为接线：
  - `searchRequested(query)` -> 切换到搜索页并调用 `triggerSearchFromRecent(query)`
  - `openConclusionRequested(conclusionId)` -> 切换到搜索页并调用 `openConclusionById(conclusionId)`
- 新增首页刷新时机：
  - 切换到首页时 `homePage_->reloadData()`
  - 最近搜索变更、收藏变更后刷新首页预览
- 首页顶栏副标题调整为主路径导向文案。

## 5. 关键产品取向落地
- 首页第一主操作固定为“立即搜索”，视觉权重最高。
- 最近搜索 / 我的收藏是第二层入口（卡片 + 轻预览）。
- 设置、激活降级为弱入口，不抢 Hero 注意力。
- 首页仅做轻量预览，不搬运完整列表页复杂度。

## 6. 数据与契约约束遵循情况
- 复用现有 Repository：`HistoryRepository`、`FavoritesRepository`。
- 未改动底层 JSON/SQLite 结构。
- 未扩展 Repository 契约。
- 对难接入数据采用轻降级（空态/摘要），避免为了首页重构大改底层。

## 7. 编译验证
执行命令：
- `cmake --build out/build/msvc-debug --config Debug`

结果：
- 构建通过，`math_search_win.exe` 产物成功生成。

## 8. 本轮取舍说明
- 为保持范围可控，首页未引入复杂 dashboard 引擎、图表、delegate 自绘系统。
- 激活状态未接入真实服务，仅保留弱入口与轻摘要占位。
- 首页预览条目严格限制为最多 3 条，保证轻量和可扫读。

## 9. 后续建议（仅保留高价值）
1. 首页 recent 预览增加来源信息（manual/recent）轻徽标。
2. 收藏预览接入“最近收藏时间”并按时间排序展示。
3. 抽离首页预览行小组件，降低 `home_page.cpp` 组装复杂度。
4. Hero 区可补一条“最近一次搜索关键词”弱提示。
5. 激活状态接入真实服务后，将页脚摘要从占位升级为真实状态。 
