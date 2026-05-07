# 测试矩阵（当前基线）

更新时间：2026-05-07  
适用仓库：`math_search_win`

## 1. 目录与配置映射规范（强约束）

请严格保持「构建目录」与「配置」一一对应：

- Debug 配置：`out/build/msvc-debug*`（如 `msvc-debug-tests`） + `-C Debug`
- Release 配置：`out/build/msvc-release` + `-C Release`

禁止混用：

- `out/build/msvc-release` + `--config Debug`
- `out/build/msvc-debug*` + `--config Release`

## 2. 当前 CTest 用例总数

基于 `out/build/msvc-debug-tests` 的当前枚举结果：**15 项**。

```powershell
ctest --test-dir out/build/msvc-debug-tests -C Debug -N
```

当前 15 个测试目标：

1. `search_service_tests`
2. `suggest_service_tests`
3. `conclusion_index_repository_tests`
4. `detail_perf_aggregator_tests`
5. `detail_render_chain_tests`
6. `conclusion_pdf_map_loader_tests`
7. `storage_repository_tests`
8. `activation_code_service_tests`
9. `license_service_tests`
10. `feature_gate_tests`
11. `activation_page_tests`
12. `page_wiring_tests`
13. `main_window_round5_tests`
14. `search_page_round5_tests`
15. `settings_activation_round5_tests`

## 3. 全量回归命令（规范版）

### 3.1 Debug 全量

```powershell
cmake -S . -B out/build/msvc-debug-tests -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.11.0/msvc2022_64"
cmake --build out/build/msvc-debug-tests --config Debug
ctest --test-dir out/build/msvc-debug-tests -C Debug --output-on-failure
```

### 3.2 Release 全量

```powershell
cmake -S . -B out/build/msvc-release -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.11.0/msvc2022_64"
cmake --build out/build/msvc-release --config Release
ctest --test-dir out/build/msvc-release -C Release --output-on-failure
```

## 4. 模块级回归（Debug）

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
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "detail_render_chain_tests|detail_perf_aggregator_tests|conclusion_pdf_map_loader_tests" --output-on-failure

# ui
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "page_wiring_tests|main_window_round5_tests|search_page_round5_tests|settings_activation_round5_tests" --output-on-failure
```

## 5. 最近一次 Debug 全量结果快照

执行命令：

```powershell
ctest --test-dir out/build/msvc-debug-tests -C Debug --output-on-failure
```

结果：

- 通过：14/15
- 失败：1/15
- 失败项：`conclusion_pdf_map_loader_tests`
- 失败现象：`no such file or directory`（对应测试可执行文件未就绪）

## 6. 快速定位建议

```powershell
# 只看测试清单
ctest --test-dir out/build/msvc-debug-tests -C Debug -N

# 单项重跑（Debug）
ctest --test-dir out/build/msvc-debug-tests -C Debug -R "conclusion_pdf_map_loader_tests" --output-on-failure -V

# Release 下验证同名用例（仅用于交叉确认）
ctest --test-dir out/build/msvc-release -C Release -R "conclusion_pdf_map_loader_tests" --output-on-failure -V
```
