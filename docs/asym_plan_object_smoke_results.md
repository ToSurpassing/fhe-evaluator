# Asymmetric Plan Object Smoke Results

Date: 2026-04-20

This note records the first planner-facing smoke target after the fixed `20-25` prototype line.

## Scope

Target:

- executable: `ckks_evaluator_26_asym_plan_object_smoke`
- file: `src/evaluator/ckks_evaluator_26_asym_plan_object_smoke.cpp`
- validation log: `logs/asym_plan_object_smoke_2026-04-20.log`

This target does not run CKKS evaluation. It validates the data model that a later planner-driven executor can consume:

- component powers
- grouped `t=3` block terms
- per-block outer multiplier
- final plaintext powers
- plaintext shadow values on `kInput`

## Result

Two existing fixed outer-assembly shapes were expressed as plan data:

```text
outer_assembly_24_shape
outer_pattern_25_shape
```

Both passed structural validation:

```text
[PASS] plan object validated
[PASS] plan object validated
```

The generated final polynomial summaries match the intended `24` and `25` layouts:

```text
outer_assembly_24_shape:
+0.150*x^7 -0.250*x^8 +0.350*x^9 -0.200*x^11 -0.180*x^15 +0.220*x^16 -0.120*x^17

outer_pattern_25_shape:
-0.110*x^10 +0.280*x^14 -0.190*x^16 +0.170*x^19 -0.130*x^22 +0.090*x^28
```

## Interpretation

What this demonstrates:

- the fixed `24` and `25` evaluator semantics can be represented as explicit plan data
- block-local terms and outer multipliers can be checked before any CKKS operation runs
- plaintext shadow generation can be attached to the plan object
- the project has started moving from hand-written patterns toward planner-facing execution

What this does not yet claim:

- it does not execute the plan homomorphically
- it does not generate plans from arbitrary coefficients
- it does not replace the existing fixed evaluator targets
- it does not implement the paper's full set-decomposition planner

## Next Step

The next narrow step is to connect one conservative executor path to this plan object for one fixed shape, probably `outer_assembly_24_shape`, while keeping the old `24` target unchanged as the reference.
