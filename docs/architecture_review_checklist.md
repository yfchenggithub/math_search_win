# 架构回归清单

> 用途：每次迭代后快速确认“真实代码实现”是否仍与系统架构一致。  
> 范围：仅面向当前仓库（`math_search_win`）的 Qt6 本地离线桌面应用。

---

## 1. 使用说明

- 勾选规则：
  - `[x]` 已确认
  - `[ ]` 未确认/有风险
  - `N/A` 本次改动不涉及
- 执行时机：
  - 开发完成后
  - 提交前（或合并前）
- 最低要求：
  - 变更涉及到的模块条目必须全勾选
  - 未勾选项必须写明原因和后续动作

---

## 2. 变更元信息

- 迭代日期：
- 变更分支：
- 主要改动文件：
- 负责人：
- 是否涉及架构边界调整（是/否）：

---

## 3. 启动与装配回归（`main` / `MainWindow`）

- [ ] `src/main.cpp` 启动顺序仍可解释（Logger -> Probe -> MainWindow）。
- [ ] `--probe-only` 行为未被破坏。
- [ ] `src/ui/main_window.cpp` 中 `licenseService_.initialize()` 与 `featureGate_` 状态同步仍成立。
- [ ] `loadSearchData()` 仍在页面初始化前执行。
- [ ] `setupPages()` 后页面索引与 `UiConstants::kPage*` 一致。
- [ ] 页面切换入口仍统一经 `switchPageWithTrigger()`。

---

## 4. 页面装配与导航回归

- [ ] 页面清单未漂移：Home/Search/Favorites/Recent/Settings/Activation。
- [ ] `MainWindow::setupPages()` 的跨页信号仍闭环（home/recent/favorites/search 联动）。
- [ ] 页面 `reloadData()` 触发策略未引入重复刷新或漏刷新。
- [ ] 新增页面若存在，已补充到 `UiConstants` 和主窗口装配中。

---

## 5. 搜索链路回归

- [ ] `SearchPage::runSearch()` 入口触发（button/return/suggest/filter）行为符合预期。
- [ ] `SearchService::search()` 的过滤、打分、排序未出现回归。
- [ ] `ConclusionIndexRepository` 的 `findTerm/findPrefix/getDocById` 依赖关系未被绕过。
- [ ] trial/full 功能门控（`FeatureGate`）对搜索结果数量限制仍生效。
- [ ] 结果列表渲染后首项自动选中逻辑仍正常（触发详情流程）。

---

## 6. Suggest 链路回归

- [ ] `onQueryTextChanged()` -> `runSuggest()` 路径仍成立。
- [ ] `SuggestService::suggest()` 仍基于 `prefixIndex/termIndex`。
- [ ] 建议点击后 `runSearch(..., "suggest_click")` 仍触发。
- [ ] Suggest 去重、排序、过滤逻辑未被破坏。

---

## 7. 详情渲染链路回归（Web + Fallback）

- [ ] `onResultSelectionChanged()` -> `enqueueDetailRenderRequest()` -> `renderDetailForRequest()` 链路通。
- [ ] `DetailRenderCoordinator` 的 stale request 保护仍有效。
- [ ] `DetailViewDataMapper` 输出 payload 与 `resources/detail/detail.js` 接口仍匹配。
- [ ] Web 模式资源检查仍通过：`resources/detail/*`、`resources/katex/*`。
- [ ] Web 失败时 `activateTextFallbackMode()` -> `QTextBrowser` 回退仍可用。
- [ ] trial 模式 `showTrialDetailPreview()` 行为未失效。
- [ ] 详情性能日志（`DetailPerfAggregator` + JS `[perf][detail]`）仍可观测。

---

## 8. 收藏链路回归

- [ ] `SearchPage::onFavoriteButtonClicked()` 增删收藏正常。
- [ ] `FavoritesRepository` 仍落盘到 `cache/favorites.json`。
- [ ] `favoritesChanged` 信号后 `FavoritesPage`、`HomePage` 能同步刷新。
- [ ] `FavoritesPage::openConclusionRequested` 回跳搜索详情仍可用。
- [ ] 若本次涉及收藏 schema，已验证向后兼容（`ids`/`items`）。

---

## 9. 历史记录链路回归

- [ ] 历史写入触发条件仍准确（`button/return/suggest_click`）。
- [ ] `HistoryRepository` 去重与容量限制行为未变坏。
- [ ] `cache/history.json` 可正确写入与恢复。
- [ ] `RecentSearchesPage` 的重搜/删除/清空动作仍正确同步。

---

## 10. 设置持久化链路回归

- [ ] 明确本次是否改了设置链路（`SettingsRepository` / `SettingsPage`）。
- [ ] 若接线了设置项：已验证 `load -> setValue -> save -> reload` 闭环。
- [ ] 若未接线：文档中仍明确“SettingsPage 以状态展示为主”。
- [ ] `cache/settings.json` 缺失/损坏时回退策略仍可用。

---

## 11. 激活/授权链路回归

- [ ] `ActivationPage::onActivateClicked()` 解析/校验/写入/reload 流程正常。
- [ ] `LicenseService::reload()` 对 missing/read/parse/invalid/valid_full 状态分支正常。
- [ ] `FeatureGate::setLicenseState()` 后 UI 门控行为与授权状态一致。
- [ ] `license/license.dat` 路径与格式未被破坏（`msw-license-v1`）。
- [ ] 若改动安全逻辑，已明确是否仍存在 TODO stub（签名/解密/到期策略）。

---

## 12. 数据与资源边界回归

- [ ] 内容与索引边界未混淆：
  - 索引：`data/backend_search_index.json`
  - 内容：`data/canonical_content_v2.json`
- [ ] `AppPaths` 目录解析逻辑未引入路径漂移风险。
- [ ] 新增运行时依赖（JSON/模板/静态资源）已放在正确目录并可被加载。
- [ ] 构建产物与源码资源边界仍清晰（不把运行时数据写入 `src/`）。

---

## 13. 日志与调试入口回归

- [ ] 关键日志分类仍可用：`search.engine`、`detail.render`、`perf.*`、`file.io`、`config`。
- [ ] 新增关键分支有对应日志，且不泄漏敏感信息。
- [ ] 推荐断点入口仍有效：
  - `main`
  - `MainWindow::loadSearchData`
  - `SearchPage::runSearch`
  - `SearchPage::renderDetailForRequest`
  - `LocalStorageService::writeJsonFileAtomically`
  - `ActivationPage::onActivateClicked`

---

## 14. 测试与文档同步回归

- [ ] 受影响模块已映射到 `docs/test_matrix.md` 的“模块 -> CTest 目标”清单（`search/suggest/storage/activation/detail/ui`）。
- [ ] 受影响模块在 `tests/search` / `tests/suggest` / `tests/storage` / `tests/activation` / `tests/ui` / `tests/detail` 中有对应自动化测试，且至少执行了最小必跑集合。
- [ ] 若受影响模块暂无自动化测试：已补充人工冒烟步骤，并在 `docs/test_matrix.md` 记录风险与补测计划。
- [ ] 测试执行结果可追踪（命令、时间、通过/失败结论）。
- [ ] 四份架构文档是否需要同步更新已判断：
  - `docs/system_architecture.md`
  - `docs/module_map.md`
  - `docs/runtime_flow.md`
  - `docs/handover_guide.md`
- [ ] 若本次改动改变了调用链或边界，文档已同步更新。

---

## 15. 结论

- 本次架构回归结论：通过 / 有条件通过 / 不通过
- 主要风险项：
- 必须在下次迭代前完成的修复：
- 备注：
