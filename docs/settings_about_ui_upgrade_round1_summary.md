# 设置/关于页 UI 升级重构总结（Round 1）

> 本文档用于沉淀本轮“设置/关于页”升级改造结果，重点记录第二、第三、第四部分，便于后续迭代与追溯。

## 第二部分：文件改动计划（已实施）

### 1) `src/ui/pages/settings_page.h`
- 调整 `SettingsPage` 构造函数，注入：
  - `ConclusionIndexRepository*`
  - `ConclusionContentRepository*`
  - `indexLoaded / contentLoaded` 状态
- 新增 `reloadData()`，用于页面级数据刷新。
- 新增结构化组装方法：
  - `setupUi()`
  - `setupHeader()`
  - `setupSections()`
  - `buildSoftwareInfoSection()`
  - `buildDataInfoSection()`
  - `buildHelpSection()`
  - `buildFeedbackSection()`
  - `buildExpansionHintSection()`
  - `createSection()`
  - `createInfoRow()`
- 目的：
  - 让设置页从“单体占位实现”升级为“可扩展的分组式页面骨架”。
  - 保持 UI 组装、数据展示、状态刷新职责清晰。

### 2) `src/ui/pages/settings_page.cpp`
- 完整重构页面结构：
  - 顶部 Header（标题 + 副标题 + meta）
  - 中部 `QScrollArea`
  - 滚动内容区 `settingsContent`
  - 四个正式分组 + 一个轻量扩展预留分组
- 四大分组：
  - 软件信息
  - 数据信息
  - 使用帮助
  - 联系与反馈
- 展示方式从“段落说明”升级为“信息行（标签 + 值）”：
  - 统一行容器 `settingsInfoRow`
  - 标签 `settingsInfoLabel`
  - 值 `settingsInfoValue`
- 接入现有真实数据来源（不改业务逻辑）：
  - 应用名 / 版本（`UiConstants`）
  - 数据目录（`AppPaths`）
  - content/index 记录数、模块数、源文件路径（repository）
  - 加载异常提示（diagnostics）
- 轻操作入口（均为真实可执行，不伪造功能）：
  - 打开数据目录
  - 打开 README（不存在则打开 docs 目录）
  - 复制反馈邮箱

### 3) `src/ui/main_window.h` / `src/ui/main_window.cpp`（最小路由微调）
- 新增 `SettingsPage* settingsPage_` 成员，避免匿名临时创建，支持显式刷新。
- `setupPages()` 中改为构造：
  - `new SettingsPage(&indexRepository_, &contentRepository_, indexLoaded_, contentLoaded_, pageStack_)`
- 页面切换到 `kPageSettings` 时触发 `settingsPage_->reloadData()`，保持展示与当前运行状态一致。
- 设置页顶部副标题更新为正式描述（去除“占位”语义）。

### 4) `src/ui/style/app.qss`
- 仅新增“设置页相关”样式块，不改全局逻辑：
  - `settingsPage`
  - `settingsPageHeader`
  - `settingsPageTitle`
  - `settingsPageSubtitle`
  - `settingsScrollArea`
  - `settingsContent`
  - `settingsGroup`
  - `settingsGroupTitle`
  - `settingsInfoRow`
  - `settingsInfoLabel`
  - `settingsInfoValue`
  - `settingsHintText`
  - `settingsMetaText`
  - `settingsSecondaryButton`
  - `settingsLinkButton`
- 使用 `sectionRole` 做“明显但克制”的层级差异：
  - software / data / help / feedback / future
- 视觉语言保持与现有页面统一：
  - 浅背景、轻边框、柔和圆角、安静层次。

## 第三部分：设计说明

### 1) 为什么这样分组
- 设置/关于页核心目标是“信息组织清晰”，不是“功能控件堆叠”。
- 将信息稳定拆成四组（软件信息、数据信息、使用帮助、联系反馈）后：
  - 用户能快速定位信息类别。
  - 后续新增真实设置项时可自然落到对应分组。

### 2) 为什么使用滚动内容区
- 设置页是天然会增长的页面（后续会有目录切换、主题、快捷键、日志等）。
- `QScrollArea` 可保证：
  - 当前版本内容不过载；
  - 后续扩展不需要推倒布局结构；
  - 小窗口下仍具可读性和可达性。

### 3) 为什么采用结构化信息行
- 旧方案是“说明文字块”，可扫读性和正式感都不足。
- 新方案采用“标签 + 值”行结构：
  - 对齐关系清楚，便于快速浏览；
  - 长路径、文件地址等值可换行且可选中；
  - 支撑未来演进为“只读信息行 / 可编辑设置行”的统一体系。

### 4) 本轮产品感提升点
- 页面头部层级明确（标题/副标题/meta）。
- 分组卡片化、边界克制、间距一致。
- 信息行统一风格，弱化“临时占位文本感”。
- 帮助与反馈区提供真实入口动作（打开、复制），避免纯说明堆叠。
- “扩展预留”采用轻提示，不伪造未实现控件。

### 5) 为后续扩展保留的空间
- 已预留可扩展分组和行级结构位，可平滑接入：
  - 数据目录切换
  - 主题/缩放
  - 快捷键说明
  - 日志/调试入口
  - 激活状态摘要
- 不需要再重构页面骨架，只需补充真实行项与行为。

## 第四部分：后续可选优化（优先 5 项）

1. 接入真实“数据目录切换”，并增加切换后的重载校验与状态回显。  
2. 接入“快捷键帮助”结构化展示（按页面分组，含冲突提示）。  
3. 接入“日志与诊断入口”（打开日志目录、复制诊断摘要）。  
4. 接入“激活状态摘要卡”（只读展示，不改激活页业务流程）。  
5. 接入“主题/字体缩放”真实设置项并复用现有设置持久化契约。  

---

## 备注
- 本轮严格聚焦设置/关于页 UI 升级，未改搜索页、详情页、最近搜索页、收藏页、激活页业务逻辑。
- 本轮保持最小侵入：主要变更集中在页面组装、展示结构与样式补充。
