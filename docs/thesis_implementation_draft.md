# 实现章节草稿：OpenFHE/CKKS 用户层的保守型非对称 BSGS 多项式求值原型

> 本文档是毕业论文实现章节的草稿素材，不是最终论文正文。
> 写作目标是把当前仓库已经完成并验证的工程工作，整理成可以进入论文的技术叙述。

## 1. 实现目标与工程边界

本文的工程目标并不是设计新的同态加密方案，也不是修改 OpenFHE 库内部的 CKKS 实现，而是在 OpenFHE/CKKS 用户层实验环境中，对论文《A Novel Asymmetric BSGS Polynomial Evaluation Algorithm under Homomorphic Encryption》中的多项式求值优化思想进行保守迁移和白盒验证。

论文关注的核心问题是 leveled FHE 中高阶多项式求值的代价。一次同态乘法在工程实现中并不只是一个抽象的乘法运算，它通常伴随 tensor product、relinearization/key-switching 以及 modulus switching/rescaling 等操作。论文的优化重点并不是首先降低 tensor product 的数量，而是利用 baby-step 与 giant-step 在深度上的不对称性，减少昂贵的 key-switch 和 modulus-switch 类型操作。

基于这一点，本文实现部分采用如下工程边界：

1. 只使用 OpenFHE 暴露的 CKKS 用户层 API。
2. 不修改 OpenFHE 内部源码。
3. 不直接实现论文完整版算法，而是构建固定、小规模、可追踪的原型。
4. 每个原型均保留 eager 对照路径和 lazy/grouped-lazy 路径。
5. 每个关键实验均维护 plaintext shadow，用于验证密文计算结果的数值正确性。
6. 重点记录 tensor product、relinearization、rescale 等操作次数，而不是只比较运行时间。

这种实现策略的目的，是先验证论文中的工程方向是否能在 OpenFHE/CKKS 用户层被安全表达，而不是过早追求完整泛化。

## 2. 实验环境与观测指标

实验代码位于独立的用户层工程 `fhe-evaluator` 中。该工程通过 CMake 构建，链接本地安装的 OpenFHE，并围绕 CKKS 多项式求值构建了一系列白盒 evaluator 原型。

所有核心实验均采用相同的观测方式：

- 对同一组输入明文槽位进行 CKKS 加密；
- 在密文上分别执行 eager evaluator 与 lazy/grouped-lazy evaluator；
- 使用明文 shadow 在每一步或最终步骤计算参考值；
- 解密结果并计算最大绝对误差；
- 记录每条路径的 tensor product、relinearization、rescale、scalar multiplication、level alignment 和 addition 次数；
- 输出 trace，包括 step name、level、noiseScaleDeg、ciphertext element count 和误差。

这种观测方式使实验不仅能判断最终结果是否正确，也能解释不同 evaluator 在 materialization 策略上的差异。

## 3. 基础 smoke 与 survival 验证

在构造多项式 evaluator 之前，工程首先进行了 OpenFHE/CKKS 用户层 API 的局部验证。这一阶段主要回答以下问题：

- `EvalMultNoRelin` 产生的 raw ciphertext 是否能在当前参数下继续参与后续计算；
- raw tensor product 何时需要 materialize；
- 不同 level 或 noiseScaleDeg 的 ciphertext 是否能安全相加；
- `FIXEDMANUAL` 与 `COMPOSITESCALINGMANUAL` 两种 scaling mode 下的行为是否一致；
- 手动 rescale、relinearize 和 level alignment 是否能被 trace 捕获。

这些 smoke/survival 实验的意义在于，它们避免了直接把论文中的抽象中间态等同于 OpenFHE 用户层中必然可用的对象。后续 evaluator 的设计都建立在这些局部验证之上。

## 4. Degree-8 V1：受限可复用 evaluator 基线

在基础 API 验证之后，工程首先构建了一个受限的 degree<=8 evaluator，即 `EvalRestrictedDegree8`。该 evaluator 支持固定次数和固定结构的多项式求值，并同时执行 eager 和 grouped-lazy 两条路径。

这一阶段的重点不是追求泛化，而是形成一个稳定的实验基线。该基线具备以下特征：

- 输入为显式 coefficient vector；
- evaluator 内部维护 plaintext input；
- 输出 eager/lazy 两条路径的 trace 和 stats；
- 支持 coefficient pattern regression；
- 支持 plan summary，用于观察 block 和 tail 的分布。

Degree-8 V1 的主要作用，是证明 grouped-lazy 策略在一个小型 CKKS 多项式 evaluator 中可以保持正确性，并减少 relin/rescale 次数。它也是后续非对称原型的保守对照面。

## 5. 固定非对称原型：从 grouped fold 到 outer assembly

在 degree<=8 基线稳定后，工程进入固定非对称原型阶段。该阶段对应仓库中的 `ckks_evaluator_20` 至 `ckks_evaluator_25`。

这一阶段逐步验证了以下结构：

1. 固定 `t=2` 的小型 decomposition；
2. `t=3` raw tensor chain 的 survival；
3. 多个 raw degree-3 product 的 grouped fold；
4. fixed polynomial evaluator；
5. 两个 grouped block 加一个 outer multiplier 的 outer assembly；
6. 第二个 outer pattern，用于验证结果不是单例。

这一阶段的共同实验信号是：

```text
tensor product 数量基本保持不变
relinearization/rescale 次数明显下降
```

例如，在固定 outer assembly 原型中，eager 路径与 grouped-lazy 路径的 tensor product 数量相同，但 relin/rescale 次数由 `18/18` 降低到 `6/6`。这与论文中“优化重点不是减少 tensor product，而是减少 materialization 相关操作”的方向一致。

需要强调的是，这一阶段仍然不是论文完整版 asymmetric BSGS。它只是以固定、可追踪的方式验证论文工程思想中的局部机制。

## 6. Planner-facing 原型：从手写结构到 generated plan

在固定 evaluator 原型之后，工程进一步把 evaluator 的结构显式化为 plan object。该阶段对应 `ckks_evaluator_26` 至 `ckks_evaluator_33`。

这一阶段的演化路径如下：

```text
手写 evaluator
  -> plan object smoke
  -> plan-driven executor
  -> plan table executor
  -> whitelisted planner
  -> coefficient-pattern planner
  -> exponent decomposition smoke
  -> generated-plan executor
```

这样做的目的，是将“怎么组织多项式求值结构”和“怎么执行 CKKS 运算”逐步分离。为了控制风险，planner 并没有直接泛化为任意多项式 planner，而是采用有限的 whitelisted patterns。

在 `ckks_evaluator_33_asym_generated_plan_patterns` 中，工程已经支持两个 known coefficient patterns，并通过固定 exponent decomposition 生成 plan，再交给 eager/lazy executor 执行。该阶段证明了 generated-plan 路径不是单个手写例子的偶然结果。

## 7. Internal BSGS 原型：显式引入 inner/outer 两层拓扑

前述 `20-33` 阶段已经证明 grouped-lazy 和 generated-plan 思路可行，但它们仍然没有显式实现论文结构中更核心的 Internal BSGS 子过程。因此工程进一步构建了 `ckks_evaluator_34_internal_bsgs_proto` 和 `ckks_evaluator_35_internal_bsgs_generated_plan`。

这一阶段固定采用教学型参数：

```text
t = 2
B = 4
bar(S1) = {x, x^2, x^3}
hat(S1) = {x^4, x^8, x^12}
S2 = {x^16}
```

其 evaluator 形状为：

```text
P(x) = inner0(x) + inner1(x) * x^16
```

其中每个 inner block 通过 Internal BSGS 形式计算：

```text
inner(x) = sum_j low_j(x) * high_j(x)
```

这里 `low_j(x)` 是 `bar(S1)` 上的线性组合，`high_j(x)` 从 `hat(S1)` 中选择。eager 路径会对每个 inner product 及时 materialize，而 inner-lazy 路径会先累加 raw inner products，再进行一次 materialization。outer assembly 目前仍采用保守 materialized 策略。

这一阶段的重要意义在于，工程第一次显式呈现了：

- `bar(S1)`；
- `hat(S1)`；
- `S2`；
- Internal BSGS 子过程；
- inner evaluator 与 outer assembly 的两层结构。

## 8. Internal BSGS 实验结果

当前 Internal BSGS 原型支持两个固定 coefficient patterns。`ckks_evaluator_34` 使用手写固定 plan，`ckks_evaluator_35` 则从 coefficient pattern 出发，通过固定 decomposition 生成 `InternalBsgsPlan`。

当前固定 decomposition 为：

```text
final_power = outer_power + low_power + high_power
outer_power in {0, 16}
low_power in {1, 2, 3}
high_power in {4, 8, 12}
```

在两个 coefficient patterns、两种 scaling mode 下，当前结果稳定表现为：

```text
tensor products: 13 vs 13
relin count:     13 vs 9
rescale count:   13 vs 9
```

其中前者为 eager 路径，后者为 inner-lazy 路径。所有最终解密误差均低于当前实验阈值 `1e-8`，plaintext skeleton 的 direct-vs-internal 误差处于普通浮点舍入量级。

这说明，在当前固定 Internal BSGS-shaped 原型中，lazy 策略并没有减少 tensor product 数量，但成功减少了 materialization 相关操作数量。这正是本文希望验证的保守迁移目标。

## 9. 代码组织

当前 Internal BSGS 相关代码采用轻量共享和实验局部执行器相结合的组织方式。

共享 helper 位于：

```text
include/internal_bsgs_common.h
```

该文件包含：

- `InnerTerm`
- `OuterBlock`
- `InternalBsgsPlan`
- `CoeffPattern`
- 两个 fixed Internal BSGS plans
- 两个 coefficient patterns
- fixed `t=2,B=4` decomposition helpers
- generated-plan validation helpers

CKKS executor 本身仍然保留在实验文件中：

```text
src/evaluator/ckks_evaluator_34_internal_bsgs_proto.cpp
src/evaluator/ckks_evaluator_35_internal_bsgs_generated_plan.cpp
```

这种组织方式是一个有意的工程折中。它减少了 `34/35` 中 plan 和 coefficient pattern 的重复，但没有在 deadline 前大规模抽象 executor，从而保护了已经验证过的实验行为。

## 10. 当前工作的准确定位

本文当前实现可以被准确描述为：

```text
一个固定、白盒、OpenFHE/CKKS 用户层的 Internal BSGS-shaped generated-plan prototype。
```

它已经验证：

- grouped-lazy materialization 在 CKKS 用户层可表达；
- fixed asymmetric outer assembly 可以正确执行；
- coefficient pattern 可以生成固定 plan；
- Internal BSGS 的 inner/outer 拓扑可以在 CKKS 上跑通；
- 在固定样例中，tensor product 数量不变，而 relin/rescale 次数下降。

但它还不能被描述为：

```text
完整实现了论文的 asymmetric BSGS 算法。
```

当前尚未完成：

- 任意多项式 planner；
- 自动选择 `t` 和 `B`；
- 完整 base-`B` 分解覆盖；
- aggressive lazy outer assembly；
- 高阶 ciphertext pipeline 的一般化支持；
- library-quality evaluator API。

这些内容应作为后续工作，而不是当前实现的硬性缺陷。

## 11. 可用于论文正文的表述

可以使用如下较安全的表述：

```text
本文在 OpenFHE/CKKS 用户层实现了一个保守型非对称 BSGS 多项式求值原型。该原型不修改 OpenFHE 内部实现，而是在用户层构建 eager 与 inner-lazy 两条可对照执行路径。实验结果表明，在固定 Internal BSGS-shaped 样例中，inner-lazy 路径保持了与 eager 路径相同的 tensor product 数量，同时将 relin/rescale 次数由 13 降低到 9，并保持最终 CKKS 解密误差低于实验阈值。
```

也可以进一步说明：

```text
该结果说明，论文中以减少 key-switch/modulus-switch 为核心的优化方向，可以在 OpenFHE/CKKS 用户层通过保守的 grouped-lazy materialization 原型得到验证。本文实现尚不声称覆盖完整论文算法，而是提供了一个可追踪、可解释、可复现实验框架。
```

不建议使用如下表述：

```text
本文完整实现了论文提出的 asymmetric BSGS 算法。
```

更准确的说法是：

```text
本文实现的是论文思想的受限工程迁移和原型验证。
```

## 12. 后续工作

后续工作可以沿三条路线推进：

1. **工程抽象**：将当前实验文件中的 CKKS executor 抽象为可复用模块，但仍保留 trace 和 plaintext shadow。
2. **planner 扩展**：在固定 `t=2,B=4` 的前提下，扩大可支持的 exponent range，并逐步从 whitelisted patterns 过渡到 bounded planner。
3. **lazy 策略增强**：在 inner-lazy 稳定后，探索 conservative outer-lazy，并评估其对误差和 level 消耗的影响。

在进入这些扩展之前，应优先保持当前 `34/35` 的可复现性，因为它们已经构成本文实现部分最重要的主体实验。
