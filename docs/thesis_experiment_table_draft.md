# 实验结果表格草稿与运行时间计量计划

> 本文档用于整理论文实验结果章节的表格草稿。
> 当前表格主要基于已经归档的日志和结果文档。真实运行时间实验尚未执行，本文档只给出计量方案，不填入未验证数据。

## 1. 实验指标说明

本文实验不只关注最终解密误差，也关注同态求值过程中的操作结构。原因是论文《A Novel Asymmetric BSGS Polynomial Evaluation Algorithm under Homomorphic Encryption》的优化目标并不是单纯减少 tensor product，而是减少 key-switch / modulus-switch 类型的 materialization 开销。

因此，当前实验表格主要包含以下指标：

- `tensor products`：非标量密文乘法产生的 tensor product 次数；
- `relin count`：relinearization 次数；
- `rescale count`：rescale 或 modulus-switch 类操作次数；
- `final max abs err`：最终解密结果与 plaintext shadow 的最大绝对误差；
- `scaling mode`：CKKS scaling 技术参数；
- `pattern/case`：当前固定原型使用的 coefficient pattern 或 evaluator case。

目前仓库已经记录了每个关键实验的 trace 和 stats，但真实 wall-clock runtime 仍需要单独 benchmark。不能仅凭 relin/rescale 次数减少就直接声称实际运行时间一定下降。

## 2. Degree-8 V1 基线结果

Degree-8 V1 是第一个可复用的受限 evaluator 基线。它支持 eager 与 grouped-lazy 成对执行，并带有 coefficient-pattern regression。

论文中可使用的表格形式如下：

| Evaluator | Degree bound | Case count | Main observation |
| --- | ---: | ---: | --- |
| Degree-8 V1 | `<= 8` | coefficient pattern regression | grouped-lazy 在受限小多项式上减少 materialization 次数，并保持误差可控 |

建议在最终论文中补充来自 `docs/degree8_v1_results.md` 的具体 case 表格。当前章节重点可以把它作为“后续非对称原型的稳定基线”，而不必把所有 regression case 都塞进正文。

## 3. 固定非对称原型 20-25

这一组实验验证从 grouped fold 到 outer assembly 的固定非对称结构。它们不是通用 evaluator，但逐步接近论文的 materialization-reduction 思路。

| Target | Structure | Tensor products | Relin count | Rescale count | Interpretation |
| --- | --- | ---: | ---: | ---: | --- |
| `22` | grouped `t=3` fold | 11 vs 11 | 11 vs 4 | 11 vs 4 | raw degree-3 terms 可先 fold 再 materialize |
| `23` | fixed polynomial evaluator | 11 vs 11 | 11 vs 4 | 11 vs 4 | grouped block 可进入小型 polynomial evaluator |
| `24` | fixed outer assembly | 18 vs 18 | 18 vs 6 | 18 vs 6 | 两个 grouped blocks 加 outer product 仍保持 switch reduction |
| `25` | second outer pattern | 16 vs 16 | 16 vs 6 | 16 vs 6 | 第二个 pattern 证明结果不是单例 |

这些结果支持如下结论：

```text
固定非对称原型中，lazy/grouped-lazy 路径没有减少 tensor product 数量，但减少了 relin/rescale 次数。
```

该结论与论文优化方向一致，但仍应表述为“保守原型验证”，不能表述为完整算法实现。

## 4. Generated-plan 原型 32-33

`32-33` 的作用是把固定求值结构推进到 planner-facing 形式。`32` 先验证单 pattern generated plan，`33` 扩展到两个 known coefficient patterns。

| Pattern | Scaling mode | Tensor products | Relin count | Rescale count | Eager final err | Lazy final err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `coeff_pattern_outer_assembly_24` | FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 1.793469e-13 | 5.247376e-14 |
| `coeff_pattern_outer_assembly_24` | COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 5.282222e-14 | 3.197679e-10 |
| `coeff_pattern_outer_pattern_25` | FIXEDMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 4.055698e-14 | 6.326205e-14 |
| `coeff_pattern_outer_pattern_25` | COMPOSITESCALINGMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 4.821767e-14 | 2.007717e-10 |

可以在论文中说明：

```text
Generated-plan 原型说明，固定 coefficient pattern 可以先生成求值 plan，再由同一 executor 执行 eager/lazy 对照。这一步将手写 evaluator 推进到了受限 planner-facing 形式。
```

## 5. Internal BSGS 原型 34-35

`34-35` 是当前最接近论文主体拓扑的实验线。它显式引入：

- `bar(S1) = {x, x^2, x^3}`
- `hat(S1) = {x^4, x^8, x^12}`
- `S2 = {x^16}`
- Internal BSGS block evaluation
- eager / inner-lazy CKKS 对照

固定参数为：

```text
t = 2
B = 4
```

当前 `35` 的 generated-plan 实验结果如下：

| Pattern | Scaling mode | Tensor products | Relin count | Rescale count | Eager final err | Inner-lazy final err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `coeff_pattern_internal_t2_b4_a` | FIXEDMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 3.510376e-13 | 3.830690e-13 |
| `coeff_pattern_internal_t2_b4_a` | COMPOSITESCALINGMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 5.042757e-14 | 4.085794e-14 |
| `coeff_pattern_internal_t2_b4_b` | FIXEDMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 2.166109e-13 | 2.447759e-13 |
| `coeff_pattern_internal_t2_b4_b` | COMPOSITESCALINGMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 3.470928e-14 | 3.620760e-14 |

这一组结果可以作为论文实现章节的核心实验表。它说明当前 prototype 已经不只是 grouped fold，而是具备显式 Internal BSGS 拓扑。

## 6. 当前实验结论

从当前已归档结果可以得到三个保守结论：

1. 在固定 grouped-lazy 和 Internal BSGS-shaped 原型中，lazy 路径保持了与 eager 路径相同的 tensor product 数量。
2. lazy 路径减少了 relin/rescale 次数，典型结果包括 `18 -> 6`、`16 -> 6`、`13 -> 9`。
3. 在当前 CKKS 参数和输入槽位下，最终解密误差低于实验阈值 `1e-8`。

这些结论足以支持“论文 materialization-reduction 思路可以在 OpenFHE/CKKS 用户层被保守验证”这一说法。

但当前还不能支持如下说法：

```text
真实运行时间一定下降。
```

真实运行时间必须单独测量。

## 7. 为什么需要单独的运行时间计量模块

理论上，relinearization 和 rescale/key-switch 相关操作通常是 leveled FHE 中较昂贵的部分。因此，当 lazy 路径显著减少 relin/rescale 次数时，可以合理预期它具有运行时间优势。

但是在论文实验部分，不能把这种预期直接写成实测结论。原因包括：

1. OpenFHE 内部实现可能包含缓存、内存分配、NTT/INTT、lazy reduction 等复杂因素；
2. 当前 prototype 的 eager 与 lazy 路径可能有不同的 level alignment 和 scalar multiplication 开销；
3. 当前 trace 中的 `total operator sec` 是开发期局部计时，不一定适合最终统计；
4. 单次运行时间波动较大，需要多轮重复和汇总；
5. 不同 scaling mode 的时间表现可能不同。

因此，最终实验需要一个独立 benchmark 模块。

## 8. 建议的 timing benchmark 设计

建议新增独立 target，例如：

```text
ckks_benchmark_01_internal_bsgs_runtime
```

它不应替代现有 white-box evaluator，而应调用或复用当前已验证的 evaluator 逻辑，专门负责运行时间计量。

建议 benchmark 输出：

- evaluator name
- case/pattern name
- scaling mode
- warmup 次数
- repeat 次数
- eager total runtime
- lazy total runtime
- eager median runtime
- lazy median runtime
- eager mean runtime
- lazy mean runtime
- eager/lazy speedup ratio
- tensor/relin/rescale stats
- final error

建议采用如下运行策略：

```text
warmup = 2 或 3
repeat = 10 或 20
每次重新执行 evaluator 主体
keygen/encryption 是否计入时间需要明确分组
```

为了更清楚地做消融，建议至少分两类时间：

1. **end-to-end evaluator time**
   包含 basis construction、block evaluation、outer assembly，但不包含 key generation。

2. **core evaluation time**
   在相同 context、相同 key、相同 input ciphertext 下，重复执行 eager/lazy evaluator 主体。

论文中优先报告 core evaluation time，因为本文关注的是 evaluator 结构差异，而不是 key generation 成本。

## 9. 建议的消融实验表

最终可以设计如下表格：

| Case | Scaling mode | Path | Tensor | Relin | Rescale | Median time | Mean time | Final err |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Internal BSGS A | FIXEDMANUAL | eager | 13 | 13 | 13 | TBD | TBD | TBD |
| Internal BSGS A | FIXEDMANUAL | inner-lazy | 13 | 9 | 9 | TBD | TBD | TBD |
| Internal BSGS A | COMPOSITESCALINGMANUAL | eager | 13 | 13 | 13 | TBD | TBD | TBD |
| Internal BSGS A | COMPOSITESCALINGMANUAL | inner-lazy | 13 | 9 | 9 | TBD | TBD | TBD |
| Internal BSGS B | FIXEDMANUAL | eager | 13 | 13 | 13 | TBD | TBD | TBD |
| Internal BSGS B | FIXEDMANUAL | inner-lazy | 13 | 9 | 9 | TBD | TBD | TBD |

如果时间允许，也可以加入 `33` 的 generated outer patterns：

| Case | Path | Tensor | Relin | Rescale | Median time | Mean time |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| outer assembly 24 | eager | 18 | 18 | 18 | TBD | TBD |
| outer assembly 24 | grouped-lazy | 18 | 6 | 6 | TBD | TBD |
| outer pattern 25 | eager | 16 | 16 | 16 | TBD | TBD |
| outer pattern 25 | grouped-lazy | 16 | 6 | 6 | TBD | TBD |

其中 `TBD` 必须等真实 benchmark 完成后再填。

## 10. 当前阶段论文中应如何表述运行时间

在没有真实 benchmark 之前，可以写：

```text
由于 relinearization 和 rescale 通常是 CKKS 同态乘法流水线中的重要开销来源，本文的 lazy 原型在操作计数层面显示出潜在性能优势。真实 wall-clock runtime 仍需通过独立 benchmark 进一步验证。
```

不能写：

```text
lazy 路径运行时间更短。
```

可以写：

```text
lazy 路径减少了 relin/rescale 次数，因此从操作结构上具备降低运行时间的可能性。
```

最终完成 benchmark 后，再根据实测数据补充：

```text
在固定参数和固定输入下，inner-lazy 路径相对 eager 路径的 median runtime 改善为 X%。
```

## 11. 建议的下一步代码任务

下一步如果进入 runtime benchmark，不建议直接改 `34/35` 主实验文件。

更好的方式是新增独立 benchmark 文件：

```text
src/benchmark/ckks_benchmark_01_internal_bsgs_runtime.cpp
```

并在 CMake 中新增 target：

```text
ckks_benchmark_01_internal_bsgs_runtime
```

该文件可以先只支持 `35` 的两个 generated Internal BSGS patterns。不要一开始 benchmark 所有历史实验，否则表格会变大，调试也会变慢。

最小版本只需要：

1. 构建 context/key/input；
2. 跑 warmup；
3. 重复运行 eager 和 inner-lazy；
4. 记录每次总时间；
5. 输出 median/mean；
6. 校验 final error；
7. 输出 CSV 或 Markdown 表格。

这个 benchmark 模块应当与 white-box trace 模块分离。trace 用来解释机制，benchmark 用来统计性能。两者不要混在一个输出里。
