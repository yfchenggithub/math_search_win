# Detail Content Visual Enhancement Round 4 Summary

## 1. 文档信息
- 日期：2026-04-20
- 主题：详情内容页视觉增强（Round 4）
- 目标：在不改功能链路的前提下，建立“一级 section 导航 + 二级段落路标 + 三级步骤提示”的阅读层级。

## 2. 本轮范围与约束
- 仅做内容层视觉增强。
- 不改数据契约。
- 不改 C++ -> WebEngine 注入逻辑。
- 不改 section 映射逻辑。
- 不改公式渲染逻辑（KaTeX/MathJax 渲染链路保持不变）。
- 不改功能行为。

## 3. 修改文件
- `resources/detail/detail.css`
- `resources/detail/detail.js`

未修改：
- `resources/detail/detail_template.html`
- C++ 侧渲染与注入代码

## 4. 关键实现

### 4.1 三层层级语义（class 体系）
一级（section 级，最强）：
- `detail-section-title`（与现有 `detail-section-header` 并存）

二级（section 内部结构标签，中强“段落路标”）：
- `detail-subtitle-primary`
- `detail-subtitle-level2`
- `detail-subtitle-understanding`
- `detail-subtitle-proof`
- `detail-subtitle-usage`
- `detail-subtitle-pitfall`
- `detail-subtitle-summary`
- `detail-subtitle-insight`

三级（步骤/条目，轻量提示）：
- `detail-subtitle-level3`
- `detail-subpoint-label`
- `detail-step-label`
- `detail-condition-label`
- `detail-scene-label`
- `detail-method-label`
- `detail-point-label`

辅助语义：
- `detail-block`
- `detail-understanding-block`
- `detail-proof-block`
- `detail-paragraph`
- `detail-inline-subtitle-paragraph`
- `detail-subtitle-lead`

### 4.2 JS：最小侵入标签识别与挂载
在 `detail.js` 中新增了最小识别模块，仅对 section body 的已渲染 HTML 做后处理，不改数据来源与渲染流程：

1. 识别范围
- 仅处理 `.detail-rich-text` 内 `p, li`。
- 仅匹配固定标签集合（避免误伤普通正文）。

2. 匹配方式
- 支持“独立段落标签”（如“思路提示”）。
- 支持“段首前缀标签: 正文”（如“步骤一：……”）。
- 处理中英文冒号、全角数字归一化。

3. section 语义归一
- 对 `pitfall/traps/examples/explanation` 做 alias 归一，确保匹配稳定。

4. 安全策略
- 若段落含复杂子节点或数学节点，不做激进改写。
- 仅在 `patchSection(..., isPlaceholder=false)` 时调用 `decorateSectionBodyLabels(...)`。

### 4.3 CSS：视觉策略落地
1. 二级标签（段落路标）
- 低饱和主题色（按理解/证明/用法/易错点/总结分主题）。
- 细左强调条 + 极浅底色 + 轻边框。
- 强于正文、弱于一级 section 标题。

2. 三级标签（轻量步骤提示）
- 明显弱于二级：更小字号、更浅底色、更弱边框、更小留白。
- 防止“三级看起来像新标题层”。

3. 阅读与公式共存
- 调整二/三级标签与正文、列表、公式块邻接间距。
- 目标是增强扫读，不抢正文与公式注意力。

## 5. 标签映射（本轮重点）
二级（中强高亮）重点覆盖：
- 理解：一句话直觉、核心拆解、几何本质、代数意义、考点价值、顿悟点、使用场景
- 证明/推导：思路提示、正式推导、结论回扣
- 用法：例1、例2、例3
- 易错点：易错点一、易错点二、易错点三
- 总结：一句话核心、使用条件、关键提醒

三级（轻量提示）重点覆盖：
- 条件1~4
- 第一步~第四步
- 步骤一~步骤四
- 场景一~场景三
- 考法一~考法三
- 要点一~要点三

## 6. 约束符合性检查
- 未改 `detail` 数据字段契约。
- 未改 section 数据映射逻辑。
- 未改 C++ 注入流程。
- 未改公式渲染调用路径。
- 未改页面功能行为。

## 7. 验证
- JS 语法校验：`detail.js` 通过 `node` 解析检查。
- 变更集中在 `resources/detail/detail.css` 与 `resources/detail/detail.js`。

## 8. 后续建议（可选）
- 在典型样本（理解+证明+公式混排）上做一次视觉走查，重点确认：
  - 二/三级强度差是否稳定。
  - 三级标签是否仍保持“提示感”而非“标题感”。
  - 公式前后留白在长文档中是否一致。
