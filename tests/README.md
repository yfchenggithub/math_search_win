# Tests 快速上手指南

本目录提供 `SearchService` / `SuggestService` 的自动化单元测试基础，目标是：

- 可编译
- 可运行
- 可回归
- 可扩展

当前测试栈：

- `Qt Test`（断言与用例组织）
- `CTest`（统一执行入口）
- `CMake`（独立测试目标，不污染主程序目标）

---

## 1. 目录结构

```text
tests/
  CMakeLists.txt
  fixtures/
    test_backend_search_index.json
  shared/
    test_fixture_loader.h
    test_fixture_loader.cpp
    test_dump_utils.h
  search/
    test_search_service.cpp
  suggest/
    test_suggest_service.cpp
```

说明：

- `fixtures/`：可控小索引样本，用于稳定验证过滤/排序/去重契约。
- `shared/`：测试公共能力（路径解析、索引加载、失败摘要）。
- `search/`：`SearchService` 行为契约测试。
- `suggest/`：`SuggestService` 行为契约测试。

---

## 2. 测试目标

由 `tests/CMakeLists.txt` 生成两个可执行测试目标：

- `math_search_search_tests`（注册为 `search_service_tests`）
- `math_search_suggest_tests`（注册为 `suggest_service_tests`）

顶层 `CMakeLists.txt` 中已通过 `include(CTest)` + `add_subdirectory(tests)` 接入测试构建。

---

## 3. 5 分钟跑通（推荐）

在仓库根目录执行：

```powershell
cmake -S . -B out/build/msvc-debug-tests -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.11.0/msvc2022_64"
cmake --build out/build/msvc-debug-tests --config Debug
ctest --test-dir out/build/msvc-debug-tests -C Debug --output-on-failure
```

如果你本地 `preset` 可直接使用，也可以执行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --test-dir out/build/msvc-debug -C Debug --output-on-failure
```

---

## 4. 常用命令

仅跑 Search 测试：

```powershell
ctest --test-dir out/build/msvc-debug-tests -C Debug -R search_service_tests --output-on-failure
```

仅跑 Suggest 测试：

```powershell
ctest --test-dir out/build/msvc-debug-tests -C Debug -R suggest_service_tests --output-on-failure
```

查看 Qt Test 中有哪些测试函数（示例）：

```powershell
cmake -E env "PATH=D:/Qt/6.11.0/msvc2022_64/bin;$env:PATH" out/build/msvc-debug-tests/tests/Debug/math_search_search_tests.exe -functions
```

---

## 5. 新增测试的标准流程

1. 明确行为契约  
例如：空查询、过滤是否生效、结果是否去重、同输入顺序是否稳定。

2. 优先在 `fixtures/test_backend_search_index.json` 补最小数据  
避免绑定真实大索引里的偶然排序。

3. 在对应文件新增用例  
- Search：`search/test_search_service.cpp`  
- Suggest：`suggest/test_suggest_service.cpp`

4. 保持断言可读  
失败信息至少包含：`query`、过滤条件、期望行为、结果摘要。

5. 运行 `build + ctest` 回归验证  
确保改动不会影响既有测试契约。

---

## 6. 失败排查建议

1. 先看断言信息里的 `expectation / query / options / result`，快速判断是过滤失效、去重失效还是排序不稳。  
2. 检查 `fixtures` 中该 query 的 term/prefix posting 是否覆盖了你要验证的场景。  
3. 排序类断言尽量用“相对优先级”或“前 N 稳定性”，不要硬编码全量顺序。  
4. 真实索引 smoke 用例允许 `QSKIP`（例如真实数据不可用），这是预期行为。  

---

## 7. 编写测试的约束

- 不依赖当前时间、随机数、网络、UI 交互。
- 不引入新第三方测试框架（例如 GoogleTest/Catch2/doctest）。
- 不为测试重写业务核心算法。
- 优先测试“输入 -> 输出”的行为契约，而非实现细节。

---

## 8. 给新同学的建议

第一天建议顺序：

1. 先完整跑一遍第 3 节命令。
2. 打开 `search/test_search_service.cpp` 和 `suggest/test_suggest_service.cpp` 看已有用例命名风格。
3. 先加一个“小而稳”的过滤用例，再提交。

按这个节奏，你可以在不改业务核心的前提下，持续扩展测试覆盖。
