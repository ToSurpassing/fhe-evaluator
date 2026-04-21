# 0. 总体写作策略摘要

这篇本科毕业设计初稿最适合采用的叙事主线是：

> 从同态多项式求值中的 materialization 开销出发，围绕 OpenFHE/CKKS 用户层可实现性，逐步构建一个 conservative、bounded、repo-local 的 evaluator 原型体系，并以 bounded degree-31 `t=3` explicit Internal BSGS prototype 作为当前正式实现版本，验证 delayed materialization / lazy raw-chain 在固定可追踪场景下减少 relin/rescale 操作的工程可行性。

这条主线比“完整复现论文算法”更安全，也更符合当前仓库证据。仓库已经形成了非常清晰的工程演化链：smoke/survival 验证 OpenFHE/CKKS API 行为，degree-8 V1 建立 eager/lazy baseline，`20-25` 形成固定 `t=2/t=3` 非对称原型，`26-33` 推进到 plan object 与 generated-plan，`34-43` 显式引入 Internal BSGS，最后 `44-50` 转向 bounded `t=3`，并在 `50` 中形成当前最适合作为论文初稿正式版的 degree-31 explicit Internal BSGS prototype。

应以 `ckks_evaluator_50_bounded_t3_degree31_internal_bsgs` 作为正式版，原因有三点。第一，它比 `41-43` 的 `t=2,B=4` explicit Internal BSGS 更接近上游论文中 higher-`t` delayed materialization 的核心方向。第二，它比 `46/49` 的 degree<=15 bounded `t=3` 版本更适合论文主结果，因为 degree bound 扩展到 `<=31`，并明确使用 `S2={1,x^8,x^16,x^24}`。第三，它有强证据闭环：源码中显式约束 degree、outer base、heavy term 数量，并在运行时检查 tensor count 不变、relin/rescale 减少、误差低于 `1e-8`；日志中两个 cases、两种 scaling mode 均给出 relin/rescale 各减少 4 次的结果。

中期报告中的 `t=2` 目标不需要被否定，而应写成“保守迁移路径中的第一阶段”。可以表述为：中期阶段先选择 `t=2` 是为了验证浅层 lazy / grouped-lazy 在 CKKS 用户层中的合法性与可观测性；随着 smoke、degree-8 baseline、固定 asymmetric prototypes 和 Internal BSGS 结构逐步稳定，后续自然扩展到 bounded `t=3`，以更贴近论文中连续 tensor product 和 delayed materialization 的思想。这样写既承认项目目标发生了推进，又不会显得前后冲突。

整篇论文应避免宣传式表达。最稳妥的写法是把贡献限定为：**实现了一个白盒、固定边界、可追踪的 CKKS evaluator 原型体系；在有限 degree-31 `t=3` cases 中验证了 tensor-product count 不变而 relin/rescale 减少；通过 plaintext shadow 与 CKKS 解密误差检查保证结果可信；同时明确指出 runtime 结果仍是 mixed signal，当前实现尚非 full paper algorithm 或 arbitrary planner。**

# 1. 论文总纲建议

| 章节 | 二级标题建议 | 章节目标 | 主要证据来源 | 推荐篇幅 |
|---|---|---|---|---|
| 第一章 绪论 | 1.1 研究背景；1.2 问题定义；1.3 国内外相关方向概述；1.4 本文工作与边界；1.5 论文结构 | 引出 CKKS 多项式求值中 materialization 开销问题，说明本文做的是用户层保守迁移 | `asymmetric_bsgs_ckks_impl_notes.md`, `AGENTS.md`, Executive Summary | 中 |
| 第二章 理论基础与相关工作 | 2.1 CKKS 与 leveled FHE；2.2 多项式求值与 BSGS/PS；2.3 lazy scheduling；2.4 Asymmetric BSGS 论文思想；2.5 本项目可验证范围 | 区分 paper-level fact 与 repo-level fact | Evidence E01-E02，背景证据包 | 中/长 |
| 第三章 面向 OpenFHE/CKKS 的保守迁移思路 | 3.1 用户层实现边界；3.2 为什么需要 smoke/survival；3.3 bounded/white-box 原则；3.4 从中期 `t=2` 到当前 `t=3`；3.5 当前正式版定位 | 解释为什么不直接实现 full algorithm，而是做 bounded prototype | `AGENTS.md`, `project_progress_summary.md`, 当前正式版判定 | 中 |
| 第四章 系统设计与实现演化 | 4.1 总体架构；4.2 smoke/survival；4.3 degree-8 baseline；4.4 fixed asymmetric prototypes；4.5 generated-plan；4.6 Internal BSGS；4.7 bounded degree-31 `t=3` formal version | 以技术里程碑组织实现 | 里程碑时间线、Evidence E03-E28 | 长 |
| 第五章 实验设计 | 5.1 研究问题；5.2 实验对象；5.3 对照路径；5.4 指标定义；5.5 参数与环境；5.6 有效性威胁 | 把实验写成机制归因型 | `EvalStats`, final log, runtime docs | 中 |
| 第六章 实验结果与分析 | 6.1 主结果；6.2 bounded `t=3` 支撑结果；6.3 Internal BSGS 过渡结果；6.4 runtime 分析；6.5 结果边界 | 以 `50` 为中心呈现实验 | final log, bounded t3 docs, runtime CSV | 长 |
| 第七章 总结与展望 | 7.1 工作总结；7.2 主要贡献；7.3 局限性；7.4 后续工作 | 收束贡献与未来工作 | safe claims / forbidden claims / gaps | 中 |

# 2. 每章写作素材包

## 2.1 第一章 绪论

**本章目标**

说明为什么 CKKS 中高阶多项式求值值得研究，为什么仅比较乘法次数不足以解释实际工程开销，并自然引出本文围绕 OpenFHE/CKKS 用户层实现 delayed materialization / lazy raw-chain 的保守迁移工作。

**核心论点**

1. 多项式求值是 leveled FHE 中常见且重要的计算形式。
2. 在 CKKS/OpenFHE 工程实现中，乘法并非单一抽象操作，通常伴随 tensor product、relinearization、rescale/level management。
3. 上游 asymmetric BSGS 论文的关键启发是：优化目标不应只看 tensor-product count，还应关注 key-switch / modulus-switch 风格的 materialization 开销。
4. 本项目不是实现新同态加密方案，也不是修改 OpenFHE 内核，而是在用户层构建可追踪 evaluator prototype。
5. 当前正式版是 bounded degree-31 `t=3` explicit Internal BSGS prototype。

**建议段落顺序**

1. 从 FHE/CKKS 应用中的多项式求值需求引入。
2. 说明高阶多项式求值的工程开销不只来自乘法次数。
3. 引出 BSGS/PS 及 asymmetric BSGS 的相关思路。
4. 说明本文选择 OpenFHE/CKKS 用户层保守迁移，而非 full paper implementation。
5. 概括本文完成的原型与实验结论。
6. 给出论文结构安排。

**适合引用的证据点**

- E01：上游论文关注 key/modulus switch 风格开销。
- E02：项目边界是 OpenFHE/CKKS 用户层，不修改内核。
- E27/E28：`degree31_spread_blocks`、`degree31_low7_blocks` 中 relin/rescale 减少 4 次。
- 当前正式版判定：`ckks_evaluator_50_bounded_t3_degree31_internal_bsgs`。

**不要写过头的地方**

- 不要说“本文完整复现了 asymmetric BSGS 算法”。
- 不要说“本文实现了任意多项式 planner”。
- 不要说“实验表明运行时间稳定提升”。
- 不要说“本文优化了 OpenFHE 内核”。

**半成品段落草稿**

随着同态加密技术的发展，CKKS 方案因其对近似实数和复数计算的支持，在隐私保护机器学习、加密信号处理和数值型安全计算等场景中具有较强的工程意义。在这些应用中，许多非线性函数最终需要被近似为多项式，并在密文状态下进行求值。因此，高阶多项式求值的效率直接影响 CKKS 应用的可用性。

传统分析中，多项式求值算法常常首先关注乘法次数或乘法深度。例如，Baby-Step Giant-Step 和 Peterson-Stockmeyer 类算法通过重组多项式结构，在一定程度上降低高阶求值所需的非标量乘法数量。然而，在 leveled FHE 的工程实现中，一次密文乘法并不只是抽象代数意义上的乘法。以 OpenFHE/CKKS 用户层为例，密文乘法通常涉及 tensor product、relinearization、rescale 或 level management 等后续处理。尤其是在 CKKS 中，尺度、模数层级和 ciphertext element 数量的变化都会影响后续运算的合法性和成本。因此，仅以 tensor-product 数量衡量 evaluator 代价并不足以完整反映工程开销。

论文 “A Novel Asymmetric BSGS Polynomial Evaluation Algorithm under Homomorphic Encryption” 的重要启发在于，它并不主要试图突破经典 BSGS/PS 框架下非标量乘法数量的数量级，而是关注同态乘法之后的 key-switch 和 modulus-switch 风格操作。该论文利用 baby-step 与 giant-step 在深度上的不对称性，提出通过更激进的 delayed materialization 方式减少中间 relinearization 和 modulus switching 操作。该思路对 CKKS 用户层 evaluator 具有直接的工程启发意义，但论文中的算法结构并不能被简单等同于 OpenFHE 用户层中立即可用的实现路径。原因在于，OpenFHE 暴露给用户层的 ciphertext 状态、level、noiseScaleDeg、relinearization key degree 以及 rescale 行为，都需要通过具体实验验证。

基于这一认识，本文的目标不是设计新的同态加密方案，也不是修改 OpenFHE 内核，而是在 OpenFHE/CKKS 用户层构建一个保守、有限边界、可追踪的 evaluator 原型体系。本文围绕 delayed materialization 与 lazy raw-chain 的工程迁移，依次完成 smoke/survival 验证、degree-8 eager/lazy baseline、固定 `t=2/t=3` asymmetric prototypes、generated-plan executor、explicit Internal BSGS 结构，以及当前正式版 bounded degree-31 `t=3` explicit Internal BSGS prototype。

当前正式版原型固定在 degree<=31 的有限范围内，采用 explicit Internal BSGS 拓扑，并在 `S2={1,x^8,x^16,x^24}` 的固定 outer block 结构下执行 eager 与 inner-lazy 两条路径对照。实验结果显示，在两个固定 coefficient patterns 中，inner-lazy 路径保持 tensor-product 数量不变，同时将 relin/rescale 操作减少 4 次；最终 CKKS 解密结果与 plaintext shadow 的最大绝对误差保持在 `1e-13` 量级，低于当前实验阈值 `1e-8`。这些结果说明，论文中的 materialization-reduction 思想可以在 OpenFHE/CKKS 用户层以 bounded prototype 的形式被安全表达和验证。

需要强调的是，本文当前实现仍然是 conservative, bounded, repo-local prototype。它不是 full asymmetric BSGS implementation，不支持任意多项式 planner，也没有实现 full outer aggressive lazy 或 CKKS bootstrapping。本文的贡献在于建立一条可追踪、可验证、可迭代的工程迁移路径，为后续更完整的 planner 和更大规模 benchmark 奠定基础。

## 2.2 第二章 理论基础与相关工作

**本章目标**

建立读者理解 CKKS、BSGS/PS、多项式求值、lazy scheduling、Internal BSGS 的基础，并严格区分上游论文事实与当前仓库验证事实。

**关键概念表**

| 概念 | 建议解释 | 与本文关系 |
|---|---|---|
| CKKS | 支持近似实数/复数计算的 leveled FHE 方案 | 本文实验对象 |
| Leveled FHE | 在有限 multiplicative depth 内执行密文计算 | 多项式求值成本背景 |
| Tensor product | 密文乘法产生的未 materialize 中间结果 | 本文保持计数不变 |
| Relinearization | 将高阶 ciphertext 结构压回较低形式 | 本文优化指标之一 |
| Rescale / Modulus switch | CKKS 中尺度和模数层级管理 | 本文优化指标之一 |
| BSGS | baby-step / giant-step 多项式求值组织 | 上游论文基础 |
| Peterson-Stockmeyer | 经典递归多项式求值算法 | 相关 baseline |
| Lazy scheduling | 延迟 materialization，把多个 raw products 聚合后再处理 | 本文核心工程策略 |
| Internal BSGS | 显式 inner block / outer block 结构 | 当前正式版拓扑 |
| Plaintext shadow | 明文参考计算 | 本文正确性验证方式 |

**文献脉络**

1. 高阶多项式求值是 leveled FHE 重要任务。
2. BSGS/PS 关注乘法数量和深度。
3. Asymmetric BSGS 论文强调 baby-step/giant-step 深度不对称。
4. 上游论文优化重点是减少 key/modulus switching，而非单纯减少 tensor product。
5. 本项目迁移的是 evaluator-level 思想，不是完整复现论文算法。

**如何写 paper-level fact 而不写成 repo-level 已实现**

| Paper-level fact | Repo-level 可写对应 |
|---|---|
| 论文提出 asymmetric BSGS 思路 | 本项目受该思路启发构建 bounded prototype |
| 论文讨论 aggressive lazy tensor products | 本项目验证有限 `t=3` raw-chain / inner-lazy |
| 论文目标是减少 switching | 本项目在固定 cases 中观察到 relin/rescale 减少 |
| 论文有更一般算法构造 | 本项目尚未实现 arbitrary planner |

**半成品段落草稿**

本章首先介绍本文所依赖的同态加密与多项式求值基础。CKKS 是一种支持近似数值计算的同态加密方案，适合处理实数或复数向量上的近似计算任务。在 CKKS 中，密文运算不仅受到乘法深度限制，还受到尺度管理、模数链消耗和 ciphertext 状态变化的影响。因此，在 CKKS 中实现高阶多项式求值时，必须同时关注代数表达式的乘法结构与具体库实现中的 level、scale 和 relinearization 行为。

在 leveled FHE 中，多项式求值是一个基础问题。许多非线性函数可以通过多项式近似表达，而加密状态下的非线性计算通常也会被转化为多项式求值。经典 BSGS 方法将多项式拆分为 baby-step 与 giant-step 两部分，通过预计算低阶幂和高阶幂来减少重复计算。Peterson-Stockmeyer 算法进一步从递归角度组织多项式求值，在非标量乘法数量上具有较好的理论性质。这些方法为高阶多项式求值提供了重要基础。

然而，在同态加密的工程环境中，非标量乘法数量并不是唯一成本指标。一次密文乘法通常会先产生更高阶的 tensor product 中间态，随后需要 relinearization 将 ciphertext 结构压回可继续使用的形式，并需要 rescale 或 modulus switching 管理 CKKS 的尺度和层级。这些 materialization 风格操作往往具有较高开销。上游论文 “A Novel Asymmetric BSGS Polynomial Evaluation Algorithm under Homomorphic Encryption” 的核心价值正在于重新审视这一成本结构：其优化目标并非主要减少 tensor product 的数量，而是利用 baby-step 和 giant-step 在深度上的不对称性，减少 key-switch 和 modulus-switch 类型操作。

该论文指出，在 BSGS 结构中，baby-step values 通常位于较浅的计算深度，而 giant-step values 位于较深层级。当两者组合时，浅层 baby-step 往往需要被推进到更深层级，这引入额外的 switching 或 rescale 开销。论文提出通过更激进的 lazy materialization 策略，使多个连续 tensor products 在中间不立即进行 relinearization 或 modulus switching，而是在聚合后统一 materialize。该思想为 evaluator-level 优化提供了新的方向。

需要区分的是，以上属于上游论文层面的算法思想和理论动机，并不意味着当前 OpenFHE/CKKS 用户层可以直接完整实现论文算法。CKKS 的用户层 API 对中间 ciphertext 状态、relinearization key degree、scaling mode 和 level alignment 有具体约束。本文仓库验证的是这些思想在有限工程边界内的可实现性，而不是声称复现了论文的完整一般算法。当前仓库中的实验对象包括 smoke/survival 测试、degree-8 baseline、固定 `t=2/t=3` prototypes、generated-plan executor、explicit Internal BSGS，以及最终的 bounded degree-31 `t=3` explicit Internal BSGS prototype。

因此，本章后续使用三个层次描述相关事实：第一，paper-level fact，即上游论文关于 asymmetric BSGS 与 delayed materialization 的理论主张；第二，repo-level validated fact，即当前仓库中通过源码、日志和实验结果验证的有限事实；第三，plausible inference，即基于当前实验趋势可以合理推断、但尚需更多 benchmark 支撑的工程判断。通过这种区分，可以避免将 bounded prototype 误写成 full paper implementation，也可以使论文结论更加稳健。

## 2.3 第三章 面向 OpenFHE/CKKS 的保守迁移思路

**本章目标**

解释为什么本文采用保守迁移策略，为什么不能直接把论文理论等同于 OpenFHE 工程实现，以及如何把中期 `t=2` 与当前 bounded `t=3` 正式版自然衔接。

**为什么不能直接等同**

- 论文中的高阶 raw ciphertext polynomial 是算法抽象。
- OpenFHE 用户层需要具体 API 支持、key degree、level/noiseScaleDeg 管理。
- scaling mode 行为需要测试。
- mixed-state addition 和 rescale 需要显式对齐。
- 因此必须先 smoke/survival，再 evaluator。

**为什么 bounded / conservative / white-box**

| 原因 | 写作角度 |
|---|---|
| CKKS 状态敏感 | 保守边界提高可验证性 |
| 本科毕设周期有限 | 固定 prototype 更适合论文初稿 |
| 后续还要迭代代码 | 白盒 trace 和 stats 便于扩展 |
| 避免过度泛化 | bounded claim 更可信 |

**从中期 `t=2` 到当前 `t=3` 的解释**

中期 `t=2` 是“验证浅层 delayed materialization 合法性”的阶段；当前 `t=3` 是在 `t=2`、degree-8、Internal BSGS 等稳定后，对论文核心方向的进一步推进。不要写成推翻中期目标，而应写成“从保守可行性验证到更接近论文核心结构的自然扩展”。

**半成品段落草稿**

上游 asymmetric BSGS 论文提供了重要的算法启发，但将其迁移到 OpenFHE/CKKS 用户层时，不能简单地把论文中的抽象中间态直接视为工程实现中可安全操作的对象。论文中讨论的 delayed materialization 和高阶 ciphertext polynomial 在理论层面具有明确含义，但 OpenFHE 用户层实际暴露的是具体 ciphertext 对象及其 level、noiseScaleDeg、ciphertext elements、relinearization key degree 等状态。一个中间 raw product 是否可以继续参与运算，多个不同状态的 ciphertext 是否可以相加，某种 scaling mode 下 rescale 后的 level 是否符合预期，都需要通过具体实验验证。

因此，本文采用保守迁移策略。该策略包含三个关键词：bounded、conservative 和 white-box。bounded 表示本文只在当前仓库已经验证的有限 degree、有限 component set 和有限 coefficient pattern 上讨论结果，不声称支持任意多项式。conservative 表示实现优先保留 eager 对照路径和明确 materialization 边界，不贸然传播未验证的高阶 raw intermediate。white-box 表示每个 evaluator 都尽量暴露 plaintext shadow、trace rows、operation counters 和 error reports，使实验结果不仅能显示最终是否正确，还能解释为什么 relin/rescale 计数发生变化。

这种策略首先体现在项目早期的 smoke 和 survival 测试中。仓库并未直接实现完整 asymmetric BSGS，而是先验证 OpenFHE/CKKS 用户层的基础行为，包括 `EvalMultNoRelin` 产生的 raw product 是否可被 materialize，不同 scaling mode 下 rescale 和 level 行为是否一致，以及 mixed-state addition 是否需要显式对齐。这些验证说明，论文级算法思想在进入 CKKS 用户层前必须经过工程合法性检查。

在中期阶段，项目以 `t=2` shallow-lazy 和固定小型 decomposition 作为主要目标。这一选择是合理的，因为 `t=2` 的 raw product 层次较浅，更适合在 CKKS 用户层验证 delayed materialization 的基本机制。相关实验显示，在固定 `t=2` component decomposition 中，grouped-lazy 路径保持 tensor-product 数量不变，同时减少 relin/rescale 次数。这一阶段的意义不是完成论文算法，而是建立“materialization reduction 可以被 OpenFHE/CKKS 用户层表达”的最小可行证据。

随着 degree-8 baseline、`t=2/t=3` fixed prototypes、plan object、generated-plan executor 和 explicit Internal BSGS 结构逐步稳定，项目自然进入 bounded `t=3` 阶段。与 `t=2` 相比，`t=3` 更接近上游论文中连续 tensor product 和 aggressive delayed materialization 的核心思想，但也更容易触发 CKKS 参数和 approximation 边界。因此，当前实现仍然保持 bounded：degree<=31，固定 outer base 为 8，固定 `S2={1,x^8,x^16,x^24}`，并限制 heavy nonconstant terms 的数量。这样的设计既保留了论文思想的核心方向，又避免将尚未验证的 arbitrary planner 或 full outer lazy 写入结论。

当前正式版 `ckks_evaluator_50_bounded_t3_degree31_internal_bsgs` 正是在这一迁移策略下形成的。它不是一个 full paper-strength asymmetric BSGS implementation，而是一个 degree-31 bounded `t=3` explicit Internal BSGS prototype。其价值在于，它把 component decomposition、outer block、inner lazy execution、plaintext validation 和 operation counters 组合到同一个可运行目标中，并在两个固定 cases 上验证了 relin/rescale 减少而 tensor count 不变的结构性结果。由此，本文可以诚实地说明：当前工作完成的是论文核心 evaluator-level 工程思想的保守迁移，而不是完整算法复现。

## 2.4 第四章 系统设计与实现演化

**本章目标**

按技术里程碑组织实现，而不是写开发流水账。重点说明每一步解决了什么工程问题，如何支撑最终 `50` 正式版。

**推荐小节划分**

1. 4.1 总体架构与指标体系  
2. 4.2 smoke/survival：OpenFHE/CKKS 用户层合法性验证  
3. 4.3 degree-8 V1：eager/lazy baseline  
4. 4.4 fixed `t=2/t=3` asymmetric prototypes  
5. 4.5 plan object 与 generated-plan executor  
6. 4.6 explicit Internal BSGS 结构  
7. 4.7 bounded degree-31 `t=3` formal version  

**milestone 一句话学术定位**

| Milestone | 学术定位 |
|---|---|
| smoke/survival | 将论文抽象操作映射到 OpenFHE 用户层前的合法性验证 |
| degree-8 V1 | 建立可复用 eager/lazy 对照 baseline |
| `20-25` | 以固定手写结构验证 asymmetric component decomposition 与 grouped-lazy |
| `26-33` | 从 hand-written evaluator 推进到 bounded planner-facing executor |
| `34-43` | 将 Internal BSGS 拓扑显式化 |
| `44-47` | 验证 bounded `t=3` raw-chain 与边界 |
| `50` | 当前论文初稿正式实现版本 |

**适合点名的源码**

- `include/ckks_lazy_poly_evaluator.h`
- `src/evaluator/ckks_lazy_poly_evaluator.cpp`
- `include/internal_bsgs_common.h`
- `include/internal_bsgs_ckks_basic.h`
- `src/evaluator/ckks_evaluator_46_bounded_t3_lazy_evaluator.cpp`
- `src/evaluator/ckks_evaluator_50_bounded_t3_degree31_internal_bsgs.cpp`
- `src/benchmark/ckks_benchmark_02_bounded_t3_runtime.cpp`

**建议图表**

- 表：技术里程碑与验证结果。
- 图：eager vs lazy materialization 流程。
- 图：bounded degree-31 `t=3` Internal BSGS topology。
- 表：源码模块映射表。

**半成品段落草稿**

本文实现采用渐进式原型路线。其核心思想不是一次性实现完整 asymmetric BSGS，而是将上游论文中的 materialization-reduction 方向拆解为一系列可以在 OpenFHE/CKKS 用户层验证的工程问题。每个阶段都保留明确的 eager 对照路径、lazy 或 grouped-lazy 路径、plaintext shadow、operation counters 和误差检查。这样做的目的，是使最终结果不仅能够运行，而且能够解释 relin/rescale 次数为什么发生变化。

第一阶段是 smoke 与 survival 验证。该阶段的主要任务是确认 OpenFHE/CKKS 用户层中基础操作的可用边界，包括 raw `EvalMultNoRelin` product 的生成与 materialization、手动 relin/rescale、level 和 noiseScaleDeg 的变化，以及不同 scaling mode 下的行为。该阶段不直接构成论文主算法，但它为后续 lazy evaluator 提供了必要的工程合法性依据。特别是，对于 delayed materialization 而言，raw intermediate 是否能在当前参数下继续参与计算，是必须先验证的问题。

第二阶段是 degree-8 V1 baseline。该阶段抽象出 `EvalRestrictedDegree8`，支持 degree<=8 的受限多项式求值，并同时执行 `expanded-eager` 和 `grouped-lazy` 两条路径。其设计中包含 `TraceRow`、`EvalStats`、`EvalResult` 等结构，用于记录 level、noiseScaleDeg、ciphertext elements、tensorProducts、relinCount 和 rescaleCount。degree-8 V1 的意义在于建立一个稳定的白盒 evaluator baseline。实验中，dense degree-8 case 保持 tensor products `12 vs 12`，同时 relin/rescale 从 `12` 降到 `6`，并通过 36 个 coefficient pattern regression cases。

第三阶段进入固定 asymmetric prototypes，对应 `ckks_evaluator_20` 至 `25`。这一阶段开始显式引入 component decomposition。`20` 采用固定 `t=2,B=4` 结构，验证 grouped-lazy 在小型 decomposition 中可以减少 materialization；`21` 验证 `t=3` raw chain survival；`22` 将多个 raw degree-3 products 先 fold 再 materialize；`23-25` 则加入固定 polynomial evaluator 和 outer assembly。该阶段的共同结果是 tensor-product 数量保持不变，而 relin/rescale 计数明显减少。它说明论文中的 materialization-reduction 思想不仅能在 degree-8 baseline 中出现，也能在更接近 asymmetric BSGS 的固定结构中出现。

第四阶段是 planner-facing line，对应 `26-33`。这一阶段的目标不是实现 arbitrary planner，而是将原本手写 evaluator shape 表示为显式 plan object。`26` 验证 plan data model，`27-28` 连接 plan-driven executor，`29-30` 引入 whitelisted planner 和 coefficient-pattern planner，`31` 验证 exponent decomposition，`32-33` 形成 generated-plan executor。该阶段建立了从 coefficients 到 decomposition，再到 generated plan 和 eager/lazy executor 的有限路径。它为后续 Internal BSGS plan generation 提供了数据结构基础。

第五阶段是 explicit Internal BSGS line，对应 `34-43`。在此之前，仓库已经有 grouped outer/block folds，但 Internal BSGS 的结构尚未被独立显式化。`34-35` 首次引入 `bar(S1)={x,x^2,x^3}`、`hat(S1)={x^4,x^8,x^12}` 和 `S2={x^16}`，并以 `t=2,B=4` 的固定结构执行 eager 与 inner-lazy 对照。随后 `41-43` 进一步将 explicit Internal BSGS 拓扑从单例扩展到 two-case 和 bounded generated explicit plans。实验结果稳定显示 tensor products `13 vs 13`，而 relin/rescale 从 `13` 降到 `9`。这一阶段的意义在于，仓库第一次拥有可以明确指认为 Internal BSGS-shaped 的 evaluator 结构。

第六阶段是 bounded `t=3` mainline，对应 `44-50`。`44` 验证固定 `t=3` component-set decomposition 的 eager 可行性；`45` 通过 probe-local context 和 `MaxRelinSkDeg(4)` 验证小型 `t=3` lazy raw-chain 的合法性；`46` 将其提升为 bounded `t=3` lazy evaluator；`47` 增加 pattern boundary 检查；`48` 进行 runtime benchmark；`49` 将 explicit Internal BSGS 与 bounded `t=3` 结构结合，但 degree<=15 的场景收益较弱。最终，`50` 将结构扩展到 degree<=31，固定 outer base 为 8，并使用 `S2={1,x^8,x^16,x^24}`。在两个 degree-31 cases 中，inner-lazy 路径保持 tensor-products 不变，同时将 relin/rescale 各减少 4 次。

因此，当前正式实现版本应定位为 bounded degree-31 `t=3` explicit Internal BSGS prototype。它继承了早期 smoke/survival 的合法性验证、degree-8 baseline 的可观测性设计、generated-plan line 的 plan validation 思路，以及 Internal BSGS line 的 explicit topology。其结果足以支撑论文初稿中的实现与实验章节，但仍应明确其固定边界：不支持 arbitrary polynomial planner，不自动选择 `t/B`，不实现 full outer aggressive lazy，也不涉及 CKKS bootstrapping。

## 2.5 第五章 实验设计

**本章目标**

把实验写成“机制归因型”：不是单纯跑分，而是验证 delayed materialization 是否在固定 CKKS evaluator 中减少 relin/rescale，同时保持 tensor count 与数值正确性。

**研究问题 RQ 建议**

| RQ | 问题 |
|---|---|
| RQ1 | 在 bounded degree-31 `t=3` explicit Internal BSGS 中，inner-lazy 是否保持 tensor-product count 不变？ |
| RQ2 | inner-lazy 是否减少 relin/rescale 计数？ |
| RQ3 | delayed materialization 是否保持 CKKS 解密误差低于阈值？ |
| RQ4 | 不同 scaling mode 下结论是否一致？ |
| RQ5 | operation-count reduction 是否能直接推出 runtime speedup？ |

**实验对象/对照组**

| 实验对象 | 对照路径 |
|---|---|
| `degree31_spread_blocks` | eager vs inner-lazy |
| `degree31_low7_blocks` | eager vs inner-lazy |
| bounded t=3 A/B/C | eager vs t3-lazy |
| degree-8 baseline | expanded-eager vs grouped-lazy |
| runtime benchmark | eager vs t3-lazy wall-clock |

**评估指标**

- tensor products
- relin count
- rescale count
- scalar multiplication count
- level-align count
- add count
- final max abs error
- scaling mode
- exploratory runtime median/mean

**继承中期报告 2×2×2 思路**

可以写成“实验维度化思想被保留，但具体实验对象随实现演化更新”。例如：

- 2 paths：eager vs lazy / inner-lazy。
- 2 scaling modes：`FIXEDMANUAL` vs `COMPOSITESCALINGMANUAL`。
- 2 main cases：`degree31_spread_blocks` vs `degree31_low7_blocks`。

这样保留了中期“成对对照”的方法论，同时不强行套用旧代码结构。

**正文应写明的参数**

- degree bound：`<=31`
- outer base：8
- `S2={1,x^8,x^16,x^24}`
- inner components：`{x}`, `{x^2}`, `{x^4}`
- scaling modes：`FIXEDMANUAL`, `COMPOSITESCALINGMANUAL`
- error threshold：`1e-8`
- `MaxRelinSkDeg(4)` for bounded t=3 line
- runtime benchmark：warmup/repeat 小，exploratory

**半成品段落草稿**

本文实验设计的目标不是单纯比较运行时间，而是验证一种 evaluator-level 机制：在 OpenFHE/CKKS 用户层中，通过 delayed materialization 或 lazy raw-chain 是否能够在保持 tensor-product 数量不变的情况下减少 relin/rescale 操作，并保持最终解密结果的数值正确性。因此，本文采用机制归因型实验设计，将操作计数、误差验证和有限 runtime benchmark 分开讨论。

实验的核心对照是 eager path 与 lazy path。eager path 在每个密文乘法后立即进行 relinearization 和 rescale，代表保守 materialization 策略；lazy 或 inner-lazy path 则使用 `EvalMultNoRelin` 生成 raw product，在局部 block 内延迟 materialization，并在聚合后统一执行 relin/rescale。两条路径计算相同的 plaintext polynomial，并使用相同输入向量和相同 coefficient pattern。这样可以将差异集中归因于 materialization 策略，而不是多项式结构或输入数据差异。

当前主实验对象是 bounded degree-31 `t=3` explicit Internal BSGS prototype。该原型固定 degree bound 为 `<=31`，outer base 为 8，outer set 为 `S2={1,x^8,x^16,x^24}`，inner components 为 `{x}`、`{x^2}` 和 `{x^4}`。实验选择两个固定 coefficient patterns：`degree31_spread_blocks` 和 `degree31_low7_blocks`。前者覆盖多个 outer blocks，后者采用较规则的 low-power pattern。二者都属于当前 bounded evaluator 可接受的范围。

本文主要评估五类指标。第一是 tensor products，用于观察 lazy path 是否改变非标量乘法数量。第二是 relin count 和 rescale count，用于衡量 materialization 风格操作的变化。第三是 scalar multiplication、level-align 和 add count，用于辅助解释不同路径中可能出现的额外操作。第四是 final max abs error，即 CKKS 解密结果与 plaintext shadow 之间的最大绝对误差，用于验证数值正确性。第五是 wall-clock runtime，但仅作为探索性指标，不作为主要结论依据。

实验同时覆盖 `FIXEDMANUAL` 和 `COMPOSITESCALINGMANUAL` 两种 scaling mode。这一设计继承了中期报告中强调的成对对照思想：同一 evaluator、同一 coefficient pattern、同一路径，在不同 scaling mode 下分别运行，以观察结论是否稳定。中期阶段的 `2×2×2` 思路可以在本文中转化为三组维度：两种执行路径、两种 scaling mode、两个正式 degree-31 cases。这样既保留了中期实验设计的逻辑，又与当前代码中的正式实验对象保持一致。

除主实验外，本文还保留若干辅助实验。degree-8 V1 用于说明 eager/lazy baseline 的形成；bounded `t=3` lazy evaluator 用于说明 `t=3` raw-chain 在 degree<=15 边界内已经获得验证；explicit Internal BSGS `t=2` line 用于说明 Internal BSGS topology 的形成过程；runtime benchmark 用于说明 operation-count reduction 与 wall-clock speedup 之间不能简单等同。通过这种设计，本文实验既能突出当前正式版结果，又能保留实现演化过程中的关键证据。

## 2.6 第六章 实验结果与分析

**本章目标**

以当前最强证据 `50` 为主，展示 bounded degree-31 `t=3` explicit Internal BSGS 的操作计数与误差结果；再用 bounded `t=3` earlier evidence 和 runtime mixed signal 做辅助分析。

**strongest results 先写**

1. `degree31_spread_blocks`
   - `FIXEDMANUAL`: tensor `20 vs 20`, relin/rescale `20 vs 16`
   - `COMPOSITESCALINGMANUAL`: 同样 `20 vs 16`
2. `degree31_low7_blocks`
   - 两种 mode 下 tensor `16 vs 16`, relin/rescale `16 vs 12`
3. final error 全部 `~e-14`，低于 `1e-8`

**主表建议**

| Case | Mode | Tensor | Relin | Rescale | Eager err | Lazy err |
|---|---|---:|---:|---:|---:|---:|
| degree31_spread_blocks | FIXEDMANUAL | 20 vs 20 | 20 vs 16 | 20 vs 16 | 6.63e-14 | 7.15e-14 |
| degree31_spread_blocks | COMPOSITE | 20 vs 20 | 20 vs 16 | 20 vs 16 | 5.05e-14 | 6.39e-14 |
| degree31_low7_blocks | FIXEDMANUAL | 16 vs 16 | 16 vs 12 | 16 vs 12 | 9.16e-14 | 7.14e-14 |
| degree31_low7_blocks | COMPOSITE | 16 vs 16 | 16 vs 12 | 16 vs 12 | 6.52e-14 | 7.12e-14 |

**辅助表**

- bounded t=3 earlier evidence：A/B/C cases。
- Internal BSGS `t=2`: `13 vs 9`。
- degree-8 baseline：`12 vs 6`。
- runtime mixed table：只放正文分析，不放摘要。

**runtime mixed signal 写法**

可写：

> runtime benchmark 显示，operation-count reduction 并不自动转化为所有小型 cases 中稳定的 wall-clock speedup。该结果说明本文主要结论应限定为 operation-count 层面的结构性减少，而 runtime 性能仍需更严格 benchmark 支撑。

**只能保守说的话**

- “在当前 bounded cases 中观察到……”
- “结果支持 materialization-reduction 方向……”
- “尚不能推广到 arbitrary polynomial……”
- “runtime 结果具有探索性……”

**半成品段落草稿**

本章首先分析当前正式版 bounded degree-31 `t=3` explicit Internal BSGS prototype 的实验结果。该原型对应 `ckks_evaluator_50_bounded_t3_degree31_internal_bsgs`，固定 degree bound 为 `<=31`，outer base 为 8，并使用 `S2={1,x^8,x^16,x^24}`。实验包含两个 accepted coefficient patterns：`degree31_spread_blocks` 和 `degree31_low7_blocks`。每个 case 分别在 `FIXEDMANUAL` 和 `COMPOSITESCALINGMANUAL` 两种 scaling mode 下运行，并比较 eager 与 inner-lazy 两条执行路径。

在 `degree31_spread_blocks` 中，两个 scaling mode 下 tensor-product count 均保持 `20 vs 20`。这说明 inner-lazy 路径并没有通过减少非标量乘法数量获得优势，而是保持了相同的乘法结构。与此同时，relin count 和 rescale count 均从 `20` 降至 `16`，减少 4 次。在 `FIXEDMANUAL` 下，eager final error 为 `6.632715e-14`，inner-lazy final error 为 `7.154520e-14`；在 `COMPOSITESCALINGMANUAL` 下，eager final error 为 `5.053596e-14`，inner-lazy final error 为 `6.388640e-14`。所有误差均远低于当前实验阈值 `1e-8`。

在 `degree31_low7_blocks` 中，结果同样保持稳定。两个 scaling mode 下 tensor-product count 均为 `16 vs 16`，而 relin/rescale count 均由 `16` 降至 `12`。在 `FIXEDMANUAL` 下，eager final error 为 `9.163850e-14`，inner-lazy final error 为 `7.144459e-14`；在 `COMPOSITESCALINGMANUAL` 下，eager final error 为 `6.519438e-14`，inner-lazy final error 为 `7.117570e-14`。该结果说明，在第二个固定 pattern 中，inner-lazy 同样能够在保持 tensor-product 数量不变的前提下减少 materialization 操作。

这两个主实验结果是本文最核心的 repo-level validated fact。它们支持如下结论：在当前 bounded degree-31 `t=3` explicit Internal BSGS prototype 中，delayed materialization / inner-lazy execution 能够在固定 cases 中减少 relin/rescale 计数，同时保持 CKKS 数值误差可控。该结论与上游 asymmetric BSGS 论文强调的 materialization-reduction 方向一致，但仍应限定在当前仓库验证范围内。它不等价于 full asymmetric BSGS algorithm，也不意味着已经实现 arbitrary planner。

为了说明正式版结果并非孤立出现，本文还可以引用 earlier bounded `t=3` evidence。`ckks_evaluator_46_bounded_t3_lazy_evaluator` 在 degree<=15 的 bounded heavy-safe cases 中已经显示出同样方向：例如 `bounded_t3_heavy_A` 中 tensor products 保持 `15 vs 15`，relin/rescale 从 `15` 降至 `10`；`bounded_t3_heavy_B` 中 tensor products 保持 `14 vs 14`，relin/rescale 从 `14` 降至 `10`。这些结果说明，在进入 degree-31 formal version 前，仓库已经对 `t=3` raw-chain 和 bounded evaluator 进行了逐步验证。

此外，explicit Internal BSGS `t=2,B=4` line 也提供了结构支撑。在 `ckks_evaluator_42_internal_bsgs_two_cases` 和 generated explicit Internal BSGS 结果中，两个固定 plans 均显示 tensor products `13 vs 13`，relin/rescale 从 `13` 降至 `9`。该阶段的意义不在于最终性能最强，而在于首次将 `bar(S1)`、`hat(S1)` 和 `S2` 的 Internal BSGS topology 显式化，为后续 bounded `t=3` explicit Internal BSGS 提供实现结构。

runtime benchmark 的结果需要单独、保守地分析。bounded `t=3` runtime benchmark 显示，operation-count reduction 是稳定的，但 wall-clock runtime 呈 mixed signal。例如在某些 `FIXEDMANUAL` 或 `COMPOSITESCALINGMANUAL` cases 中，lazy path 更快；而在另一些小型 cases 中，lazy path 接近或慢于 eager path。这说明 relin/rescale count 的减少是结构性结果，但不能直接推出所有小型多项式上稳定的运行时间加速。造成这种现象的可能原因包括 scalar multiplication、level alignment、OpenFHE 内部实现、内存行为以及 scaling mode 差异。由于当前 benchmark repeat 较少，且未进行 CPU pinning 或系统噪声控制，本文不应把 runtime 结果写成最终性能结论。

综上，实验结果支持本文的核心机制判断：在 OpenFHE/CKKS 用户层，通过 bounded explicit Internal BSGS 和 delayed materialization，可以在固定 degree-31 cases 中减少 relin/rescale 操作，而不减少 tensor-product 数量，并保持最终数值误差可控。该结果说明论文中的 materialization-reduction 思想具有用户层工程迁移价值，但当前结论仍局限于 conservative, bounded, repo-local prototype。

## 2.7 第七章 总结与展望

**本章目标**

总结贡献、诚实说明局限，并自然引出继续做完整版 asymmetric BSGS / planner / benchmark 的未来工作。

**贡献点建议**

1. 构建 OpenFHE/CKKS 用户层白盒 evaluator 原型体系。
2. 建立 eager/lazy 对照、plaintext shadow、stats/counters。
3. 完成从 smoke 到 bounded degree-31 `t=3` explicit Internal BSGS 的实现演化。
4. 在固定 degree-31 cases 中验证 relin/rescale 减少而 tensor count 不变。
5. 通过 runtime mixed signal 明确区分 operation-count reduction 与 wall-clock speedup。

**局限性写法**

不要写成“失败”，而写成“有意保守边界”：

- 当前不支持 arbitrary planner。
- 当前不自动选择 `t/B`。
- 当前主要是 inner-lazy，不是 full outer aggressive lazy。
- 当前 benchmark 规模仍小。
- 当前没有 CKKS bootstrapping 集成。

**后续工作**

- 扩展 bounded degree-31 cases。
- 统一 CSV 指标导出。
- 更严格 runtime benchmark。
- 探索 outer-lazy。
- 发展 general planner。
- 研究更接近论文完整算法的参数选择和 base-`B` decomposition。

**半成品段落草稿**

本文围绕 OpenFHE/CKKS 用户层多项式求值优化，构建了一个 conservative、bounded、repo-local 的 evaluator 原型体系。本文没有修改 OpenFHE 内核，也没有声称实现完整 asymmetric BSGS 算法，而是聚焦于上游论文中最具有工程迁移价值的思想：通过 delayed materialization / lazy raw-chain 减少 relin/rescale 等 materialization 风格操作。

在实现上，本文采用逐步验证路线。首先通过 smoke/survival 测试确认 OpenFHE/CKKS 用户层中 raw product、scaling mode、level alignment 等基础行为；随后建立 degree-8 eager/lazy baseline；再通过固定 `t=2/t=3` asymmetric prototypes 验证 component decomposition 与 grouped-lazy；之后推进到 plan object、generated-plan executor 和 explicit Internal BSGS；最终形成 bounded degree-31 `t=3` explicit Internal BSGS prototype。该过程体现了从合法性验证到结构化 evaluator，再到 bounded formal version 的工程演化。

实验结果表明，在当前正式版的两个 degree-31 fixed cases 中，inner-lazy 路径保持 tensor-product count 不变，同时将 relin/rescale 操作各减少 4 次，并保持最终 CKKS 解密误差低于 `1e-8` 阈值。该结果说明，在 OpenFHE/CKKS 用户层中，materialization-reduction 思想可以通过保守的 explicit Internal BSGS prototype 被有效表达和验证。

本文也明确区分了 operation-count reduction 与 runtime speedup。探索性 runtime benchmark 显示，尽管 relin/rescale 计数减少是稳定信号，但 wall-clock runtime 在小型 bounded cases 中呈现 mixed signal。因此，本文不将当前结果表述为稳定性能加速，而将其限定为 evaluator 结构层面的 materialization 操作减少。

当前工作的局限性同样明确。本文尚未实现 arbitrary polynomial planner，尚未自动选择 `t`、`B` 或 component sets，也未实现 full outer aggressive lazy 和 CKKS bootstrapping。当前实验仍集中在有限 degree 和有限 coefficient patterns 上。后续工作可以从三个方向展开：一是扩展 bounded degree-31 accepted cases 并统一 CSV 指标导出；二是设计更严格的 runtime benchmark；三是在已有 explicit Internal BSGS 结构基础上逐步探索更一般的 planner 和 outer-lazy 策略。通过这些扩展，本文的 conservative prototype 可以继续向更完整的 asymmetric BSGS evaluator 发展。

# 3. 摘要写作素材包

**中文摘要应包含的 5~7 个关键信息点**

1. CKKS 中高阶多项式求值重要，开销不只来自乘法次数。
2. 上游 asymmetric BSGS 论文启发本文关注 key/modulus-switch 风格 materialization 开销。
3. 本文在 OpenFHE/CKKS 用户层实现 conservative bounded prototype，不修改 OpenFHE 内核。
4. 构建 eager/lazy 对照、plaintext shadow、operation counters 和 trace。
5. 当前正式版为 bounded degree-31 `t=3` explicit Internal BSGS prototype。
6. 在两个 fixed degree-31 cases 中，tensor count 不变，relin/rescale 各减少 4 次。
7. runtime 仍是 mixed signal，后续需更严格 benchmark 和 planner 扩展。

**可以写进摘要的结果**

- degree31 两个 cases。
- tensor 不变。
- relin/rescale 减少 4。
- 误差低于 `1e-8`。
- OpenFHE/CKKS 用户层 bounded prototype。

**绝对不要写进摘要的结果**

- “完整实现 asymmetric BSGS”。
- “任意多项式 planner”。
- “稳定运行时间加速”。
- “支持 bootstrapping”。
- “达到论文 asymptotic switching complexity”。

**摘要骨架**

本文面向 OpenFHE/CKKS 用户层中的高阶多项式求值问题，研究如何将 asymmetric BSGS 论文中的 materialization-reduction 思想保守迁移到可运行、可追踪的 evaluator 原型中。不同于仅关注 tensor-product 数量的传统分析，本文重点关注 CKKS 工程实现中 relinearization 和 rescale 等 key/modulus-switch 风格操作。

本文不修改 OpenFHE 内核，而是在用户层构建 conservative、bounded、repo-local 的 evaluator 原型体系。实现过程中依次完成 smoke/survival 验证、degree-8 eager/lazy baseline、固定 `t=2/t=3` asymmetric prototypes、generated-plan executor、explicit Internal BSGS，以及 bounded degree-31 `t=3` explicit Internal BSGS prototype。系统通过 plaintext shadow、operation counters 和 CKKS 解密误差对 evaluator 行为进行白盒验证。

实验结果表明，在当前正式版的两个 fixed degree-31 cases 中，inner-lazy 路径保持 tensor-product count 不变，同时将 relin/rescale 操作各减少 4 次；最终最大绝对误差保持在 `1e-13` 量级，低于当前实验阈值 `1e-8`。结果说明，delayed materialization / lazy raw-chain 思想可以在 OpenFHE/CKKS 用户层的有限边界内被有效表达。本文同时指出，当前实现尚非完整 asymmetric BSGS 算法，也不支持任意多项式 planner；runtime benchmark 仍呈 mixed signal，后续需进一步扩展 planner 和严格性能评估。

# 4. 图表落位建议

| 图/表名称 | 放置位置 | 数据或证据来源 | 表达观点 |
|---|---|---|---|
| 表 1-1 本文工作边界 | 第一章 1.4 | `AGENTS.md`, forbidden claims | 明确不夸大 |
| 图 2-1 CKKS 乘法与 materialization 关系 | 第二章 2.1 | theory notes | 说明乘法不只是 tensor |
| 图 2-2 BSGS / asymmetric BSGS 结构示意 | 第二章 2.4 | paper notes | 引出 baby/giant asymmetry |
| 表 3-1 Paper-level vs Repo-level facts | 第三章 3.1 | Evidence Units | 避免混写 |
| 图 3-1 保守迁移路线 | 第三章 3.4 | 里程碑时间线 | t=2 到 t=3 自然演化 |
| 表 4-1 技术里程碑总表 | 第四章 4.1 | 里程碑时间线 | 展示实现演化 |
| 图 4-1 evaluator 总体 pipeline | 第四章 4.1 | Method design evidence | coeff -> plan -> executor |
| 图 4-2 bounded degree-31 t=3 Internal BSGS topology | 第四章 4.7 | `50` log/source | 当前正式版结构 |
| 表 4-2 源码模块映射 | 第四章 4.7 | 高价值文件清单 | 增强可追溯性 |
| 表 5-1 实验 RQ 与指标 | 第五章 5.1 | 实验设计 | 机制归因 |
| 表 6-1 degree-31 主结果 | 第六章 6.1 | final log | 最强结果 |
| 表 6-2 bounded t=3 支撑结果 | 第六章 6.2 | `bounded_t3_lazy...` | 主结果前置验证 |
| 表 6-3 runtime exploratory result | 第六章 6.4 | runtime docs/CSV | runtime mixed |
| 表 7-1 局限与后续工作 | 第七章 7.3 | gaps | future work |

# 5. 安全措辞模板

**表示有限验证成立**

- “在当前仓库验证的 bounded cases 中，实验结果显示……”
- “在固定 degree<=31、固定 outer base 的实现边界内，观察到……”
- “该结果支持本文的工程假设，但仍属于 repo-local validation。”

**表示当前仍有边界**

- “当前实现仍是 conservative, bounded prototype。”
- “该原型并不支持 arbitrary polynomial planning。”
- “本文没有修改 OpenFHE 内核，也不讨论 bootstrapping 集成。”

**表示不能直接推广**

- “该结论不能直接推广到任意多项式或任意参数设置。”
- “由于当前实验覆盖的是有限 coefficient patterns，仍需更多 benchmark 支撑一般性结论。”
- “本文将该结果限定为当前实现边界内的工程观察。”

**表示结果显示但不代表一般结论**

- “operation-count reduction 是稳定观察，但 wall-clock runtime 仍呈 mixed signal。”
- “该实验说明 relin/rescale 减少是结构性结果，但不能单独推出稳定加速。”
- “runtime 部分应视为探索性测量，而非最终性能结论。”

**表示 future work**

- “后续工作可在当前 explicit Internal BSGS 结构基础上扩展 planner。”
- “未来可进一步探索 outer-lazy materialization 策略。”
- “更严格的 runtime benchmark 需要更高 repeat、系统噪声控制和更接近应用的 polynomial cases。”

# 6. 风险段落清单

| 风险位置 | 容易写过头的说法 | 修正建议 |
|---|---|---|
| 摘要 | “实现了 asymmetric BSGS 算法” | 改为“实现了受 asymmetric BSGS 启发的 bounded evaluator prototype” |
| 绪论贡献 | “显著提升运行速度” | 改为“减少 relin/rescale 操作；runtime 仍需进一步评估” |
| 理论基础 | “论文方法已在 CKKS 中实现” | 改为“本文在 OpenFHE/CKKS 用户层保守迁移其 evaluator-level 思想” |
| 第三章中期衔接 | “中期目标变了” | 改为“从 `t=2` 合法性验证自然推进到 bounded `t=3` formal version” |
| 第四章实现 | “planner 支持任意 coefficients” | 改为“bounded/generated/whitelisted finite patterns” |
| 第六章结果 | “lazy path 更快” | 改为“operation-count reduction 稳定，runtime mixed” |
| Internal BSGS | “实现 full Internal BSGS” | 改为“explicit Internal BSGS-shaped topology in bounded cases” |
| t=3 | “实现 aggressive lazy” | 改为“局部 raw-chain / inner-lazy，outer assembly 仍保守” |
| OpenFHE | “优化 OpenFHE” | 改为“使用 OpenFHE 用户层 API 构建 evaluator” |
| 展望 | “只差工程化即可完整复现” | 改为“仍需 arbitrary planner、outer-lazy、参数选择和严格 benchmark” |
