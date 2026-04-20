# 收藏页 UI 重构（第二轮）总结

更新时间：2026-04-20

## 第一部分：仓库理解

### 1. 收藏页现状识别
- 已存在收藏页文件：
  - `src/ui/pages/favorites_page.h`
  - `src/ui/pages/favorites_page.cpp`
- 改造前为占位实现（标题 + 输入框 + 列表占位 + 空状态占位），未接入真实收藏数据与详情流程。

### 2. 收藏数据接口识别
- 已存在仓储接口：
  - `src/domain/repositories/favorites_repository.h`
  - `src/domain/repositories/favorites_repository.cpp`
- 当前核心能力：
  - `load/save`
  - `add/remove/toggle/contains`
  - `allIds/count/clear`
- 当前存储语义以 `conclusionId` 列表为主（`ids`），并兼容读取 `items[].id`。
- 本轮遵循“最小侵入”原则：未扩写 `FavoritesRepository` 契约。

### 3. 详情接线点识别
- 主接线点在：
  - `src/ui/main_window.h/.cpp`
  - `src/ui/pages/search_page.h/.cpp`
- 原先仅有“最近搜索 -> 搜索页”流程；
- 收藏页缺少“按 conclusionId 直接打开详情”的接线能力。

### 4. 本轮最小侵入改造策略
- 只聚焦以下范围：
  - `FavoritesPage`
  - `FavoriteItemCard`（新增组件）
  - `app.qss`
- 仅做必要接线：
  - `MainWindow` 增加收藏页信号连接
  - `SearchPage` 增加 `openConclusionById(...)`
- 不扩散重构：
  - 不改最近搜索页结构
  - 不改底层存储格式
  - 不推翻导航与主框架

---

## 第二部分：文件改动计划

### 1. 修改文件
- `src/ui/pages/favorites_page.h`
- `src/ui/pages/favorites_page.cpp`
- `src/ui/style/app.qss`
- `src/ui/main_window.h`
- `src/ui/main_window.cpp`
- `src/ui/pages/search_page.h`
- `src/ui/pages/search_page.cpp`
- `CMakeLists.txt`

### 2. 新增文件
- `src/ui/widgets/favorites/favorite_item_card.h`
- `src/ui/widgets/favorites/favorite_item_card.cpp`

### 3. 职责拆分原则
- `FavoritesPage`：
  - 页面组装
  - 数据加载/排序/刷新
  - 空状态切换
  - 与主流程信号交互
- `FavoriteItemCard`：
  - 单卡展示层级
  - 单卡交互状态（hover/pressed/focus）
  - “打开详情 / 取消收藏”局部行为发射
- `app.qss`：
  - 统一视觉风格
  - 避免在 cpp 中散落样式字符串

---

## 第三部分：完整代码实现（落地结果）

> 本节按“最终落地文件”给出实现结果索引与关键实现点，便于后续升级时快速定位。

### A. FavoritesPage（重构）
- 文件：
  - `src/ui/pages/favorites_page.h`
  - `src/ui/pages/favorites_page.cpp`

关键实现：
- 页面结构升级为：
  - Header（标题 + 说明）
  - 轻量工具条（统计 + 排序 + 筛选骨架按钮）
  - `QScrollArea` 卡片区
  - 空状态区（标题/说明/去搜索）
- 页面行为：
  - `reloadData()`
  - `applySort()`
  - `rebuildCards()`
  - `clearCards()`
  - `updateEmptyState()`
- 数据映射优先级（按实际仓库字段）：
  - `conclusionId`：来自 `FavoritesRepository::allIds()`
  - `title`：优先 `content.meta.title`，退化 `index.doc.title`
  - `module`：优先 `content.identity.module`，退化 `index.doc.module`
  - `tags`：优先 `content.meta.tags`，退化 `index.doc.tags`
  - `difficulty`：优先 `content.meta.difficulty`，退化 `index.doc.difficulty`
  - `summary`：在 `statement` / `summary` 候选里做列表友好选择
  - 收藏时间：优先 `favoritedAt`，退化 `createdAt/updatedAt`（若缺失则隐藏）
- 排序能力：
  - 最近收藏
  - 标题
  - 模块

### B. FavoriteItemCard（新增）
- 文件：
  - `src/ui/widgets/favorites/favorite_item_card.h`
  - `src/ui/widgets/favorites/favorite_item_card.cpp`

关键实现：
- 严格信息层级：
  - 第一层：`title`
  - 第二层：`module + difficulty + tags`
  - 第三层：`summary`（最多 2 行）
  - 第四层：`favoritedAt`（弱化）
  - 第五层：操作区（打开详情 / 取消收藏）
- 交互态：
  - hover / pressed / focused
  - 整卡点击可打开详情（避免误触操作按钮）
  - 键盘 `Enter/Space` 支持打开详情
- 文本处理：
  - 标题单行优雅省略
  - 摘要两行省略
  - 字段缺失时自动隐藏对应区域，不造假

### C. app.qss（视觉统一）
- 文件：
  - `src/ui/style/app.qss`

关键实现：
- 新增 Favorites 专属样式域：
  - `QWidget#FavoritesPage`
  - Header / Toolbar / ScrollArea / EmptyState
  - `FavoriteItemCard` 及内部标题、摘要、时间、chips、按钮
- 状态覆盖：
  - hover / pressed / focus / disabled
- 视觉目标落地：
  - 背景浅灰白
  - 卡片白底轻边框
  - 主操作按钮适度强调
  - 弱操作默认克制，hover 才凸显

### D. 最小接线修改
- `src/ui/main_window.h/.cpp`
  - 新增 `FavoritesPage* favoritesPage_`
  - 收藏页信号接线：
    - `openConclusionRequested(conclusionId)` -> 切换到搜索页并打开详情
    - `navigateToSearchRequested()` -> 切换到搜索页
  - 页面切换到收藏页时自动 `reloadData()`
- `src/ui/pages/search_page.h/.cpp`
  - 新增 `openConclusionById(const QString&)`
  - 按 `conclusionId` 直达详情渲染链路
- `CMakeLists.txt`
  - 新增 `favorite_item_card.h/.cpp` 编译项

### E. 验证结果
- 构建：
  - `cmake --build out/build/msvc-debug --config Debug` 通过
- 测试：
  - `ctest -C Debug --output-on-failure` 通过（4/4）

---

## 第四部分：设计说明（重点）

### 1. 为什么收藏页要“更稳、更内容化”
- 收藏页是“知识资产沉淀页”，不是“操作流水页”。
- 目标不是让用户快速点按钮，而是先读到有价值的结论内容，再决定是否进入详情或取消收藏。
- 因此视觉重心必须从“按钮优先”转向“标题 + 摘要优先”。

### 2. 页面布局决策
- 采用 `Header -> 工具条 -> 内容滚动区 -> 空状态`，原因：
  - Header 建立明确页面定位（我的收藏 / 复习资产）
  - 工具条承担轻操作（排序）和状态反馈（总数）
  - 中部滚动区只承载内容卡片，避免注意力干扰
  - 空状态作为正式页面组成，而非临时占位

### 3. 卡片分层决策
- 第一层（标题）最显眼：
  - 用户 1 秒内识别结论对象
- 第二层（模块/难度/标签）降级呈现：
  - 提供结构定位，不喧宾夺主
- 第三层（摘要）承接内容感：
  - 提升“读一眼就知道是什么”的效率
- 第四层（收藏时间）弱化：
  - 仅做辅助，不抢阅读通道
- 第五层（操作）存在但不主导：
  - `打开详情` 清晰可见
  - `取消收藏` 默认弱化，hover 强化

### 4. 与最近搜索页的差异化
- 最近搜索页：轻、时间流、快速回访。
- 收藏页：稳、内容承载、知识资产感。
- 本轮通过以下方式避免同质化：
  - 收藏页标题更稳重
  - 卡片内容层级更完整（尤其摘要层）
  - 工具条样式更克制
  - 操作按钮视觉权重显著下降

### 5. 观感与效率的平衡点
- 观感提升：
  - 留白、边界感、弱对比层级
  - 统一圆角和浅边框，减少“控件拼接感”
- 扫读效率提升：
  - 标题和摘要优先级清晰
  - 排序快速切换
  - 字段缺失自动收敛显示，减少噪声
- 工程可维护性：
  - 样式集中 `app.qss`
  - 页面/卡片职责分离
  - 接线改动可控且可回溯

---

## 第五部分：后续可选优化（Top 5）

1. 收藏按模块分组（组头 + 折叠）  
2. 筛选按钮接入正式筛选面板（模块/标签/难度）  
3. 卡片摘要支持关键词高亮（结合搜索上下文）  
4. 收藏卡片增强键盘导航（上下移动、回车打开）  
5. 抽取统一 EmptyState 组件（Recent/Favorites 共用骨架，保留差异化主题）

---

## 附录：本轮变更文件清单

- `CMakeLists.txt`
- `src/ui/pages/favorites_page.h`
- `src/ui/pages/favorites_page.cpp`
- `src/ui/widgets/favorites/favorite_item_card.h`（新增）
- `src/ui/widgets/favorites/favorite_item_card.cpp`（新增）
- `src/ui/style/app.qss`
- `src/ui/main_window.h`
- `src/ui/main_window.cpp`
- `src/ui/pages/search_page.h`
- `src/ui/pages/search_page.cpp`

