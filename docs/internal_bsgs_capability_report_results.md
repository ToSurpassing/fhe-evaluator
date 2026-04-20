# Internal BSGS Capability Report Results

Date: 2026-04-21

This note records a planner/capability smoke target for the fixed `t=2,B=4` Internal BSGS coefficient-list evaluator.

## Scope

Target:

- executable: `ckks_evaluator_37_internal_bsgs_capability_report`
- file: `src/evaluator/ckks_evaluator_37_internal_bsgs_capability_report.cpp`
- validation log: `logs/internal_bsgs_capability_report_2026-04-21.log`

This target does not run CKKS. It checks only the fixed decomposition support boundary used by the current coefficient-list evaluator.

## Supported Powers

The current fixed decomposition supports:

```text
x^5, x^6, x^7,
x^9, x^10, x^11,
x^13, x^14, x^15,
x^21, x^22, x^23,
x^25, x^26, x^27,
x^29, x^30, x^31
```

Each supported power is represented as:

```text
power = outer + low + high
outer in {0, 16}
low in {1, 2, 3}
high in {4, 8, 12}
```

This is a current project boundary, not a claim about the full asymmetric BSGS paper algorithm.

## Rejection Checks

The target explicitly confirms that nonzero coefficients at the following powers are rejected:

```text
x^0, x^1, x^2, x^3, x^4,
x^8, x^12, x^16, x^17,
x^20, x^24, x^28, x^32
```

The target also confirms that zero coefficients at unsupported powers are ignored before planning, while an unsupported nonzero coefficient in a coefficient list is rejected with a clear error.

## Result

The validation log reports:

```text
[PASS] supported power table matches fixed t=2,B=4 decomposition
[PASS] zero coefficients at unsupported powers were ignored
[PASS] coefficient list rejected unsupported nonzero x^20
```

## Interpretation

This makes the `36` evaluator boundary explicit:

- it is a usable restricted coefficient-list evaluator
- it is not an arbitrary degree-31 evaluator
- zero coefficients are skipped
- unsupported nonzero powers fail fast
- the accepted powers are derived from the fixed decomposition table

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_37_internal_bsgs_capability_report -j2
./build/ckks_evaluator_37_internal_bsgs_capability_report
```

The next natural step is either to extend the supported decomposition table, or to extract the repeated Internal BSGS CKKS executor from `35/36/benchmark` into a small shared helper.
