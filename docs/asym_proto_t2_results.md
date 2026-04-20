# Asymmetric Prototype T2 Results

Date: 2026-04-20

This note records the first prototype after freezing Degree-8 V1. The goal is to start a separate, more paper-shaped line without refactoring the V1 evaluator.

## Scope

Target:

- executable: `ckks_evaluator_20_asym_proto_t2`
- file: `src/evaluator/ckks_evaluator_20_asym_proto_t2.cpp`
- validation log: `logs/asym_proto_t2_2026-04-20.log`

This is a fixed, teaching-sized prototype:

- `B = 4`
- `t = 2`
- `S1^(0) = {x, x^2, x^3}`
- `S1^(1) = {x^4, x^8, x^12}`
- terms are reconstructed as `x^(a+4b) = x^a * x^(4b)`
- active terms cover `a in {1,2,3}` and `b in {1,2,3}`, so the maximum degree is 15

The use of `t=2` is a project decision for a conservative first prototype. It is not presented as the paper's optimal setting.

## Result

The prototype compares:

- `expanded-eager`: materializes each decomposed product independently
- `grouped-lazy`: computes the same raw decomposed products, folds them, and materializes once

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 14 vs 14 | 14 vs 6 | 14 vs 6 | 1.463138e-12 | 1.468361e-12 |
| COMPOSITESCALINGMANUAL | 14 vs 14 | 14 vs 6 | 14 vs 6 | 1.135379e-13 | 8.114372e-12 |

## Interpretation

This is the first repo-local prototype that explicitly uses a baby-step component-set decomposition rather than only the Degree-8 V1 two-block shape.

What it demonstrates:

- the set decomposition is visible in the executable output
- both strategies evaluate the same fixed degree-15 polynomial
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
