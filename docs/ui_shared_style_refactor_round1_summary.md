# UI 共用样式统一提炼（Round 1）总结

更新时间：2026-04-20  
适用版本：当前 `main` 工作区（本轮未改业务数据结构）

## 1. 文档目的

这份文档用于沉淀本轮“最近搜索页 + 我的收藏页”共用样式统一提炼结果，方便后续主页、搜索页、设置页、激活页重构时直接复用，并保证可追溯。

## 2. 本轮范围与边界

### 已做
- 提炼并落地基础视觉令牌（spacing / radius / typography）。
- 统一 Page Header、Card、按钮层级、Empty State、Tag/Badge/MetaText 的样式协议。
- 统一 objectName 挂钩命名，减少页面间样式割裂。
- 保留 recent/favorites 职责差异，不做强制同质化。
- 为后续页面预留复用入口：`futureHomePage`、`futureSearchPage`。

### 明确没做
- 没有引入复杂主题系统/动态换肤。
- 没有重写页面结构和业务流程。
- 没有改 Repository / MainWindow 业务行为。
- 没有新增重型组件库（仅轻量样式层提炼）。

## 3. 设计原则（本轮执行口径）

1. 只提炼在 recent/favorites 已验证有效的样式语言。  
2. 差异因职责而存在时保留差异。  
3. 优先统一基础视觉协议，而不是抽象未来假设。  
4. 最小侵入，不伤害已有页面观感。  
5. 给下一轮主页/搜索页重构留可复用命名接口。

## 4. 关键提炼结果

## 4.1 统一样式入口与工具

新增：
- `src/ui/style/app_style.h`
- `src/ui/style/app_style.cpp`

职责：
- 视觉令牌常量（边距、间距、圆角、字号等）。
- 全局 QSS 一次加载（替代原先散落在页面内的样式加载逻辑）。
- 公共文本格式化：
  - `formatRelativeDateTime(...)`
  - `formatDifficultyText(...)`

## 4.2 QSS 重构为“基础层 + 差异层”

修改：
- `src/ui/style/app.qss`

结构变化：
- 基础层：页面基底、header、card、button、empty state、tag/badge/meta、scrollbar。
- 差异层：仅保留 recent/favorites 必要视觉差异（例如收藏页标题更稳重、工具条存在等）。

## 4.3 命名协议统一（objectName / property）

### 页面根容器
- `recentSearchPage`
- `favoritesPage`
- `futureHomePage`
- `futureSearchPage`

### Header
- `pageHeader`
- `pageTitleLabel`
- `pageSubtitleLabel`
- `pageSummaryLabel`

### Card
- `elevatedCard`
- `listItemCard`
- `contentCard`

### Text
- `cardTitleLabel`
- `cardMetaLabel`
- `cardSummaryLabel`
- `subtleTextLabel`

### Button
- `primaryButton`
- `secondaryButton`
- `weakDangerButton`
- `toolbarButton`
- `emptyStatePrimaryButton`

### Tag/Badge
- `tagChip`
- `difficultyBadge`
- `metaBadge`

### Empty State
- `emptyState`
- `emptyStateTitle`
- `emptyStateDescription`

## 4.4 视觉令牌（已固化）

来自 `app_style.h`：
- 页面外边距：`24`
- 区块间距：`22`
- 小间距：`8`
- 卡片间距：`12`
- 卡片内边距：`18/16`
- 空状态卡片内边距：`30/28`
- 卡片圆角：`16`
- 按钮圆角：`11`
- badge/chip 圆角：`9`
- 标题/正文/次级文本字号分层常量

## 5. 保留的页面差异（刻意不统一）

1. 收藏页保留工具条（排序/筛选占位）与更稳重标题层级。  
2. 最近搜索页保留“轻快回访”感（结构更直接，操作更利落）。  
3. Empty State 卡片宽度保留差异（recent/favorites）。

这些差异是职责差异，不是风格不一致。

## 6. 文件变更清单（可追溯）

### 新增文件
- `src/ui/style/app_style.h`
- `src/ui/style/app_style.cpp`
- `docs/ui_shared_style_refactor_round1_summary.md`（本文档）

### 修改文件
- `CMakeLists.txt`
- `src/ui/style/app.qss`
- `src/ui/pages/recent_searches_page.h`
- `src/ui/pages/recent_searches_page.cpp`
- `src/ui/pages/favorites_page.h`
- `src/ui/pages/favorites_page.cpp`
- `src/ui/widgets/recent_search_item_widget.cpp`
- `src/ui/widgets/favorites/favorite_item_card.cpp`
- `src/ui/pages/home_page.cpp`
- `src/ui/pages/search_page.cpp`

## 7. 行为与风险说明

- 业务逻辑未做结构性调整，主要是样式挂钩与公共格式化函数替换。
- 风险主要在 UI 呈现层（QSS 选择器命名变更），已通过编译和测试验证。

## 8. 验证结果

在 2026-04-20 本地执行：

1. `cmake --build out/build/msvc-debug --config Debug`：通过  
2. `ctest -C Debug --output-on-failure --test-dir out/build/msvc-debug`：4/4 通过

## 9. 后续接入建议（直接用于下一轮）

1. 主页重构时直接挂 `futureHomePage` 并复用 `pageHeader`/按钮层级。  
2. 搜索页重构时直接挂 `futureSearchPage` 并复用 card/button/empty state 协议。  
3. 若后续要抽组件，优先顺序：`PageHeaderWidget` -> `EmptyStateWidget` -> `CardFrame`。  
4. 新页面优先使用统一 objectName，避免再回到页面私有命名。  
5. 新样式优先加在 `app.qss` 基础层，只有职责差异才加页面差异覆盖。

## 10. 快速回滚参考

若需局部回滚本轮样式提炼，可按粒度回退：
- 仅回退 QSS：`src/ui/style/app.qss`
- 回退公共样式入口：`src/ui/style/app_style.*`
- 回退页面挂钩：`recent_searches_page.cpp` / `favorites_page.cpp`
- 回退卡片挂钩：`recent_search_item_widget.cpp` / `favorite_item_card.cpp`

建议不要混合回滚（例如只回退命名不回退 QSS），以免选择器失配。
