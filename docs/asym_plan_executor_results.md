# Asymmetric Plan Executor Results

Date: 2026-04-20

This note records the first conservative executor driven by an explicit asymmetric plan object.

## Scope

Target:

- executable: `ckks_evaluator_27_asym_plan_executor`
- file: `src/evaluator/ckks_evaluator_27_asym_plan_executor.cpp`
- validation log: `logs/asym_plan_executor_2026-04-20.log`

This target executes only one fixed plan:

```text
outer_assembly_24_shape
```

It intentionally keeps the old `24` target unchanged. The purpose is to prove that a plan object can drive the same conservative eager/lazy executor shape before attempting broader planning.

## Result

| Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | ---: | ---: | ---: | ---: | ---: |
| FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 1.793199e-13 | 6.198949e-14 |
| COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 5.920797e-14 | 3.357219e-10 |

The plan-driven executor preserves the same core signal as `24`:

```text
tensor products = 18 vs 18
relin count     = 18 vs 6
rescale count   = 18 vs 6
```

## Interpretation

What this demonstrates:

- `outer_assembly_24_shape` can be validated and then executed from plan data
- the old fixed `24` evaluator semantics can be preserved while moving toward planner-driven execution
- grouped-lazy still reduces relin/rescale count without reducing tensor-product count
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it is not a general planner
- it does not execute arbitrary plan objects
- it does not yet share code with `24`
- it does not implement full paper set decomposition

## Baseline Check

After adding this target, the Degree-8 V1 coefficient-pattern regression was rerun:

```text
cases    = 36
all pass = yes
```

The full project build also completed successfully.

## Next Step

The next narrow step is to let the same executor accept both known fixed plans, `outer_assembly_24_shape` and `outer_pattern_25_shape`, and run them as a small plan table. That would test multi-plan execution without introducing arbitrary coefficient planning.
