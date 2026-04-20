# Asymmetric Coefficient-Pattern Planner Results

Date: 2026-04-20

This note records the first coefficient-pattern planner checkpoint in the asymmetric prototype line.

## Scope

Target:

- executable: `ckks_evaluator_30_asym_coeff_pattern_planner`
- file: `src/evaluator/ckks_evaluator_30_asym_coeff_pattern_planner.cpp`
- validation log: `logs/asym_coeff_pattern_planner_2026-04-20.log`

This target accepts only two known coefficient patterns:

```text
coeff_pattern_outer_assembly_24
coeff_pattern_outer_pattern_25
```

It maps those coefficient patterns to existing whitelisted plans, then executes the same conservative eager/lazy evaluator.

## Result

| Coeff pattern | Plan | Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| `coeff_pattern_outer_assembly_24` | `outer_assembly_24_shape` | FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 1.485086e-13 | 6.779223e-14 |
| `coeff_pattern_outer_assembly_24` | `outer_assembly_24_shape` | COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 7.218589e-14 | 2.701516e-10 |
| `coeff_pattern_outer_pattern_25` | `outer_pattern_25_shape` | FIXEDMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 4.620778e-14 | 5.505342e-14 |
| `coeff_pattern_outer_pattern_25` | `outer_pattern_25_shape` | COMPOSITESCALINGMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 5.014312e-14 | 3.603756e-10 |

## Interpretation

What this demonstrates:

- the mainline now has a coefficient-pattern entry point
- plan selection is still deliberately whitelisted
- selected plans preserve the previous switch-count reductions
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it does not plan arbitrary polynomial coefficients
- it does not derive base-`B` decompositions
- it does not choose component powers automatically
- it does not implement full paper set decomposition

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_30_asym_coeff_pattern_planner -j2
./build/ckks_evaluator_30_asym_coeff_pattern_planner
cmake --build build -j2
```

The Degree-8 V1 regression was not rerun in this step. This was a deliberate speed tradeoff because the change only adds a new standalone target and does not modify the existing V1 evaluator code.

## Next Step

The next mainline step should introduce a tiny exponent-decomposition helper for the same two patterns. It should verify that each final exponent can be represented as:

```text
inner t=3 factors + optional outer x^8
```

This would move the planner from pattern matching toward the paper's set-decomposition semantics without opening arbitrary layouts yet.
