# 最近搜索页 UI 重构（第一轮）修改总结

更新时间：2026-04-20

## 1. 本轮目标与边界

本轮聚焦“最近搜索”页面的第一轮 UI 重构，目标是提升页面成品感、扫读效率和交互清晰度，在不推翻现有工程结构的前提下完成落地。

严格边界：

- 重点改造：`RecentSearchesPage`
- 新增单条组件：`RecentSearchItemWidget`
- 样式统一：`app.qss`
- 仅做最小必要接线：`MainWindow`、`HistoryRepository`、`CMakeLists.txt`
- 明确未扩散：未改造 `FavoritesPage`、搜索主流程结构、详情页与底层存储格式

## 2. 主要改动概览

### 2.1 页面层（RecentSearchesPage）

文件：

- `src/ui/pages/recent_searches_page.h`
- `src/ui/pages/recent_searches_page.cpp`

核心重构点：

- 页面结构改为：`Header + ScrollArea + EmptyState`
- 新增页面职责方法：
  - `reloadData()`
  - `rebuildList()`
  - `clearListItems()`
  - `updateEmptyState()`
  - `handleClearAll()`
  - `handleItemDelete(...)`
  - `handleSearchAgain(...)`
  - `formatRelativeTime(...)`
- 接入 `HistoryRepository` 数据读取与操作：
  - 列表读取
  - 单条删除
  - 清空历史
  - 再次搜索时刷新历史顺序
- 空状态实现：
  - 标题：还没有最近搜索
  - 说明：你搜索过的内容会出现在这里，方便快速回访
  - 按钮：去搜索
- 时间显示规则实现：
  - 今天 `HH:mm`
  - 昨天 `HH:mm`
  - 更早 `yyyy-MM-dd HH:mm`

### 2.2 单条记录组件（RecentSearchItemWidget）

文件（新增）：

- `src/ui/widgets/recent_search_item_widget.h`
- `src/ui/widgets/recent_search_item_widget.cpp`

组件能力：

- 信息区：搜索词 / 时间 / 轻量 meta（来源等）
- 操作区：`再次搜索`（主操作）+ `删除`（弱化次操作）
- 交互状态：
  - `hovered`
  - `pressed`
  - `focused`
- 主区域点击可触发再次搜索（避开按钮命中区域）
- 长文本省略显示（`elidedText`）
- 键盘可用性：`Enter/Space` 可触发再次搜索

### 2.3 样式统一（app.qss）

文件（新增）：

- `src/ui/style/app.qss`

覆盖对象：

- `RecentSearchPage` 页面容器
- header：标题、说明、统计、清空按钮
- 列表滚动区与滚动条
- item 容器与文字层级
- 主次按钮状态（hover/pressed/focus/disabled）
- 空状态卡片与按钮

设计参数（本轮落地）：

- 页面边距：24
- header 与列表间距：22
- item 间距：12
- item 内边距：18 x 16
- item 圆角：15
- 按钮圆角：11
- 主色仅用于主操作与焦点态，删除按钮默认弱化

### 2.4 最小必要接线

#### MainWindow

文件：

- `src/ui/main_window.h`
- `src/ui/main_window.cpp`

改动点：

- 持有 `RecentSearchesPage* recentSearchesPage_`
- 接收最近搜索页信号并接入现有搜索流程：
  - `searchRequested(query)` -> 切换到搜索页并调用 `triggerSearchFromRecent(query)`
  - `navigateToSearchRequested()` -> 切换到搜索页
- 切换到“最近搜索”页时主动 `reloadData()`
- 更新顶部副标题文案

#### HistoryRepository（最小补充）

文件：

- `src/domain/repositories/history_repository.h`
- `src/domain/repositories/history_repository.cpp`

新增接口：

- `bool removeQuery(const QString& query);`

说明：

- 仅补齐“删除单条历史”能力
- 不修改存储结构，不扩展 schema，不改现有 load/save 机制

#### CMake

文件：

- `CMakeLists.txt`

改动：

- 新增 `recent_search_item_widget.h/.cpp` 编译项

## 3. 样式加载策略说明

当前实现在 `RecentSearchesPage` 内通过 `locateAppStylePath()` + `applyAppStyleSheetOnce()` 尝试加载 `app.qss`，并通过 `qApp` 属性避免重复加载。

当前行为：

- 优先查找项目根 `app.qss`
- 找不到时查找 `src/ui/style/app.qss`
- 找到后追加到应用级样式表

后续建议：

- 下一轮可将样式加载迁移到 `main.cpp` 或统一 UI 启动入口，减少页面对全局样式装配的职责耦合

## 4. 验证结果

构建验证：

- `cmake --build out/build/msvc-debug --config Debug` 通过

测试验证：

- `ctest -C Debug --output-on-failure` 通过（4/4）
  - `search_service_tests`
  - `suggest_service_tests`
  - `detail_perf_aggregator_tests`
  - `storage_repository_tests`

## 5. 已知取舍与注意事项

- 这轮未引入复杂 Model/View + Delegate，优先可维护性和最小侵入
- 单条删除基于 query 归一化键，符合当前历史“按 query 去重”的仓储语义
- `app.qss` 目前聚焦最近搜索页样式，后续扩展页面时需防止全局样式冲突

## 6. 后续重构建议（按优先级）

1. 将 `app.qss` 统一加载迁移到应用启动入口
2. 抽取通用空状态组件（Recent/Favorites 复用）
3. 最近搜索按“今天/近7天/更早”分组
4. 增加键盘上下导航与当前项高亮
5. 为最近搜索补充交互测试（删除、清空、再次搜索、空态跳转）

## 7. 本轮变更文件清单

- `src/ui/pages/recent_searches_page.h`
- `src/ui/pages/recent_searches_page.cpp`
- `src/ui/widgets/recent_search_item_widget.h`（新增）
- `src/ui/widgets/recent_search_item_widget.cpp`（新增）
- `src/ui/style/app.qss`（新增）
- `src/domain/repositories/history_repository.h`
- `src/domain/repositories/history_repository.cpp`
- `src/ui/main_window.h`
- `src/ui/main_window.cpp`
- `CMakeLists.txt`
