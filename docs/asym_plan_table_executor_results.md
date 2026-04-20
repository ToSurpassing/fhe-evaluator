# Asymmetric Plan Table Executor Results

Date: 2026-04-20

This note records the first conservative executor that runs a small table of explicit asymmetric plan objects.

## Scope

Target:

- executable: `ckks_evaluator_28_asym_plan_table_executor`
- file: `src/evaluator/ckks_evaluator_28_asym_plan_table_executor.cpp`
- validation log: `logs/asym_plan_table_executor_2026-04-20.log`

This target executes two fixed plans:

```text
outer_assembly_24_shape
outer_pattern_25_shape
```

It intentionally does not generate plans from arbitrary coefficients. The purpose is to validate that the same conservative executor can run more than one known BSGS-shaped plan.

## Result

| Plan | Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `outer_assembly_24_shape` | FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 2.596702e-13 | 9.935141e-14 |
| `outer_assembly_24_shape` | COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 7.834101e-14 | 3.852140e-10 |
| `outer_pattern_25_shape` | FIXEDMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 6.243492e-14 | 5.648553e-14 |
| `outer_pattern_25_shape` | COMPOSITESCALINGMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 5.489101e-14 | 4.056385e-10 |

## Interpretation

What this demonstrates:

- one plan-driven executor can run both fixed outer-assembly shapes
- the switch-count reduction holds across both plan entries
- final errors remain below the current `1e-8` experimental threshold
- this is a stronger planner/executor separation checkpoint than `27`

What this does not yet claim:

- it is not an arbitrary polynomial planner
- it does not synthesize component sets from degree bounds
- it does not choose block layouts automatically
- it does not implement full paper set decomposition

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

The next narrow step is to introduce a very small plan-generation function for the two known shapes. It should still only emit whitelisted layouts, but it should move the source of truth from handwritten `MakeOuterAssembly24Plan()` / `MakeOuterPattern25Plan()` calls toward a planner interface.
