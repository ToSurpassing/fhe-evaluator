# Internal BSGS Coefficient-List Evaluator Results

Date: 2026-04-21

This note records the first fixed `t=2,B=4` Internal BSGS evaluator that accepts coefficient lists instead of only named coefficient patterns.

## Scope

Target:

- executable: `ckks_evaluator_36_internal_bsgs_coeff_list`
- file: `src/evaluator/ckks_evaluator_36_internal_bsgs_coeff_list.cpp`
- validation log: `logs/internal_bsgs_coeff_list_2026-04-21.log`

This is still a restricted prototype. It accepts coefficient vectors of length at most `32`, skips zero coefficients, and rejects unsupported nonzero powers.

Current fixed decomposition:

```text
t = 2
B = 4
bar(S1) = {x, x^2, x^3}
hat(S1) = {x^4, x^8, x^12}
S2 = {x^16}
```

Supported nonzero powers are those representable as:

```text
power = outer + low + high
outer in {0, 16}
low in {1, 2, 3}
high in {4, 8, 12}
```

So the current supported powers are:

```text
x^5, x^6, x^7, x^9, x^10, x^11, x^13, x^14, x^15,
x^21, x^22, x^23, x^25, x^26, x^27, x^29, x^30, x^31
```

This is a project boundary, not a paper claim.

## Result

| Case | Mode | Active coeffs | Tensor products | Relin count | Rescale count | Eager final err | Inner-lazy final err |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `coeff_list_sparse_degree31` | FIXEDMANUAL | 4 | 11 vs 11 | 11 vs 9 | 11 vs 9 | 3.386692e-13 | 3.847463e-13 |
| `coeff_list_sparse_degree31` | COMPOSITESCALINGMANUAL | 4 | 11 vs 11 | 11 vs 9 | 11 vs 9 | 7.350752e-14 | 6.446939e-14 |
| `coeff_list_zero_skipping` | FIXEDMANUAL | 5 | 12 vs 12 | 12 vs 9 | 12 vs 9 | 1.769446e-13 | 1.923724e-13 |
| `coeff_list_zero_skipping` | COMPOSITESCALINGMANUAL | 5 | 12 vs 12 | 12 vs 9 | 12 vs 9 | 4.022466e-14 | 4.044626e-14 |
| `coeff_list_dense_supported` | FIXEDMANUAL | 18 | 13 vs 13 | 13 vs 9 | 13 vs 9 | 4.593895e-14 | 7.081542e-14 |
| `coeff_list_dense_supported` | COMPOSITESCALINGMANUAL | 18 | 13 vs 13 | 13 vs 9 | 13 vs 9 | 6.921631e-14 | 3.577006e-14 |

## Interpretation

What this confirms:

- the evaluator now starts from coefficient vectors, not only named pattern objects
- zero coefficients are skipped before planning
- active coefficient structure affects the number of required internal groups
- eager and inner-lazy paths still share the same generated Internal BSGS plan
- inner-lazy keeps tensor counts unchanged while reducing relin/rescale counts
- all final errors remain below the current `1e-8` threshold

What this does not yet claim:

- it is not an arbitrary degree-31 polynomial evaluator
- it does not support constants, linear terms, or powers outside the fixed decomposition table
- it does not choose `t`, `B`, or component sets automatically
- it does not implement full paper-strength asymmetric BSGS

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_36_internal_bsgs_coeff_list -j2
./build/ckks_evaluator_36_internal_bsgs_coeff_list
```

The next natural step is to decide whether to extend the supported decomposition table or to extract the repeated Internal BSGS CKKS executor into a small shared helper.
