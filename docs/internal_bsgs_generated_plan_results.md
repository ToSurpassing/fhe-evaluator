# Internal BSGS Generated Plan Results

Date: 2026-04-21

This note records the first checkpoint that generates fixed Internal BSGS plans from coefficient patterns.

## Scope

Target:

- executable: `ckks_evaluator_35_internal_bsgs_generated_plan`
- file: `src/evaluator/ckks_evaluator_35_internal_bsgs_generated_plan.cpp`
- validation log: `logs/internal_bsgs_generated_plan_2026-04-21.log`

This target keeps the same conservative teaching constraints as `34`:

- `t = 2`
- `B = 4`
- `bar(S1) = {x, x^2, x^3}`
- `hat(S1) = {x^4, x^8, x^12}`
- `S2 = {x^16}`

Unlike `34`, the internal plan is not selected as a prewritten plan object. It is generated from a coefficient pattern by decomposing each exponent into:

```text
final_power = outer_power + low_power + high_power
outer_power in {0, 16}
low_power in {1, 2, 3}
high_power in {4, 8, 12}
```

The generated plan is validated against the input coefficients before CKKS execution.

## Result

| Pattern | Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Inner-lazy final max abs err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `coeff_pattern_internal_t2_b4_a` | FIXEDMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 3.510376e-13 | 3.830690e-13 |
| `coeff_pattern_internal_t2_b4_a` | COMPOSITESCALINGMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 5.042757e-14 | 4.085794e-14 |
| `coeff_pattern_internal_t2_b4_b` | FIXEDMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 2.166109e-13 | 2.447759e-13 |
| `coeff_pattern_internal_t2_b4_b` | COMPOSITESCALINGMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 3.470928e-14 | 3.620760e-14 |

Plaintext skeleton checks:

```text
coeff_pattern_internal_t2_b4_a direct-vs-internal max_abs_err = 2.168404e-19
coeff_pattern_internal_t2_b4_b direct-vs-internal max_abs_err = 1.084202e-19
```

## Interpretation

What this demonstrates:

- the repo now has `coefficients -> fixed decomposition -> InternalBsgsPlan -> executor`
- the generated plan path works for two fixed coefficient patterns
- eager and inner-lazy paths still share the same Internal BSGS topology
- inner-lazy keeps tensor products unchanged while reducing relin/rescale counts
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it is not an arbitrary planner
- it does not derive `B`, `t`, or component sets automatically
- it does not support arbitrary exponents outside the fixed decomposition table
- it does not implement full paper-strength asymmetric BSGS

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_35_internal_bsgs_generated_plan -j2
./build/ckks_evaluator_35_internal_bsgs_generated_plan
cmake --build build -j2
```

The full build completed successfully after the target-specific run.

## Next Step

The next high-ROI step is to factor the repeated Internal BSGS executor code from `34` and `35` only after one more mainline decision: either keep it as experiment-local code for the deadline, or extract a tiny shared internal-BSGS helper. Do not jump to an arbitrary planner yet.
