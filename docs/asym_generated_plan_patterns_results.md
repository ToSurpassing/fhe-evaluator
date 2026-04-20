# Asymmetric Generated Plan Patterns Results

Date: 2026-04-20

This note records the first generated-plan executor checkpoint that runs more than one coefficient pattern.

## Scope

Target:

- executable: `ckks_evaluator_33_asym_generated_plan_patterns`
- file: `src/evaluator/ckks_evaluator_33_asym_generated_plan_patterns.cpp`
- validation log: `logs/asym_generated_plan_patterns_2026-04-20.log`

This target supports two whitelisted coefficient patterns:

```text
coeff_pattern_outer_assembly_24
coeff_pattern_outer_pattern_25
```

Unlike `32`, this target is no longer a single-pattern generated-plan smoke. It generates one plan per known coefficient pattern using a small whitelisted exponent-decomposition table, validates the generated plan against the requested coefficients, then runs the same conservative eager/lazy executor.

## Result

| Pattern | Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `coeff_pattern_outer_assembly_24` | FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 1.793469e-13 | 5.247376e-14 |
| `coeff_pattern_outer_assembly_24` | COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 5.282222e-14 | 3.197679e-10 |
| `coeff_pattern_outer_pattern_25` | FIXEDMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 4.055698e-14 | 6.326205e-14 |
| `coeff_pattern_outer_pattern_25` | COMPOSITESCALINGMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 4.821767e-14 | 2.007717e-10 |

## Interpretation

What this demonstrates:

- generated-plan execution is no longer a one-off single-pattern case
- two distinct coefficient patterns preserve the expected switch-count reduction
- the executor still keeps tensor products unchanged while reducing relin/rescale counts
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it does not generate plans for arbitrary coefficients
- it does not derive decomposition rules from a general base-`B` algorithm
- it does not support arbitrary outer multipliers or arbitrary block counts
- it does not implement the full paper algorithm

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_33_asym_generated_plan_patterns -j2
./build/ckks_evaluator_33_asym_generated_plan_patterns
cmake --build build -j2
```

The full build completed successfully after the target-specific run.

## Next Step

The next mainline step should be a small `34` checkpoint that removes more direct plan-selection dependence from the generated-plan path, or adds one carefully chosen third whitelisted pattern. The priority is still to prove the generated decomposition path is not a single example, not to build an arbitrary planner.
