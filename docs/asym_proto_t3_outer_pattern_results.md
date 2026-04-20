# Asymmetric Prototype T3 Outer Pattern Results

Date: 2026-04-20

This note records a second fixed `t=3` BSGS-shaped outer assembly pattern. It is intentionally a near-copy of the `24` execution model with a different coefficient/exponent layout, so the experiment checks that the outer-assembly signal is not a one-off polynomial.

## Scope

Target:

- executable: `ckks_evaluator_25_asym_proto_t3_outer_pattern`
- file: `src/evaluator/ckks_evaluator_25_asym_proto_t3_outer_pattern.cpp`
- validation log: `logs/asym_proto_t3_outer_pattern_2026-04-20.log`

Fixed structure:

```text
P2(x) = block0(x) + block1(x) * z
z = x^8

block0 powers: x^10, x^14, x^16
block1*z powers: x^19, x^22, x^28
```

Both blocks are still fixed grouped `t=3` decomposed blocks over component powers `{x, x^2, x^4, x^8}`. This target deliberately does not add a planner, dynamic block sizing, or a new materialization policy.

## Result

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 8.986100e-14 | 4.482655e-14 |
| COMPOSITESCALINGMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 3.315126e-14 | 2.028517e-10 |

The grouped-lazy path again preserves tensor-product count while reducing materialization-related operations:

```text
tensor products = 16 vs 16
relin count     = 16 vs 6
rescale count   = 16 vs 6
```

## Interpretation

What this demonstrates:

- the `24` outer-assembly result is not tied to one specific coefficient pattern
- a second fixed BSGS-shaped `t=3` layout also works under the same conservative execution model
- grouped-lazy continues to reduce relin/rescale count without reducing tensor-product count
- final errors remain below the current `1e-8` experimental threshold

What should still be tracked:

- `COMPOSITESCALINGMANUAL` lazy error remains around `1e-10`
- this pattern reaches a higher final plaintext power (`x^28`) but is still not a general degree-28 evaluator
- the code is duplicated from `24` by design; it is an experiment checkpoint, not a reusable library abstraction

What this does not yet claim:

- it is not the paper's full asymmetric BSGS algorithm
- it does not implement general `S1^(j)` set generation
- it does not implement a planner for arbitrary exponents or coefficients
- it does not establish asymptotic complexity

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

At this point, `20` through `25` form a useful subject-prototype evidence chain:

- `20`: fixed `t=2` asymmetric decomposition
- `21`: `t=3` raw-chain survival
- `22`: `t=3` grouped raw fold
- `23`: fixed `t=3` polynomial evaluator
- `24`: fixed `t=3` outer assembly
- `25`: second fixed `t=3` outer pattern

The next high-value step is to summarize this chain in one compact milestone document, then start the first small planner-facing prototype rather than adding many more hand-written patterns.
