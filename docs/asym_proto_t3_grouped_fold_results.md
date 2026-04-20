# Asymmetric Prototype T3 Grouped-Fold Results

Date: 2026-04-20

This note records the first fixed `t=3` grouped-fold prototype. It extends the `21` survival chain from one lazy three-factor product to multiple lazy three-factor products folded before one final materialization.

## Scope

Target:

- executable: `ckks_evaluator_22_asym_proto_t3_grouped_fold`
- file: `src/evaluator/ckks_evaluator_22_asym_proto_t3_grouped_fold.cpp`
- validation log: `logs/asym_proto_t3_grouped_fold_2026-04-20.log`

Fixed prototype structure:

- component powers: `{x, x^2, x^4, x^8}`
- each active term multiplies three components
- active powers: `x^7`, `x^8`, `x^9`, `x^11`
- grouped-lazy path folds four degree-3 raw products before final materialization

This is still a hand-written prototype. It intentionally does not include a general planner or outer giant-step assembly.

## Result

The prototype compares:

- `expanded-eager`: materializes the two-product prefix and final product for each active term
- `grouped-lazy`: computes each three-factor term as a degree-3 raw product, folds the raw products, and materializes once

The grouped-lazy path reaches and folds 4-element ciphertexts:

```text
grouped-lazy raw t=3 terms folded ... ctElems = 4
```

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 11 vs 11 | 11 vs 4 | 11 vs 4 | 2.296057e-13 | 3.631589e-14 |
| COMPOSITESCALINGMANUAL | 11 vs 11 | 11 vs 4 | 11 vs 4 | 3.607255e-14 | 4.616947e-10 |

## Interpretation

What this demonstrates:

- `t=3` grouped-lazy folding works for multiple raw products in the current user-layer prototype
- tensor-product count remains unchanged
- grouped-lazy reduces materialization-related operations from 11 to 4
- final errors remain below the current `1e-8` experimental threshold
- the `COMPOSITESCALINGMANUAL` lazy error is visibly larger than the eager error and should be tracked in later experiments

What this does not yet claim:

- it is not the paper's full asymmetric BSGS algorithm
- it does not implement a general `S1^(j)` planner
- it does not yet include coefficient-block planning or outer giant-step assembly
- it does not establish asymptotic complexity

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

The next high-value step is to turn this grouped-fold prototype into a tiny fixed polynomial evaluator:

- keep the same fixed `t=3` decomposition
- keep the same four active terms
- add one small outer/tail assembly layer only if needed
- preserve eager-vs-lazy stats and plaintext shadow

The important thing is not generality yet. The goal is to produce a small, paper-shaped evaluator path that can be explained end to end.
