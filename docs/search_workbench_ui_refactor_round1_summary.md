# Search Workbench UI Refactor Round 1 Summary

## 1. 文档信息
- 日期：2026-04-20
- 范围：`SearchPage` 搜索工作台 UI 重构（仅 UI 层）
- 目标：在不改搜索功能、不改筛选逻辑、不改详情渲染内核的前提下，提升页面结构、主次关系与整体观感

## 2. 本轮改动范围
- 修改文件：
  - `src/ui/pages/search_page.h`
  - `src/ui/pages/search_page.cpp`
  - `src/ui/style/app.qss`
- 未改文件：
  - `src/ui/detail/detail_pane.h/.cpp`（本轮未修改）
  - `src/ui/main_window.h/.cpp`（本轮未修改）
  - `resources/detail/*`（WebEngine 内部模板/脚本未修改）

## 3. 结构性改造摘要
- 页面重构为「顶部搜索主操作区 + 中下分栏工作台」。
- 左栏重组为「筛选与排序面板 + 搜索结果面板」。
- 右栏重组为「详情壳层 + 头部状态区 + 内容承托区」。
- 新增 UI 状态承托：
  - 结果区空状态（初始态/无结果态）
  - 详情区头部状态文案（等待/加载/成功/失败/兼容模式）
  - 详情耗时标签的样式状态化（idle/loading/success/failed）

## 4. 文件级变更要点

### 4.1 `search_page.h`
- 新增 UI 辅助方法声明：
  - `updateResultEmptyState(...)`
  - `updateDetailShellMeta(...)`
- 新增 UI 成员：
  - `detailMetaLabel_`
  - `resultEmptyState_`
  - `resultEmptyTitleLabel_`
  - `resultEmptyDescriptionLabel_`

### 4.2 `search_page.cpp`
- UI 结构重构集中在 `buildUi()`，核心逻辑函数未改语义：
  - 搜索触发链路仍为 `runSearch(...)`
  - 筛选链路仍由原有 `ComboBox` 信号驱动
  - 详情仍经 `DetailPane` + `WebEngine` 渲染
- 将结果区状态显示统一到：
  - `updateStatusLine(...)`（已有）
  - `updateResultEmptyState(...)`（新增）
- 将详情区辅助信息统一到：
  - `updateDetailShellMeta(...)`（新增）
  - `updateDetailTimingLabel(...)`（改为 property 驱动样式，不再内联 setStyleSheet）
- 优化结果列表项可扫读性（仅文案排布与尺寸，不改数据来源）：
  - 标题
  - module/category/difficulty
  - tags/score

### 4.3 `app.qss`
- 新增 SearchWorkbench 专属样式块（仅 `objectName` 精准命中）：
  - `searchPage`
  - `searchTopBar`
  - `searchInput`
  - `searchButton`
  - `searchFilterPanel`
  - `searchResultsPanel`
  - `resultList`
  - `detailShell`
  - `detailShellHeader`
  - `detailShellMeta`
  - `detailPerfLabel`
  - `detailShellBody`
- 增加结果项 hover/selected、空状态承托、详情状态色、滚动条统一样式。

## 5. 第四部分：设计说明（重点）

### 5.1 为什么这样定义“搜索工作台”
- 搜索页是核心主路径，不是普通单页内容展示页。
- 采用「输入 -> 筛选 -> 扫结果 -> 看详情」的工作流组织，减少视线跳转成本。
- 通过顶部主搜索区突出入口，把筛选区降为辅助，把右侧详情作为稳定阅读承托区。

### 5.2 为什么详情区这轮只改外层
- 详情区内核依赖 WebEngine + HTML/JS/WebChannel，属于高风险链路。
- 本轮仅重构外层容器、状态区、空状态和耗时信息位置，避免触碰详情渲染契约。
- 这样可以在明显提升观感和结构层次的同时，把功能回归风险降到最低。

### 5.3 哪些地方提升了核心搜索效率
- 搜索输入成为首屏主视觉入口，启动动作更明确。
- 筛选区视觉降噪，减少“表单墙”感。
- 结果列表项信息分层更清晰，扫读速度更快。
- 无结果/初始态提示更正式，减少“空白等待”。
- 详情头部状态可感知，用户能快速判断“等待中/加载中/已就绪/异常”。

### 5.4 哪些地方提升了观感
- 左右栏容器化统一，避免“两个时代 UI 拼接感”。
- 卡片边界、圆角、留白、层级语言与近期页面保持一致。
- 结果区与详情区形成明确主从关系，页面更有产品气质而非工具堆叠感。

### 5.5 哪些地方降低了功能改动风险
- 未改 `SearchService` / `SuggestService` 调用契约。
- 未改筛选组合条件生成逻辑。
- 未改 `DetailPane` 渲染流程、模板契约、公式渲染链路。
- 未改 Repository / 数据模型 / 存储结构。

## 6. 第五部分：后续可选优化（Top 5）
1. 将结果项升级为自定义 item widget 或 delegate，进一步增强标题/标签/难度的视觉层次。
2. 筛选区增加折叠与摘要条，提升小窗口下的空间效率。
3. 搜索框增加快捷键提示与轻交互反馈（如 Enter/ESC 的显式提示）。
4. 详情头部补充当前结论标题与轻量上下文信息（仅壳层，不改 WebEngine 内核）。
5. 增加结果统计摘要增强（按模块/分类分布），用于快速判断检索面。

## 7. 验证与回归
- 构建验证：
  - `cmake --build out/build/msvc-debug --config Debug` 已通过。
- 风险控制结论：
  - 本轮改动集中在 UI 结构和 qss 样式。
  - 搜索/筛选/详情渲染核心链路保持不变。

