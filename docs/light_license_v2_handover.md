# 轻授权骨架 v2 实现交接文档

更新时间：2026-04-21  
适用分支：当前工作区（`math_search_win`）

## 1. 本轮目标与结果

本轮已落地“激活码输入 -> 本地 `license/license.dat` 落盘 -> 启动统一读 `license.dat`”的完整闭环，并接入 `FeatureGate` 到搜索/详情/收藏能力。

达成点：

1. 新增授权核心模块：`LicenseState`、`DeviceFingerprintService`、`ActivationCodeService`、`LicenseService`、`FeatureGate`
2. 激活页从占位升级为可真实激活、可失败提示、可重新检测授权
3. 设置页新增授权信息展示（只读）
4. 启动时统一读取 `license/license.dat` 恢复授权，不依赖输入框历史
5. 搜索/详情/收藏按 `FeatureGate` 做体验版/正式版差异控制
6. 已通过本地编译与测试

## 2. 设计原则（为什么这么做）

1. 最小侵入：不推翻现有 Qt Widgets 布局，保留已有页面结构和样式对象名
2. 状态单一来源：
   - 激活码只是输入媒介
   - `license/license.dat` 是持久化状态载体
   - `LicenseService` 是运行态授权状态源
   - `FeatureGate` 是功能开关源
3. 默认降级安全：读失败/解析失败/设备不匹配/校验失败全部降级体验版，不崩溃
4. 轻协议可演进：当前用 CRC32 轻校验，同时预留真实签名/加密/到期校验扩展点

## 3. 激活码协议与授权文件

## 3.1 激活码协议（已实现）

格式：`MSW1.<payload_base64url>.<check8>`

- 前缀：固定 `MSW1`
- `payload_base64url`：JSON UTF-8 后 Base64Url
- `check8`：对“解码后的原始 JSON 文本”做 CRC32，8 位大写十六进制

已校验字段：

- `v==1`
- `p=="msw"`
- `s/w/e/d` 非空（其中 `e` 后续还会校验必须为 `full`）
- `f` 为非空数组
- `iat` 非空
- `exp` 可空

已实现本地校验：

1. 三段式格式校验
2. Base64Url 解码与 JSON 解析
3. CRC32 `check8` 校验
4. 当前设备码与 payload `d` 匹配校验
5. 仅接受 `e=full`
6. 短码 `f` -> 强类型 `Feature` 映射

短码映射（集中在 `FeatureGate`）：

- `bsp` -> `basic_search_preview`
- `fs` -> `full_search`
- `fd` -> `full_detail`
- `fav` -> `favorites`
- `af` -> `advanced_filter`

## 3.2 license.dat 格式（已实现）

路径：`license/license.dat`  
格式：`key=value`（单一主格式）

已写入字段：

- `format=msw-license-v1`
- `product=math_search_win`
- `serial=...`
- `watermark=...`
- `edition=full`
- `device=...`
- `features=basic_search_preview,...`
- `issued_at=...`
- `expire_at=...`
- `issuer=offline-manual`
- `source=activation_code`
- `payload_ver=1`
- `activation_prefix=MSW1`
- `activation_check=...`
- `status=valid`

## 4. 核心类职责

## 4.1 `LicenseState`

文件：

- `src/license/license_state.h`
- `src/license/license_state.cpp`

职责：

- 统一承载授权运行态
- 提供状态枚举（`Missing/Trial/ValidFull/Invalid/ReadError/ParseError/DeviceMismatch/...`）
- 提供 UI 与 Gate 统一可读字段（serial、水印、设备码、技术原因、已启用功能等）

## 4.2 `DeviceFingerprintService`

文件：

- `src/license/device_fingerprint_service.h`
- `src/license/device_fingerprint_service.cpp`

职责：

- 生成稳定设备码（尽量使用机器稳定信息组合后 SHA256）
- 支持字段缺失降级
- 输出展示友好格式：`XXXX-XXXX-XXXX-XXXX`

## 4.3 `ActivationCodeService`

文件：

- `src/license/activation_code_service.h`
- `src/license/activation_code_service.cpp`

职责：

- 解析激活码
- 校验激活码（格式/CRC/设备/edition/feature）
- 构建 `license.dat` 内容
- 对日志输出做激活码脱敏

关键接口：

- `parseActivationCode(...)`
- `validateActivationCode(...)`
- `buildLicenseFileContent(...)`
- `maskActivationCodeForLog(...)`

## 4.4 `LicenseService`

文件：

- `src/license/license_service.h`
- `src/license/license_service.cpp`

职责：

- 启动初始化读取 `license/license.dat`
- 解析 + 基础校验 + 设备校验 + edition 校验
- 维护当前 `LicenseState`
- 提供 `reload()` 与 `writeLicenseFile(...)`
- 提供 `licenseStateChanged(...)` 信号

策略：

- 文件不存在 -> `Missing` -> 体验版
- 读取失败 -> `ReadError` -> 体验版
- 解析失败 -> `ParseError` -> 体验版
- 校验失败 -> `Invalid/DeviceMismatch` -> 体验版
- 校验通过且 `edition=full` -> `ValidFull` -> 正式版

## 4.5 `FeatureGate`

文件：

- `src/license/feature_gate.h`
- `src/license/feature_gate.cpp`

职责：

- 强类型 `Feature` 开关判断
- 对页面返回禁用原因
- 根据 `LicenseState` 统一切换试用/正式版能力

规则：

- 体验版：仅 `BasicSearchPreview=true`
- 正式版：全部 `true`

## 5. 启动与激活流程（端到端）

## 5.1 启动流程

1. `MainWindow` 构造时调用 `licenseService_.initialize()`
2. `LicenseService` 读取并校验 `license/license.dat`
3. 生成 `LicenseState`
4. `featureGate_.setLicenseState(...)`
5. 页面按状态刷新（激活页/设置页/搜索页/收藏页）

## 5.2 激活流程

1. 激活页读取输入框激活码（trim + 判空）
2. `ActivationCodeService::parseActivationCode(...)`
3. `ActivationCodeService::validateActivationCode(...)`
4. 成功后 `buildLicenseFileContent(...)`
5. `LicenseService::writeLicenseFile(...)` 写入 `license/license.dat`
6. `LicenseService::reload()` 立刻生效
7. UI 刷新为正式版，功能开关解锁

失败处理：

- 不写入正式授权
- 保持体验版
- 激活页显示明确失败原因（状态与提示）

## 5.3 重启恢复

程序重启后只读 `license/license.dat` 恢复授权状态，不依赖激活码输入框历史。

## 6. 页面接入说明

## 6.1 激活/升级页 `ActivationPage`

文件：

- `src/ui/pages/activation_page.h`
- `src/ui/pages/activation_page.cpp`

已接入：

1. 展示真实设备码（来自 `DeviceFingerprintService`）
2. 展示授权状态/授权类型/有效期/授权编号/水印编号
3. “立即激活”触发真实解析校验落盘逻辑
4. “重新检测授权”触发 `LicenseService::reload()`
5. 失败原因即时可见（激活失败态）

## 6.2 设置/关于页 `SettingsPage`

文件：

- `src/ui/pages/settings_page.h`
- `src/ui/pages/settings_page.cpp`

新增“授权信息”区（只读）：

- 当前授权状态（体验版/正式版）
- 授权文件状态（未找到/有效/无效/设备不匹配/读取失败等）
- 授权编号、水印编号
- 本机设备码
- 授权文件路径
- 状态说明与技术原因

## 6.3 搜索页 `SearchPage`

文件：

- `src/ui/pages/search_page.h`
- `src/ui/pages/search_page.cpp`

已接入：

1. `full_search`：
   - 体验版限制展示前 `5` 条
   - 正式版开放完整搜索
2. `advanced_filter`：
   - 体验版禁用模块/分类/标签筛选
   - 正式版开放
3. `full_detail`：
   - 体验版只展示标题/摘要/部分内容预览 + 升级提示
   - 正式版展示完整详情
4. `favorites`：
   - 新增“收藏当前结论”按钮
   - 体验版阻止写入，给统一提示
   - 正式版可写入收藏并联动刷新首页/收藏页

## 6.4 收藏页 `FavoritesPage`

文件：

- `src/ui/pages/favorites_page.h`
- `src/ui/pages/favorites_page.cpp`

已接入：

- 体验版锁定收藏功能（展示锁定提示，不允许写操作）
- 正式版维持原收藏列表能力

## 6.5 主窗口注入 `MainWindow`

文件：

- `src/ui/main_window.h`
- `src/ui/main_window.cpp`

新增服务成员注入：

- `DeviceFingerprintService`
- `ActivationCodeService`
- `LicenseService`
- `FeatureGate`

并统一状态传播到页面。

## 7. 变更文件清单

新增：

- `src/license/license_state.h`
- `src/license/license_state.cpp`
- `src/license/device_fingerprint_service.h`
- `src/license/device_fingerprint_service.cpp`
- `src/license/activation_code_service.h`
- `src/license/activation_code_service.cpp`
- `src/license/license_service.h`
- `src/license/license_service.cpp`
- `src/license/feature_gate.h`
- `src/license/feature_gate.cpp`
- `docs/light_license_v2_handover.md`（本文档）

修改：

- `CMakeLists.txt`
- `src/ui/main_window.h`
- `src/ui/main_window.cpp`
- `src/ui/pages/activation_page.h`
- `src/ui/pages/activation_page.cpp`
- `src/ui/pages/settings_page.h`
- `src/ui/pages/settings_page.cpp`
- `src/ui/pages/search_page.h`
- `src/ui/pages/search_page.cpp`
- `src/ui/pages/favorites_page.h`
- `src/ui/pages/favorites_page.cpp`

## 8. 编译与测试

本地已执行：

1. `cmake --build out/build/msvc-debug --config Debug`  
   结果：通过，生成 `math_search_win.exe`
2. `ctest --test-dir out/build/msvc-debug -C Debug --output-on-failure`  
   结果：`4/4` 通过

## 9. 后续扩展预留（已留接口/TODO）

在代码中已留扩展点：

1. `verifyActivationSignature(...)`  
   从 CRC32 轻校验升级到真实签名
2. `decryptActivationPayload(...)`  
   接入 payload 加密
3. `validateExpiration(...)`  
   到期策略升级（时区/宽限/时间源）
4. `validateFeatureSet(...)`  
   套餐能力集校验
5. `verifyLicenseSignature(...)`  
   `license.dat` 签名校验预留

## 10. 维护建议

1. 后续若接入服务端签发工具，建议同时提供“激活码生成脚本/工具”与本地 QA 示例
2. 若计划增强抗篡改能力，可在 `LicenseService` 增加哈希链与签名字段并启用强校验失败审计日志
3. 若要支持套餐化，优先扩展 `FeatureGate` 与 `validateFeatureSet(...)`，页面无需大改

