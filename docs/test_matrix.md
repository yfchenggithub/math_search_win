# 测试矩阵（第六轮测试收口）

更新时间：2026-04-21  
适用仓库：`math_search_win`

## 1. 当前测试体系总览

- 自动化入口：`CTest + Qt Test`（`tests/CMakeLists.txt`）
- 当前 CTest 用例总数：14
- 最新回归快照（本轮执行）：
  - 命令：`ctest --test-dir out/build/msvc-debug-tests -C Debug --output-on-failure`
  - 结果：14/14 通过，0 失败

当前 14 个 CTest 目标：

1. `search_service_tests`
2. `suggest_service_tests`
3. `conclusion_index_repository_tests`
4. `detail_perf_aggregator_tests`
5. `detail_render_chain_tests`
6. `storage_repository_tests`
7. `activation_code_service_tests`
8. `license_service_tests`
9. `feature_gate_tests`
10. `activation_page_tests`
11. `page_wiring_tests`
12. `main_window_round5_tests`
13. `search_page_round5_tests`
14. `settings_activation_round5_tests`

## 2. 模块测试矩阵

| 模块 | 自动化覆盖情况 | 关键自动化测试 | 人工冒烟项 | 已知未覆盖风险 |
|---|---|---|---|---|
| `search` | 已覆盖（核心算法 + 页面触发 +索引仓库） | `search_service_tests`、`conclusion_index_repository_tests`、`search_page_round5_tests`、`main_window_round5_tests` | 搜索按钮/回车触发、过滤项生效、首项自动选中详情 | 真实大索引下的排序质量与性能仅有 smoke，不是强约束性能承诺 |
| `suggest` | 已覆盖（prefix/term/dedup/filter/排序） | `suggest_service_tests`、`search_page_round5_tests` | 输入联想出现、点击建议后触发 `suggest_click` 搜索 | Suggest 与真实线上词频策略无对齐（当前仅离线索引契约） |
| `storage` | 已覆盖（favorites/history/settings + 原子写） | `storage_repository_tests`、`page_wiring_tests`、`search_page_round5_tests` | 收藏增删后落盘、历史重搜/删除/清空、settings 缺失/损坏回退 | 多进程并发写冲突、极端掉电恢复场景未自动化 |
| `activation` | 已覆盖（码解析/校验、license 状态机、feature gate、激活页交互） | `activation_code_service_tests`、`license_service_tests`、`feature_gate_tests`、`activation_page_tests`、`settings_activation_round5_tests` | 输入激活码、reload 状态刷新、过期码报错可读 | 签名校验/解密仍为 TODO stub，安全强校验未落地 |
| `detail` | 已覆盖（渲染链路分支 + fallback + perf 聚合） | `detail_render_chain_tests`、`detail_perf_aggregator_tests`、`search_page_round5_tests`、`main_window_round5_tests` | 详情 Web 正常渲染；缺资源时 fallback 文本回退 | `detail.js` 与 C++ payload 的端到端浏览器级契约仍缺少真实 WebEngine 集成回归 |
| `ui` | 已覆盖（页面装配、跨页信号、关键按钮行为） | `page_wiring_tests`、`main_window_round5_tests`、`search_page_round5_tests`、`settings_activation_round5_tests`、`activation_page_tests` | 启动后首页/导航/跨页跳转、收藏与历史联动、设置与激活按钮可用 | 缺少像素级视觉回归；`main.cpp --probe-only` 启动参数未有专门自动化 |

## 3. 与《架构回归清单》的联动规则

联动基线：`docs/architecture_review_checklist.md` 第 14 节（测试与文档同步回归）。

本轮把“受影响模块必须有对应测试”具体化为以下执行规则：

1. 先判定变更影响模块（至少标注到 `search/suggest/storage/activation/detail/ui` 之一）。
2. 按下表执行对应 CTest 集（可用 `-R` 正则）。
3. 在提交说明或回归记录中附上执行命令与通过/失败结论。
4. 若受影响模块暂无自动化覆盖，必须补至少 1 条人工冒烟步骤，并在本矩阵登记风险与补测计划。

| 受影响模块 | 必跑 CTest 目标（最小集合） | 建议命令 |
|---|---|---|
| `search` | `search_service_tests` + `search_page_round5_tests` | `ctest --test-dir out/build/msvc-debug-tests -C Debug -R "search_service_tests|search_page_round5_tests" --output-on-failure` |
| `suggest` | `suggest_service_tests` + `search_page_round5_tests` | `ctest --test-dir out/build/msvc-debug-tests -C Debug -R "suggest_service_tests|search_page_round5_tests" --output-on-failure` |
| `storage` | `storage_repository_tests` + `page_wiring_tests` | `ctest --test-dir out/build/msvc-debug-tests -C Debug -R "storage_repository_tests|page_wiring_tests" --output-on-failure` |
| `activation` | `activation_code_service_tests` + `license_service_tests` + `feature_gate_tests` + `activation_page_tests` | `ctest --test-dir out/build/msvc-debug-tests -C Debug -R "activation_code_service_tests|license_service_tests|feature_gate_tests|activation_page_tests" --output-on-failure` |
| `detail` | `detail_render_chain_tests` + `detail_perf_aggregator_tests` | `ctest --test-dir out/build/msvc-debug-tests -C Debug -R "detail_render_chain_tests|detail_perf_aggregator_tests" --output-on-failure` |
| `ui` | `main_window_round5_tests` + `page_wiring_tests` + `search_page_round5_tests` + `settings_activation_round5_tests` | `ctest --test-dir out/build/msvc-debug-tests -C Debug -R "main_window_round5_tests|page_wiring_tests|search_page_round5_tests|settings_activation_round5_tests" --output-on-failure` |

## 4. 人工冒烟建议（至少一组）

建议每次发版前执行以下 8 项：

1. 启动：正常启动到 Home；`--probe-only` 参数可直接退出且无崩溃。
2. 搜索：输入关键词，点击搜索或回车，结果列表变化且首项自动选中。
3. Suggest：输入前缀出现建议，点击建议后触发搜索并写入历史来源 `suggest_click`。
4. 收藏：在搜索页收藏/取消收藏，`cache/favorites.json` 与 Favorites/Home 联动更新。
5. 最近记录：执行搜索后 Recent 页面可见记录，可重搜、删除、清空。
6. 激活：输入合法激活码后状态切到 Active；输入非法/过期码有可读错误提示。
7. trial/full 门控：trial 下搜索条数、详情、收藏/高级筛选受限；full 下解除限制。
8. 详情 Web/fallback：资源完整时走 Web 详情；故意缺失 `resources/detail` 或 `resources/katex` 时可回退文本模式。

## 5. 性能基线（最小可比较）

当前已有性能链路：

- 详情性能日志：`DetailPerfAggregator` + JS `[perf][detail]`。
- 搜索性能日志分类：`perf.search`（日志分类存在）。

本轮新增最小基线自动化：

- 用例：`SearchServiceTest::performanceBaseline_round2_limitQuery_logsAvgAndP95`
- 所在文件：`tests/search/test_search_service.cpp`
- 目标：记录固定夹具查询 `limit query` 的 `avg/p95/min/max`，并设置宽松阈值（`p95 < 500ms`）用于拦截明显退化。

本轮样例输出（本地一次执行）：

- `[perf][search][baseline] fixture=round2 query="limit query" runs=40 avgMs=0.070 p95Ms=0.072 minMs=0.069 maxMs=0.072`

说明：

- 该基线用于“同机型/同环境”的趋势比较，不是对外性能承诺。
- 后续可按版本记录同一用例输出，观察 `p95` 漂移。

## 6. 推荐运行命令

### 6.1 全量回归

```powershell
cmake -S . -B out/build/msvc-debug-tests -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.11.0/msvc2022_64"
cmake --build out/build/msvc-debug-tests --config Debug
ctest --test-dir out/build/msvc-debug-tests -C Debug --output-on-failure
```

### 6.2 按模块跑

```powershell
# search
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "search_service_tests|conclusion_index_repository_tests|search_page_round5_tests" --output-on-failure

# suggest
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "suggest_service_tests|search_page_round5_tests" --output-on-failure

# storage
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "storage_repository_tests|page_wiring_tests" --output-on-failure

# activation
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "activation_code_service_tests|license_service_tests|feature_gate_tests|activation_page_tests|settings_activation_round5_tests" --output-on-failure

# detail
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "detail_render_chain_tests|detail_perf_aggregator_tests" --output-on-failure

# ui
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "page_wiring_tests|main_window_round5_tests|search_page_round5_tests|settings_activation_round5_tests" --output-on-failure
```

### 6.3 失败后快速定位

```powershell
# 列出可用测试名（先确认命中规则）
ctest --test-dir out/build/msvc-debug-tests -C Debug -N

# 只跑失败模块并打开详细日志
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "search_service_tests" --output-on-failure -V

# 列出某个 QtTest 可用测试函数
cmake -E env "PATH=D:/Qt/6.11.0/msvc2022_64/bin;$env:PATH" out/build/msvc-debug-tests/tests/Debug/math_search_search_tests.exe -functions

# 只跑单个测试函数（示例）
cmake -E env "PATH=D:/Qt/6.11.0/msvc2022_64/bin;$env:PATH" out/build/msvc-debug-tests/tests/Debug/math_search_search_tests.exe behaviorRound2_trialAndFullResultCap_contracts
```
