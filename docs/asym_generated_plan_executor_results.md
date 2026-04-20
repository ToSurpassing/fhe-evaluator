# Asymmetric Generated Plan Executor Results

Date: 2026-04-20

This note records the first generated-plan executor checkpoint in the asymmetric prototype line.

## Scope

Target:

- executable: `ckks_evaluator_32_asym_generated_plan_executor`
- file: `src/evaluator/ckks_evaluator_32_asym_generated_plan_executor.cpp`
- validation log: `logs/asym_generated_plan_executor_2026-04-20.log`

This target supports one coefficient pattern:

```text
coeff_pattern_outer_assembly_24
```

Unlike `30`, it does not simply select an existing plan. It uses a small whitelisted exponent-decomposition table to generate:

```text
generated_outer_assembly_24_shape
```

The generated plan is validated against the input coefficients, then executed with the same conservative eager/lazy executor.

## Result

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 2.557813e-13 | 3.043412e-14 |
| COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 8.256791e-14 | 4.936633e-10 |

## Interpretation

What this demonstrates:

- the current mainline now has `coefficients -> decomposition -> generated plan -> executor`
- generated plan terms preserve the expected switch-count reduction
- final errors remain below the current `1e-8` experimental threshold
- this is the first step beyond pure plan selection

What this does not yet claim:

- it does not generate plans for arbitrary coefficients
- it does not support the second known pattern yet
- it does not derive decomposition rules automatically
- it does not implement full paper base-`B` set decomposition

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_32_asym_generated_plan_executor -j2
./build/ckks_evaluator_32_asym_generated_plan_executor
cmake --build build -j2
```

The Degree-8 V1 regression was not rerun in this step. This was a deliberate speed tradeoff because the change only adds a standalone target and does not modify existing V1 evaluator code.

## Next Step

The next mainline step should extend generated-plan support to `coeff_pattern_outer_pattern_25`, then remove the direct plan-selection path from the main generated-plan prototype.
