# Project Progress Summary

Date: 2026-04-21

This document summarizes the current state of the OpenFHE/CKKS user-layer asymmetric BSGS migration project.

The project goal is not to implement a new HE scheme or modify OpenFHE internals. The goal is to conservatively migrate the polynomial-evaluation optimization idea from the asymmetric BSGS paper into a white-box CKKS/OpenFHE experiment framework.

## Core Direction

The paper's engineering direction is:

```text
do not primarily reduce tensor products
reduce key-switch / modulus-switch style materialization work
```

The current repository follows that direction through conservative user-layer prototypes:

- eager baseline path
- lazy or grouped-lazy comparison path
- plaintext shadow
- trace rows
- relin/rescale/tensor counters
- fixed, explainable evaluator layouts before planner generalization

## Current Stage

The current repo is beyond smoke testing and beyond a single fixed evaluator.

The current stage is:

```text
fixed Internal BSGS-shaped generated-plan prototype
```

In more concrete terms, the current mainline has reached:

```text
coefficients
  -> fixed t=2,B=4 exponent decomposition
  -> InternalBsgsPlan
  -> eager / inner-lazy CKKS executor
  -> trace / stats / error report
```

This is still not the full paper algorithm. It is a conservative, fixed, repo-local prototype that validates the paper's materialization-reduction intuition under OpenFHE/CKKS user-layer constraints.

## Milestones

### 1. Smoke And Survival Layer

The early experiments established basic OpenFHE/CKKS user-layer behavior:

- raw `EvalMultNoRelin` products can be produced
- materialization through relin/rescale can be traced
- mixed-state addition and scale/level alignment need explicit care
- CKKS scaling modes must be tested, not assumed

These experiments were necessary because paper-level legality does not automatically imply OpenFHE user-layer usability.

### 2. Degree-8 V1

The Degree-8 V1 line produced a reusable restricted evaluator:

- `EvalRestrictedDegree8`
- eager/lazy paired execution
- plaintext shadow
- trace/stats
- coefficient-pattern regression

The important result was not generality. The important result was a stable baseline showing that grouped lazy can reduce relin/rescale counts while preserving correctness in a controlled CKKS setting.

Reference:

- `docs/degree8_v1_results.md`

### 3. Asymmetric Prototype Line 20-25

The `20-25` line moved beyond degree-8 into fixed asymmetric prototype shapes:

- fixed `t=2` decomposition
- `t=3` survival chain
- grouped `t=3` fold
- fixed polynomial evaluator
- outer assembly
- second outer pattern

This line showed the same main signal in more paper-shaped examples:

```text
tensor products unchanged
relin/rescale reduced
```

Reference:

- `docs/asym_proto_milestone_20_25.md`

### 4. Planner-Facing Line 26-33

The `26-33` line moved from handwritten evaluator shapes toward plan objects:

- explicit plan object smoke
- plan-driven executor
- plan table executor
- whitelisted planner
- coefficient-pattern planner
- exponent decomposition smoke
- generated-plan executor
- generated plans for two known patterns

This line established:

```text
coefficient pattern
  -> decomposition / whitelisted planning
  -> generated plan
  -> eager / lazy executor
```

It deliberately stayed finite and whitelisted. That was a project decision to preserve traceability.

Reference:

- `docs/asym_generated_plan_patterns_results.md`

### 5. Internal BSGS Line 34-35

The `34-35` line is the current strongest subject-level milestone.

It introduces:

- explicit `bar(S1)`
- explicit `hat(S1)`
- explicit `S2`
- named Internal BSGS block evaluation
- eager CKKS path
- inner-lazy CKKS path
- generated fixed InternalBsgsPlan from coefficient patterns

The current fixed structure is:

```text
t = 2
B = 4
bar(S1) = {x, x^2, x^3}
hat(S1) = {x^4, x^8, x^12}
S2 = {x^16}
```

The important result is stable across two fixed coefficient patterns:

```text
tensor products: 13 vs 13
relin count:     13 vs 9
rescale count:   13 vs 9
```

Reference:

- `docs/internal_bsgs_milestone_34_35.md`
- `docs/internal_bsgs_generated_plan_results.md`

## Current Code Organization

The new shared helper is intentionally small:

- `include/internal_bsgs_common.h`

It contains:

- fixed Internal BSGS data structures
- two fixed handwritten Internal BSGS plans
- two coefficient patterns
- fixed `t=2,B=4` exponent decomposition helpers
- generated-plan validation helpers

It does not contain the CKKS executor. The executor remains local to the experiment files for now. This keeps the verified experimental behavior easy to inspect and avoids a risky refactor before the deadline.

## Safe Thesis Claim

A safe thesis-level claim at the current stage is:

```text
We implemented a conservative CKKS/OpenFHE user-layer prototype inspired by the asymmetric BSGS polynomial-evaluation algorithm. In fixed Internal BSGS-shaped examples, the prototype preserves tensor-product count while reducing relin/rescale operations, and it validates correctness through plaintext shadow and CKKS decryption error checks.
```

The current repo should not claim:

```text
We implemented the full asymmetric BSGS algorithm.
```

The accurate wording is:

```text
We implemented a fixed, white-box, Internal BSGS-shaped generated-plan prototype that conservatively validates the materialization-reduction strategy under OpenFHE/CKKS user-layer constraints.
```

## Remaining Gaps

The main remaining gaps are:

- no arbitrary polynomial planner yet
- no automatic choice of `t`, `B`, or component sets
- no full base-`B` planner for broad exponent ranges
- no aggressive lazy propagation through the outer assembly
- no high-order ciphertext pipeline beyond validated shallow/inner-lazy paths
- no library-quality API for the Internal BSGS executor

These are expected gaps. They should be framed as future work or next-phase engineering work, not as failures of the current prototype.

## Recommended Next Steps

Near-term:

1. Keep the verified `34/35` behavior stable.
2. Avoid large refactors before the deadline.
3. If code cleanup is needed, keep it limited to small helpers like `internal_bsgs_common.h`.
4. Use the milestone documents as the basis for the thesis implementation chapter.

After the deadline pressure is lower:

1. Extract a reusable Internal BSGS executor helper.
2. Add a small planner that derives the fixed `t=2,B=4` sets from a bounded degree.
3. Explore a conservative outer-lazy variant.
4. Only then consider broader parameterization.
