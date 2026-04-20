# Asymmetric Prototype T3 Survival Results

Date: 2026-04-20

This note records the first `t=3` survival prototype after the fixed `t=2` asymmetric decomposition prototype. The goal is not to implement a full evaluator yet. The goal is to verify that the current OpenFHE/CKKS user-layer setup can survive a three-factor lazy composition path with one delayed materialization.

## Scope

Target:

- executable: `ckks_evaluator_21_asym_proto_t3_survival`
- file: `src/evaluator/ckks_evaluator_21_asym_proto_t3_survival.cpp`
- validation log: `logs/asym_proto_t3_survival_2026-04-20.log`

Fixed survival decomposition:

- `B = 2`
- `t = 3`
- `S1^(0) = {x}`
- `S1^(1) = {x^2}`
- `S1^(2) = {x^4}`
- target term: `x * x^2 * x^4 = x^7`

This is a project survival probe. It is intentionally smaller than the paper's full asymmetric BSGS construction.

## Result

The prototype compares:

- `expanded-eager`: materializes `x*x^2` before multiplying by `x^4`
- `grouped-lazy`: computes `x*x^2` as a raw product, multiplies the raw prefix by `x^4`, then materializes once

The grouped-lazy path reaches a 4-element ciphertext before final materialization:

```text
grouped-lazy term raw prefix*x^4 raw product ... ctElems = 4
```

That confirms this target is actually exercising a degree-3 secret-key intermediate, rather than only repeating the old two-factor lazy case.

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 4 vs 4 | 4 vs 3 | 4 vs 3 | 6.088268e-13 | 6.525687e-13 |
| COMPOSITESCALINGMANUAL | 4 vs 4 | 4 vs 3 | 4 vs 3 | 3.920803e-14 | 7.620131e-14 |

## Interpretation

What this demonstrates:

- a conservative `t=3` lazy chain can be constructed in the current OpenFHE/CKKS user-layer project
- `SetMaxRelinSkDeg(3)` plus `EvalMultKeysGen` is sufficient for this specific degree-3 intermediate
- tensor-product count remains unchanged
- grouped-lazy saves one relinearization and one rescale in this minimal chain
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it is not a full `t=3` polynomial evaluator
- it does not yet fold multiple `t=3` raw products before one materialization
- it does not yet implement a general `S1^(j)` planner
- it does not yet include outer giant-step assembly
- it does not establish the paper's asymptotic switching-count result

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

The next useful step is to extend this survival target into a tiny `t=3` grouped-fold prototype:

- keep the same fixed component sets
- add two or three `t=3` decomposed products
- fold the raw degree-3 products before one materialization
- compare against eager materialization after each product

That would move from "one t=3 chain survives" to "a small t=3 grouped-lazy sum survives."
