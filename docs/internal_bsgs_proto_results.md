# Internal BSGS Prototype Results

Date: 2026-04-20

This note records the first fixed, tiny Internal BSGS prototype in the asymmetric evaluator line.

## Scope

Target:

- executable: `ckks_evaluator_34_internal_bsgs_proto`
- file: `src/evaluator/ckks_evaluator_34_internal_bsgs_proto.cpp`
- validation log: `logs/internal_bsgs_proto_2026-04-20.log`

This target runs two fixed teaching prototypes:

- `t = 2`
- `B = 4`
- `bar(S1) = {x, x^2, x^3}`
- `hat(S1) = {x^4, x^8, x^12}`
- `S2 = {x^16}`

It evaluates:

```text
P(x) = inner0(x) + inner1(x) * x^16
```

Each inner block is evaluated with an explicit Internal BSGS structure:

```text
inner(x) = sum_j low_j(x) * high_j(x)
```

where `low_j(x)` is a linear combination over `bar(S1)` and `high_j(x)` is selected from `hat(S1)`.

## Result

| Plan | Scaling mode | Tensor products | Relin count | Rescale count | Eager final max abs err | Inner-lazy final max abs err |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `tiny_internal_bsgs_t2_b4` | FIXEDMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 3.211676e-13 | 3.495058e-13 |
| `tiny_internal_bsgs_t2_b4` | COMPOSITESCALINGMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 7.497861e-14 | 3.820100e-14 |
| `tiny_internal_bsgs_t2_b4_alt` | FIXEDMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 1.729553e-13 | 2.841011e-13 |
| `tiny_internal_bsgs_t2_b4_alt` | COMPOSITESCALINGMANUAL | 13 vs 13 | 13 vs 9 | 13 vs 9 | 5.672611e-14 | 6.219023e-14 |

The plaintext skeleton also passed for both plans:

```text
tiny_internal_bsgs_t2_b4 direct-vs-internal max_abs_err = 2.168404e-19
tiny_internal_bsgs_t2_b4_alt direct-vs-internal max_abs_err = 1.084202e-19
```

## Interpretation

What this demonstrates:

- the repo now has an explicit two-case Internal BSGS prototype, not only grouped outer-block folds
- the prototype separates `bar(S1)`, `hat(S1)`, and `S2`
- eager and inner-lazy paths share the same Internal BSGS topology
- inner-lazy keeps tensor products unchanged while reducing relin/rescale counts
- final errors remain below the current `1e-8` experimental threshold

What this does not yet claim:

- it is not a general planner
- it is not a full paper-strength asymmetric BSGS implementation
- it does not derive `B`, `t`, or the sets automatically
- it does not make the outer assembly lazy; only the inner BSGS groups are lazily folded

## Validation

Commands run:

```text
cmake -S . -B build
cmake --build build --target ckks_evaluator_34_internal_bsgs_proto -j2
./build/ckks_evaluator_34_internal_bsgs_proto
cmake --build build -j2
```

The full build completed successfully after the target-specific run.

## Next Step

The next high-ROI step is no longer to add more cases. The better next step is to factor the Internal BSGS plan shape into a small reusable helper or add a tiny decomposition-to-internal-plan smoke, still without a general planner.
