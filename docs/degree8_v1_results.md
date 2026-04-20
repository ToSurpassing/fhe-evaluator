# Degree-8 V1 Results

Date: 2026-04-20

This note freezes the current restricted degree-8 evaluator as V1. The purpose is to preserve the current conservative baseline before starting a new, more paper-shaped asymmetric prototype line.

## Scope

V1 covers polynomials of the form:

```text
P(x) = c0 + c1*x + c2*x^2 + ... + c8*x^8
```

The reusable entry point is `EvalRestrictedDegree8(...)`. It evaluates the same plan with two strategies:

- `expanded-eager`
- `grouped-lazy`

The prototype is intentionally conservative:

- it runs only in the user-layer project code
- it does not modify OpenFHE internals
- it keeps tensor-product counts comparable between eager and lazy
- it targets fewer materialization steps, observed as fewer relinearization/rescale operations
- it keeps plaintext shadow/reference evaluation for correctness checks

## Validation Artifacts

The frozen V1 validation logs are:

- `logs/degree8_v1_build_2026-04-20.log`
- `logs/degree8_v1_evaluator_11_2026-04-20.log`
- `logs/degree8_v1_evaluator_12_2026-04-20.log`

Commands used:

```sh
cmake --build build -j2
./build/ckks_evaluator_11_param_coeff_blocks
./build/ckks_evaluator_12_coeff_pattern_regression
```

## Dense Degree-8 Result

For the dense degree-8 polynomial used by `ckks_evaluator_11_param_coeff_blocks`:

```text
P(x)=0.25 -0.3*x +0.7*x^2 -1.2*x^3 +0.5*x^4 +0.2*x^5 -0.4*x^6 +0.9*x^7 +1.1*x^8
```

V1 produces the following key comparison.

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 12 vs 12 | 12 vs 6 | 12 vs 6 | 4.856449e-12 | 4.859890e-12 |
| COMPOSITESCALINGMANUAL | 12 vs 12 | 12 vs 6 | 12 vs 6 | 7.870371e-13 | 5.869472e-12 |

The important V1 conclusion is that grouped-lazy does not reduce tensor-product count in this restricted prototype, but it does reduce relinearization and rescale counts by half on the dense degree-8 path while keeping numerical error below the current regression threshold.

## Coefficient Pattern Regression

`ckks_evaluator_12_coeff_pattern_regression` checks sparse, dense, block-only, tail-only, and linear/block combinations under both `FIXEDMANUAL` and `COMPOSITESCALINGMANUAL`.

Current frozen result:

```text
cases    = 36
all pass = yes
```

The regression verifies:

- eager and lazy final errors are below `1e-8`
- lazy relin/rescale counts are not worse than eager
- plan summaries match expected layouts, block lists, outer multipliers, and tail counts
- explicit low-degree linear-block cases remain covered
- `c5` guard cases remain covered, so `c1` stays as a tail when `c5` is active in V1

## Interpretation

This V1 should be treated as a stable conservative baseline, not as the final paper-faithful algorithm.

What V1 demonstrates:

- OpenFHE/CKKS user-layer code can implement a white-box eager-vs-lazy polynomial evaluator.
- Plaintext shadow and coefficient-pattern regression can keep the evaluator diagnosable.
- The paper's key engineering direction is visible: materialization-related operations can drop while tensor-product count stays the same.

What V1 does not yet implement:

- the paper's full asymmetric BSGS set decomposition
- `S1^(j)` baby-step component sets
- base-`B` reconstruction of larger baby-step powers
- aggressive consecutive tensor chains with higher-order intermediate ciphertexts
- an asymptotic `O(d^(1/t))` switching-count experiment

## Next Step

Freeze V1 here and start a separate restricted asymmetric prototype target. The next line should be a small hand-written set-decomposition experiment, not a refactor of V1:

- add a new `ckks_evaluator_20_*` target
- keep the degree and decomposition fixed
- make the baby-step component sets explicit
- compare ordinary eager materialization against grouped-lazy composition
- keep trace, stats, and plaintext shadow
