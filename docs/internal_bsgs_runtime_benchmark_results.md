# Internal BSGS Runtime Benchmark Results

Date: 2026-04-21

This note records the first lightweight wall-clock runtime benchmark for the fixed Internal BSGS generated-plan prototype.

## Scope

Target:

- executable: `ckks_benchmark_01_internal_bsgs_runtime`
- file: `src/benchmark/ckks_benchmark_01_internal_bsgs_runtime.cpp`
- validation log: `logs/internal_bsgs_runtime_2026-04-21.log`

This benchmark uses the same fixed Internal BSGS constraints as `34/35`:

- `t = 2`
- `B = 4`
- `bar(S1) = {x, x^2, x^3}`
- `hat(S1) = {x^4, x^8, x^12}`
- `S2 = {x^16}`

The benchmark measures core evaluator runtime only. Key generation and encryption are outside the timed region.

Default settings:

```text
warmup = 1
repeat = 3
```

This is intentionally a lightweight first benchmark, not the final large-sample runtime experiment.

## Result

| Pattern | Mode | Path | Tensor | Relin | Rescale | Median sec | Mean sec | Final err |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `coeff_pattern_internal_t2_b4_a` | FIXEDMANUAL | eager | 13 | 13 | 13 | 5.951540e-01 | 6.258515e-01 | 2.935436e-13 |
| `coeff_pattern_internal_t2_b4_a` | FIXEDMANUAL | inner-lazy | 13 | 9 | 9 | 7.298596e-01 | 6.397611e-01 | 3.552206e-13 |
| `coeff_pattern_internal_t2_b4_a` | COMPOSITESCALINGMANUAL | eager | 13 | 13 | 13 | 1.543352e+00 | 1.539266e+00 | 4.179762e-14 |
| `coeff_pattern_internal_t2_b4_a` | COMPOSITESCALINGMANUAL | inner-lazy | 13 | 9 | 9 | 1.086385e+00 | 1.155159e+00 | 5.751291e-14 |
| `coeff_pattern_internal_t2_b4_b` | FIXEDMANUAL | eager | 13 | 13 | 13 | 6.542201e-01 | 6.607304e-01 | 2.147259e-13 |
| `coeff_pattern_internal_t2_b4_b` | FIXEDMANUAL | inner-lazy | 13 | 9 | 9 | 4.957434e-01 | 5.131317e-01 | 2.526668e-13 |
| `coeff_pattern_internal_t2_b4_b` | COMPOSITESCALINGMANUAL | eager | 13 | 13 | 13 | 9.778164e-01 | 1.013226e+00 | 3.872587e-14 |
| `coeff_pattern_internal_t2_b4_b` | COMPOSITESCALINGMANUAL | inner-lazy | 13 | 9 | 9 | 1.107362e+00 | 1.094378e+00 | 4.830425e-14 |

## Interpretation

What this confirms:

- the benchmark module runs the same fixed generated Internal BSGS patterns as `35`
- the operation-count signal is preserved: tensor products stay at `13`, while relin/rescale counts change from `13/13` to `9/9`
- final errors remain below the current `1e-8` threshold
- wall-clock timing can now be measured separately from white-box trace experiments

What this does not yet prove:

- it does not prove that inner-lazy is always faster
- it does not provide final statistically stable runtime numbers
- it does not benchmark all earlier prototype lines
- it does not include key generation or encryption in the timed region

The first lightweight timing result is mixed:

- `coeff_pattern_internal_t2_b4_a` under `COMPOSITESCALINGMANUAL` shows inner-lazy faster in this run
- `coeff_pattern_internal_t2_b4_b` under `FIXEDMANUAL` shows inner-lazy faster in this run
- the other two mode/pattern pairs do not show a clear speedup in this small sample

This is an important result. It confirms that reduced relin/rescale count is a structural improvement, but wall-clock runtime still depends on alignment, scalar operations, OpenFHE internals, memory behavior, and scaling mode.

## Thesis-Safe Statement

A safe statement after this benchmark is:

```text
The proposed inner-lazy prototype consistently reduces relin/rescale counts in the tested Internal BSGS cases. A lightweight runtime benchmark shows mixed wall-clock results, indicating that operation-count reductions do not automatically translate to uniform runtime speedups in the current OpenFHE/CKKS user-layer prototype.
```

Avoid claiming:

```text
The lazy evaluator is always faster.
```

## Next Step

Before using runtime data as a final thesis result, the benchmark should be rerun with larger repeat counts, for example:

```text
warmup = 3
repeat = 20
```

It may also be useful to print per-repeat samples or CSV output, so outliers can be inspected.
