# AGENTS.md

## Repository rule

本仓库是一个 Windows 本地离线版《高中数学二级结论 3 秒搜索系统》。

任何涉及以下内容的修改，都必须同步检查并更新 docs 文档：

- 系统架构
- 模块边界
- 页面结构
- 启动流程
- 数据流
- 本地存储结构
- 授权/激活流程
- HTML/CSS/JS/Katex 渲染链路
- 搜索 / suggest / detail 的调用链

## Documentation requirements

当修改影响系统结构时，请同步更新以下文件（如存在）：

- docs/system_architecture.md
- docs/module_map.md
- docs/runtime_flow.md
- docs/handover_guide.md

文档必须遵守：

- 只基于真实代码
- 不允许把规划中写成已实现
- 必须区分：已实现 / 部分实现 / 骨架 / 规划中
- 必须尽量引用真实目录、文件名、类名、函数名
- 必须写清关键调用链与依赖关系
- 架构图统一使用 Mermaid
- 面向维护者与接手人，而不是面向产品宣传

## Architecture focus

优先说明这些关键路径：

1. 程序入口与初始化
2. 页面装配关系
3. 搜索链路
4. Suggest 链路
5. 详情渲染链路
6. 收藏/历史/设置持久化链路
7. 激活/授权链路
8. 本地资源与模板组织方式
9. 日志与调试入口

## Writing style

- 少空话，多证据
- 少泛化描述，多具体引用
- 若代码中无法确认，明确写"待确认"
- 如果旧文档与代码冲突，以代码为准
