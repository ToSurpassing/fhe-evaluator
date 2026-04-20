# Asymmetric Prototype T2 Results

Date: 2026-04-20

This note records the first prototype after freezing Degree-8 V1. The goal is to start a separate, more paper-shaped line without refactoring the V1 evaluator.

## Scope

Target:

- executable: `ckks_evaluator_20_asym_proto_t2`
- file: `src/evaluator/ckks_evaluator_20_asym_proto_t2.cpp`
- first validation log: `logs/asym_proto_t2_2026-04-20.log`
- two-pattern validation log: `logs/asym_proto_t2_patterns_2026-04-20.log`

This is a fixed, teaching-sized prototype:

- `B = 4`
- `t = 2`
- `S1^(0) = {x, x^2, x^3}`
- `S1^(1) = {x^4, x^8, x^12}`
- terms are reconstructed as `x^(a+4b) = x^a * x^(4b)`
- active terms cover `a in {1,2,3}` and `b in {1,2,3}`, so the maximum degree is 15
- two coefficient patterns are currently checked: `dense_3x3` and `sparse_cross`

The use of `t=2` is a project decision for a conservative first prototype. It is not presented as the paper's optimal setting.

## Result

The prototype compares:

- `expanded-eager`: materializes each decomposed product independently
- `grouped-lazy`: computes the same raw decomposed products, folds them, and materializes once

Initial single-pattern result:

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 14 vs 14 | 14 vs 6 | 14 vs 6 | 1.463138e-12 | 1.468361e-12 |
| COMPOSITESCALINGMANUAL | 14 vs 14 | 14 vs 6 | 14 vs 6 | 1.135379e-13 | 8.114372e-12 |

Two-pattern regression result:

| Pattern | Scaling mode | Active decomposed products | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dense_3x3 | FIXEDMANUAL | 9 | 14 vs 14 | 14 vs 6 | 14 vs 6 | 1.478229e-12 | 1.463103e-12 |
| sparse_cross | FIXEDMANUAL | 5 | 10 vs 10 | 10 vs 6 | 10 vs 6 | 9.567282e-13 | 1.039730e-12 |
| dense_3x3 | COMPOSITESCALINGMANUAL | 9 | 14 vs 14 | 14 vs 6 | 14 vs 6 | 1.244248e-13 | 5.934153e-12 |
| sparse_cross | COMPOSITESCALINGMANUAL | 5 | 10 vs 10 | 10 vs 6 | 10 vs 6 | 1.002049e-13 | 5.258145e-12 |

## Interpretation

This is the first repo-local prototype that explicitly uses a baby-step component-set decomposition rather than only the Degree-8 V1 two-block shape.

What it demonstrates:

- the set decomposition is visible in the executable output
- both strategies evaluate the same fixed degree-15 polynomial pattern in each case
- the lazy benefit holds for both a dense 3-by-3 product grid and a sparse cross pattern
- tensor-product count remains unchanged
- grouped-lazy reduces materialization-related operations from 14 to 6
- errors remain below the current `1e-8` experimental threshold

What it still does not claim:

- it is not the full paper algorithm
- it does not implement `t >= 3`
- it does not implement a general planner
- it does not use higher-order consecutive tensor chains beyond the conservative `t=2` product layer
- it does not establish asymptotic switching complexity

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.
