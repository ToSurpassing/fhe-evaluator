# Asymmetric Exponent Decomposition Results

Date: 2026-04-20

This note records the first exponent-decomposition smoke for the asymmetric planner line.

## Scope

Target:

- executable: `ckks_evaluator_31_asym_exponent_decomposition`
- file: `src/evaluator/ckks_evaluator_31_asym_exponent_decomposition.cpp`
- validation log: `logs/asym_exponent_decomposition_2026-04-20.log`

This target does not run CKKS. It verifies that the known coefficient patterns used by `30` can be explained as:

```text
inner t=3 factors + optional outer x^8
```

## Result

Both coefficient patterns passed:

```text
coeff_pattern_outer_assembly_24
coeff_pattern_outer_pattern_25
```

Example decompositions:

```text
x^15 = (x, x^2, x^4) * x^8
x^28 = (x^4, x^8, x^8) * x^8
```

The full run ended with:

```text
[PASS] coefficient powers match inner t=3 factors plus optional outer x^8
[PASS] coefficient powers match inner t=3 factors plus optional outer x^8
```

## Interpretation

What this demonstrates:

- the current coefficient-pattern planner is no longer just an opaque lookup
- every active exponent in the two known patterns has an explicit `t=3` factor decomposition
- the outer multiplier is represented as part of the same decomposition check
- this moves the planner toward the paper's set-decomposition semantics without opening arbitrary layouts

What this does not yet claim:

- it does not derive decompositions from arbitrary exponents
- it does not choose component powers automatically
- it does not implement base-`B` decomposition in the general paper sense
- it does not execute CKKS

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_31_asym_exponent_decomposition -j2
./build/ckks_evaluator_31_asym_exponent_decomposition
cmake --build build -j2
```

The Degree-8 V1 regression was not rerun in this step. This was a deliberate speed tradeoff because the change only adds a standalone non-CKKS smoke target and does not modify existing evaluator code.

## Next Step

The next mainline step should make the decomposition helper produce the plan terms for one known coefficient pattern, instead of only checking an existing plan. Keep it constrained to the current component set `{x, x^2, x^4, x^8}` and the current optional outer `x^8`.
