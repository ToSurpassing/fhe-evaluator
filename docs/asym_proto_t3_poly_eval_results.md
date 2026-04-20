# Asymmetric Prototype T3 Polynomial Evaluator Results

Date: 2026-04-20

This note records the first fixed `t=3` polynomial evaluator prototype. It builds directly on the `22` grouped-fold primitive and adds a small tail layer so the experiment is an end-to-end polynomial evaluation path rather than only a raw grouped block.

## Scope

Target:

- executable: `ckks_evaluator_23_asym_proto_t3_poly_eval`
- file: `src/evaluator/ckks_evaluator_23_asym_proto_t3_poly_eval.cpp`
- validation log: `logs/asym_proto_t3_poly_eval_2026-04-20.log`

Fixed polynomial:

```text
P(x) = 0.25 - 0.30*x + 0.12*x^4 + t3_block(x)
t3_block(x) = 0.15*x^7 - 0.25*x^8 + 0.35*x^9 - 0.20*x^11
```

The grouped-lazy path still uses the fixed `t=3` decomposed block from the previous prototype:

- component powers: `{x, x^2, x^4, x^8}`
- four active degree-3 decomposed products
- raw products are folded before one materialization
- tail terms are added after the folded block is materialized

This is still not a general planner. It is a fixed, white-box evaluator path.

## Result

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 11 vs 11 | 11 vs 4 | 11 vs 4 | 5.237200e-13 | 4.551359e-13 |
| COMPOSITESCALINGMANUAL | 11 vs 11 | 11 vs 4 | 11 vs 4 | 3.604894e-13 | 1.066150e-10 |

The grouped-lazy path folds a 4-element raw degree-3 block before final block materialization:

```text
grouped-lazy raw t=3 terms folded ... ctElems = 4
```

## Interpretation

What this demonstrates:

- the project now has a fixed end-to-end `t=3` polynomial evaluator prototype
- tensor-product count remains unchanged
- grouped-lazy reduces materialization-related operations from 11 to 4
- tail assembly can be added after the grouped `t=3` block without breaking correctness
- final errors remain below the current `1e-8` experimental threshold

What should still be tracked:

- `COMPOSITESCALINGMANUAL` lazy error is around `1e-10`, still passing but much larger than eager
- scalar alignment and tail alignment are still conservative engineering choices, not paper claims

What this does not yet claim:

- it is not the paper's full asymmetric BSGS algorithm
- it does not implement a general base-`B` planner
- it does not include outer giant-step assembly
- it does not establish the paper's asymptotic switching-count result

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

The next high-value step is to add a tiny outer assembly layer:

- keep the fixed `t=3` grouped block as the inner baby-step block
- build one second grouped block or one shifted block
- multiply it by a fixed outer power
- compare eager-vs-lazy materialization counts end to end

That would move the prototype closer to the paper's BSGS-shaped decomposition while still staying fixed and white-box.
