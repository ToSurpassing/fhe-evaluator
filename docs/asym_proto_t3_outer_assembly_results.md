# Asymmetric Prototype T3 Outer Assembly Results

Date: 2026-04-20

This note records the first fixed `t=3` prototype with a BSGS-shaped outer assembly. It builds on the `23` fixed polynomial evaluator and adds a second grouped `t=3` block multiplied by one fixed outer power.

## Scope

Target:

- executable: `ckks_evaluator_24_asym_proto_t3_outer_assembly`
- file: `src/evaluator/ckks_evaluator_24_asym_proto_t3_outer_assembly.cpp`
- validation log: `logs/asym_proto_t3_outer_assembly_2026-04-20.log`

Fixed structure:

```text
P(x) = block0(x) + block1(x) * z
z = x^8

block0 powers: x^7, x^8, x^9, x^11
block1*z powers: x^15, x^16, x^17
```

Both `block0` and `block1` are evaluated as fixed grouped `t=3` decomposed blocks:

- component powers: `{x, x^2, x^4, x^8}`
- each active term is a three-factor decomposed product
- grouped-lazy path folds raw degree-3 products before one block materialization
- outer assembly then multiplies the materialized `block1` by `z=x^8`

This is still a hand-written prototype. It intentionally does not implement a general base-`B` planner.

## Result

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 2.226859e-13 | 6.866404e-14 |
| COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 6.004022e-14 | 3.360876e-10 |

The grouped-lazy path reduces materialization-related operations across the two inner grouped blocks and the outer assembly:

```text
tensor products = 18 vs 18
relin count     = 18 vs 6
rescale count   = 18 vs 6
```

## Depth Requirement

This target uses:

```text
SetMultiplicativeDepth(12)
SetMaxRelinSkDeg(3)
```

During development, smaller depths were insufficient once the second grouped block and the outer `block1*z` assembly were included. The current result should therefore be interpreted as a deeper fixed prototype than `21`, `22`, and `23`, not as a drop-in replacement for their shallower contexts.

## Interpretation

What this demonstrates:

- the project now has a fixed BSGS-shaped `t=3` outer assembly prototype
- tensor-product count remains unchanged
- grouped-lazy reduces relin/rescale counts from 18 to 6
- two independent grouped `t=3` blocks can be assembled through one fixed outer multiplier
- final errors remain below the current `1e-8` experimental threshold

What should still be tracked:

- `COMPOSITESCALINGMANUAL` lazy error is around `1e-10`, still passing but visibly larger than eager
- the prototype needs a deeper CKKS context than earlier `t=3` experiments
- outer assembly currently materializes `block1` before multiplying by `z`; this is conservative and not the paper's most aggressive lazy route

What this does not yet claim:

- it is not the paper's full asymmetric BSGS algorithm
- it does not implement general `S1^(j)` set generation
- it does not implement a planner for arbitrary exponents or coefficients
- it does not establish the paper's asymptotic switching-count result

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

The next high-value step is to avoid getting stuck polishing this fixed example. A good next move is either:

- add one second coefficient/layout pattern for this outer-assembly prototype, or
- write a compact milestone summary table for `20` through `24` and then begin the first small planner-facing step.

The main research signal is already present: in a BSGS-shaped fixed `t=3` prototype, grouped-lazy preserves tensor-product count while reducing materialization operations.
