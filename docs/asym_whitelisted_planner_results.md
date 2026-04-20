# Asymmetric Whitelisted Planner Results

Date: 2026-04-20

This note records the first tiny planner interface for the asymmetric prototype line.

## Scope

Target:

- executable: `ckks_evaluator_29_asym_whitelisted_planner`
- file: `src/evaluator/ckks_evaluator_29_asym_whitelisted_planner.cpp`
- validation log: `logs/asym_whitelisted_planner_2026-04-20.log`

This target still executes only known fixed plans. The change from `28` is that the plan table is generated through:

```text
PlanId
WhitelistedPlanIds()
MakeWhitelistedPlan(PlanId)
```

This is a planner interface checkpoint, not an arbitrary polynomial planner.

## Result

| Plan | Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Lazy final max abs err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `outer_assembly_24_shape` | FIXEDMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 1.919240e-13 | 3.662682e-14 |
| `outer_assembly_24_shape` | COMPOSITESCALINGMANUAL | 18 vs 18 | 18 vs 6 | 18 vs 6 | 6.174407e-14 | 3.593034e-10 |
| `outer_pattern_25_shape` | FIXEDMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 5.142738e-14 | 6.488751e-14 |
| `outer_pattern_25_shape` | COMPOSITESCALINGMANUAL | 16 vs 16 | 16 vs 6 | 16 vs 6 | 4.868830e-14 | 1.372712e-10 |

## Interpretation

What this demonstrates:

- the executor now receives plans through a planner-like API
- the planner is deliberately whitelisted and cannot synthesize arbitrary layouts
- both known BSGS-shaped plans still preserve the switch-count reduction
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it is not a coefficient-driven planner
- it does not derive component powers from a degree bound
- it does not choose block layouts
- it does not implement full paper set decomposition

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_29_asym_whitelisted_planner -j2
./build/ckks_evaluator_29_asym_whitelisted_planner
cmake --build build -j2
```

The Degree-8 V1 regression was not rerun in this step. This was a deliberate speed tradeoff because the change only adds a new standalone target and does not modify the existing V1 evaluator code.

## Next Step

The next mainline step should move beyond enum selection toward a tiny coefficient-pattern planner. Keep it narrow: accept only one or two known coefficient maps and emit the same whitelisted plan shapes.
