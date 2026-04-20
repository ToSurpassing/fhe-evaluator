# Internal BSGS Milestone 34-35

Date: 2026-04-21

This milestone summarizes the first explicit Internal BSGS line in the CKKS/OpenFHE user-layer prototype.

The purpose of this line is not to implement the full asymmetric BSGS algorithm from the paper. The purpose is to cross one important structural boundary:

```text
grouped outer/block folds
  -> explicit bar(S1) / hat(S1) / S2 structure
  -> Internal BSGS block evaluation
  -> generated fixed InternalBsgsPlan
```

This is a project-level conservative prototype. It is paper-shaped, white-box, fixed, and intentionally limited.

## Current Position

Before this milestone, the repository had already established:

- Degree-8 V1 as a stable conservative lazy/eager baseline
- fixed `t=2` and `t=3` asymmetric prototypes in `20-25`
- plan-object and plan-table executor experiments in `26-28`
- whitelisted and coefficient-pattern planner experiments in `29-33`

Those experiments demonstrated the core project signal:

```text
tensor products remain the same
relin/rescale operations decrease
```

However, they did not yet expose a standalone Internal BSGS subroutine. The `34-35` line fills that gap.

## Confirmed Internal BSGS Chain

| Target | Role | Structure | Main result |
| --- | --- | --- | --- |
| `34` | fixed Internal BSGS prototype | two handwritten fixed plans with `t=2`, `B=4` | 13 vs 13 tensor products, 13 vs 9 relin/rescale |
| `35` | generated fixed Internal BSGS plans | coefficient patterns generate `InternalBsgsPlan` through fixed exponent decomposition | same 13 vs 13 tensor products, 13 vs 9 relin/rescale for two patterns |

Both targets run:

- plaintext skeleton checks
- CKKS eager path
- CKKS inner-lazy path
- trace and stats
- `FIXEDMANUAL`
- `COMPOSITESCALINGMANUAL`

## Fixed Structure

The current Internal BSGS prototype is fixed to:

```text
t = 2
B = 4
bar(S1) = {x, x^2, x^3}
hat(S1) = {x^4, x^8, x^12}
S2 = {x^16}
```

The evaluated shape is:

```text
P(x) = inner0(x) + inner1(x) * x^16
```

Each inner block is evaluated as:

```text
inner(x) = sum_j low_j(x) * high_j(x)
```

where:

- `low_j(x)` is a linear combination over `bar(S1)`
- `high_j(x)` is selected from `hat(S1)`
- the lazy variant folds the raw inner products before materialization
- the outer assembly is still conservative and materialized

## Result Summary

The `34` and `35` checkpoints both show the same stable result:

| Line | Cases | Tensor products | Relin count | Rescale count | Error threshold |
| --- | ---: | ---: | ---: | ---: | ---: |
| `34` handwritten Internal BSGS plans | 2 | 13 vs 13 | 13 vs 9 | 13 vs 9 | below `1e-8` |
| `35` generated Internal BSGS plans | 2 | 13 vs 13 | 13 vs 9 | 13 vs 9 | below `1e-8` |

The plaintext checks also pass at floating-point roundoff level.

This means the repository now has a verified path:

```text
coefficients
  -> fixed t=2,B=4 exponent decomposition
  -> InternalBsgsPlan
  -> eager / inner-lazy CKKS executor
  -> trace / stats / error report
```

## Why This Matters

This milestone is different from the earlier grouped-lazy experiments.

Earlier prototypes showed that delayed materialization works inside fixed grouped products and outer assemblies. The new line makes the inner/outer evaluator topology explicit:

- `bar(S1)` and `hat(S1)` are visible in the code and trace
- Internal BSGS is a named subroutine in the experiment
- coefficient patterns can generate the fixed Internal BSGS plan
- eager and inner-lazy paths share the same topology

This is the first repo-local evidence that the paper's Internal BSGS-shaped organization can be conservatively migrated into the OpenFHE/CKKS user-layer experiment framework.

## What This Does Not Claim

The current milestone does not claim:

- a general asymmetric BSGS planner
- automatic choice of `t`, `B`, or decomposition sets
- arbitrary polynomial support
- full base-`B` algorithm coverage
- aggressive lazy propagation across the outer assembly
- support for higher-order ciphertext pipelines beyond the validated shallow/inner-lazy path
- any modification or guarantee about OpenFHE internals

All conclusions are repo-local engineering results from the current user-layer prototypes.

## Writing Notes For The Thesis

A safe thesis statement for this stage is:

```text
We implemented a conservative CKKS/OpenFHE user-layer prototype that follows the asymmetric BSGS paper's materialization-reduction direction. The prototype does not reduce tensor-product count. Instead, in fixed Internal BSGS-shaped examples, it preserves the tensor-product count while reducing relin/rescale operations from 13 to 9, with final CKKS error below the experiment threshold.
```

Avoid saying:

```text
We implemented the full asymmetric BSGS algorithm.
```

A more accurate description is:

```text
We implemented a fixed, white-box, Internal BSGS-shaped prototype that validates the paper's engineering intuition under OpenFHE/CKKS user-layer constraints.
```

## Next Engineering Decision

The next step should not be another large batch of patterns.

There are two reasonable short-term paths:

1. Keep `34/35` as experiment-local deadline code and write the thesis section around them.
2. Extract a tiny shared Internal BSGS helper after the deadline risk is lower.

Do not jump directly to an arbitrary planner yet. The current generated plan path is intentionally fixed; that is what makes the results explainable and low-risk.
