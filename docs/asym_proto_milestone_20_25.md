# Asymmetric Prototype Milestone 20-25

Date: 2026-04-20

This milestone summarizes the first subject-prototype line for the asymmetric BSGS migration work. The goal of this line was not to build a general evaluator yet. The goal was to establish, with small white-box CKKS/OpenFHE experiments, that grouped-lazy materialization can preserve tensor-product count while reducing relin/rescale operations in increasingly paper-shaped structures.

## Confirmed Prototype Chain

| Target | Role | Structure | Main result |
| --- | --- | --- | --- |
| `20` | fixed `t=2` decomposition | two fixed asymmetric patterns | grouped-lazy reduces materialization in a small decomposed setting |
| `21` | `t=3` survival | one raw three-factor chain | `ctElems=4` raw chain survives and materializes correctly |
| `22` | `t=3` grouped fold | multiple raw degree-3 terms folded before materialization | 11 vs 11 tensor products, 11 vs 4 relin/rescale |
| `23` | fixed polynomial evaluator | one grouped `t=3` block plus tail terms | 11 vs 11 tensor products, 11 vs 4 relin/rescale |
| `24` | fixed outer assembly | two grouped `t=3` blocks plus `z=x^8` outer multiply | 18 vs 18 tensor products, 18 vs 6 relin/rescale |
| `25` | second outer pattern | second fixed two-block outer layout up to `x^28` | 16 vs 16 tensor products, 16 vs 6 relin/rescale |

All results above are repo-local engineering results from the current OpenFHE/CKKS user-layer prototypes. They are not claims about OpenFHE internals or the full paper algorithm.

## What Is Now Established

The current repository has confirmed:

- `t=3` raw intermediate ciphertexts can be produced and materialized in the current user-layer setup
- multiple raw degree-3 products can be folded before one final materialization
- fixed polynomial evaluation can combine grouped `t=3` blocks with conservative tail/outer assembly
- two distinct BSGS-shaped outer patterns show the same core signal
- tensor-product count stays unchanged while relin/rescale count decreases
- Degree-8 V1 regression still passes after the subject-prototype additions

## What This Means For The Graduation Project

This is now more than smoke testing. It is a conservative migration of the paper's main engineering direction:

```text
do not primarily reduce tensor products
reduce materialization-related operations instead
```

The prototypes are still deliberately fixed and white-box. That is a project decision: it keeps the CKKS/OpenFHE behavior observable before introducing a planner.

## Known Limits

The current line still does not implement:

- general `S1^(j)` set generation
- base-`B` exponent decomposition
- arbitrary coefficient planning
- dynamic block sizing
- aggressive lazy propagation across the outer assembly
- the paper's asymptotic `O(d^(1/t))` switching-count construction

The `COMPOSITESCALINGMANUAL` lazy path often has larger error than eager, generally around `1e-10` in the later `t=3` prototypes. It remains below the current `1e-8` experiment threshold but should stay visible in all future summaries.

## Next Engineering Step

The next step is a planner-facing prototype, not another hand-written FHE evaluator:

- make the fixed block/outer structure explicit as data
- validate component powers, block terms, outer multipliers, and final powers
- keep the existing FHE executor paths untouched
- only after this plan object is stable, connect a conservative executor to it

This keeps progress moving toward the paper's set-decomposition semantics without risking the verified `20-25` evaluator line.
