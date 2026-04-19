# Asymmetric BSGS for CKKS/OpenFHE: Implementation Notes

## 0. Status of this document

This document is an implementation-oriented summary of the paper:

- **A Novel Asymmetric BSGS Polynomial Evaluation Algorithm under Homomorphic Encryption**

Its purpose is **not** to reproduce the paper verbatim.
Its purpose is to give a **paper-faithful but engineering-safe** guide for implementing related ideas in the current OpenFHE/CKKS user-layer repo.

This document follows one hard rule:

- **Anything explicitly claimed as a paper result must match the paper.**
- **Anything that is an engineering decision for this repo must be marked as a project decision, not a paper claim.**

---

# Part I. What the paper actually does

## 1. Problem setting

The paper studies **high-degree polynomial evaluation under leveled FHE**.

It discusses BGV, BFV, and CKKS as the relevant leveled FHE families.
Its focus is not on changing the underlying HE scheme itself, but on changing the **polynomial evaluator** used on ciphertexts.

The motivating perspective is:

- leveled FHE supports arithmetic operations
- many higher-level encrypted computations reduce to polynomial evaluation
- high-degree polynomial evaluation is expensive
- in practice, the expensive part is not only the algebraic multiplication count, but also the associated key/modulus management

This is the paper’s starting point.

---

## 2. What the paper says is expensive

The paper emphasizes that in leveled FHE, a homomorphic multiplication is not really an atomic operation.

Conceptually, it involves:

1. tensor product
2. modulus switching / rescaling / level management
3. relinearization / key switching

The paper’s practical claim is that **key-switching and modulus-switching are the most expensive components**, because they require costly conversions and operations such as NTT/INTT-related work.

Therefore, the paper’s optimization target is:

- **not mainly reducing the number of tensor products**
- **but reducing the number of key switches and modulus switches**

This distinction must be preserved in implementation.

---

## 3. The paper’s comparison baseline

The paper compares against:

- the classical BSGS idea
- the Peterson–Stockmeyer algorithm as the best-known classical recursive polynomial evaluator in this family

The paper explicitly states that the Peterson–Stockmeyer algorithm can evaluate a degree-`d` polynomial using approximately:

- `sqrt(2d) + O(log d)` non-scalar multiplications

and that the ordinary BSGS-style approach is around:

- slightly more than `2 * sqrt(d)` homomorphic multiplications

The paper does **not** claim to break the classical lower bound on non-scalar multiplication count.
Instead, it keeps the tensor-product count in `O(sqrt(d))` while reducing key/modulus switching.

---

## 4. The key observation: baby-step values are shallower than giant-step values

This is the core structural insight.

### Classical BSGS setup

For a polynomial `f(X)` of degree `d`, one writes:

`f(X) = sum_{j=0}^{m-1} f^{(j)}(X) * X^{jk}`

with `k ≈ m ≈ sqrt(d)`.

Then define:

- baby-step set  
  `S1 = {1, x, x^2, ..., x^{k-1}}`

- giant-step set  
  `S2 = {1, z, z^2, ..., z^{m-1}}`, where `z = x^k`

Each `f^{(j)}(x)` can be computed using only linear combinations of the already-precomputed baby-step powers, and then the final result is assembled as:

`f(x) = sum_j y_j * z^j`

where `y_j = f^{(j)}(x)`.

### What changes under leveled FHE

The paper points out that:

- `S1` is generally located at **shallower multiplicative depth**
- `S2` is located at **deeper multiplicative depth**

Therefore, when multiplying `y_j` by `z^j`, one usually needs to push `y_j` down to the level of `z^j`.

This is the asymmetry that the paper exploits.

---

## 5. Important non-claim: the paper does not solve this for Peterson–Stockmeyer recursion

This point is easy to misstate and must not be misstated.

The paper explicitly says that extending the same idea to the recursive Peterson–Stockmeyer algorithm, **while preserving multiplicative depth**, remains an open question.

Why?

Because in Peterson–Stockmeyer recursion:

- the baby-step set is reused across recursive layers
- if you relax its noise management and deepen it, that depth increase propagates upward

Therefore:

- the paper’s actual construction is a new **asymmetric BSGS**
- it is **not** "a small tweak of Peterson–Stockmeyer recursion"

This distinction must remain explicit in all implementation notes and code comments.

---

## 6. Standard lazy vs the paper’s aggressive lazy

The paper first recalls an ordinary lazy / hoisting idea:

If one needs to compute a sum of products

`sum_i ct_i * ct'_i`

then instead of immediately doing switching after each product, one can:

1. compute the tensor product of each pair
2. sum those intermediate results
3. do only one modulus switch and key switch at the end

This is standard lazy materialization.

### The paper’s stronger move

The paper goes further.

Given `t` ciphertexts:

`ct_0, ct_1, ..., ct_{t-1}`

it proposes to:

- compute their consecutive tensor product
- perform **no modulus switching** in the middle
- perform **no key switching** in the middle
- and only switch once at the end

As a result, the intermediate ciphertext is no longer a linear ciphertext.
It becomes a ciphertext polynomial over `R_q[s]` of degree at most `t`.

The paper explicitly says this more aggressive lazy style is the key to its construction.

---

## 7. The set decomposition used by the paper

Let the polynomial degree be `d`.
Define:

- `l = ceil(log d)`
- `l' = l - (t - 1)`

Assume, for simplicity of exposition, that `t` divides `l'`.
Then define:

- `l'' = l' / t`
- `B = 2^(l'')`

The paper then defines the small baby-step component sets:

`S1^(j) = { x^e | e = i * B^j, i in [B] }`, for `j in [t]`

and the outer giant-step set:

`S2 = { x^e | e = i * B^t, i = 0, 1, ..., floor(d / B^t) }`

The paper proves that any needed power `x^e` can be represented via base-`B` decomposition, so that:

`S ⊆ S1^(0) × S1^(1) × ... × S1^(t-1) × S2`

where the set product means multiplicative combination, not Cartesian product.

This is the algebraic backbone of the algorithm.

---

## 8. Why `l' = l - (t - 1)` is chosen

The paper’s reason is very specific.

The aggressive lazy tensor chain increases effective noise / capacity usage by an amount comparable to about `t - 1` levels.

So the idea is:

- compute the small component sets only up to level `l'`
- leave `t - 1` levels of "headroom"
- use that headroom to absorb the delayed materialization cost

The paper’s argument is:
the continuous tensor product of `t` ciphertexts at level `l'` has noise magnitude not exceeding that of a ciphertext at level `l' + (t - 1)`.

This is exactly why `l'` is set to `l - (t - 1)`.

---

## 9. How the paper reconstructs baby-step values

This is the most important implementation-relevant structural step.

The paper defines the large baby-step range:

`S1 = {1, x, x^2, ..., x^(2^(l') - 1)}`

Each target exponent `e` in this range is written in base `B`:

`e = e_0 + e_1 B + ... + e_(t-1) B^(t-1)`

Then:

1. each small set `S1^(j)` is first constructed normally
2. all ciphertexts inside each `S1^(j)` are switched to the same level `l'`
3. the ciphertexts corresponding to the digits `e_j` are selected
4. they are multiplied by **consecutive tensor products**
5. no modulus switch and no key switch is performed in the middle

So the "baby-step object" is no longer a standard linear ciphertext.
It becomes a degree-`t` ciphertext polynomial.

This is not an incidental detail.
It is the core mechanism.

---

## 10. Why the paper does not simply materialize the entire large baby-step set

Because that would be too expensive.

If one explicitly combines all component sets to build all of `S1`, then the number of tensor products would blow up to `O(d)`, which defeats the purpose.

So the paper adds another structural layer.

---

## 11. Internal BSGS

The paper splits the large baby-step set into two grouped sets:

- `bar(S1)`
- `hat(S1)`

where:

- `bar(S1)` contains the lower half of the base-`B` exponent structure
- `hat(S1)` contains the upper half

The paper then uses a smaller ordinary BSGS procedure internally:

- `bar(S1)` acts as the inner baby-step set
- `hat(S1)` acts as the inner giant-step set

This gives an **Internal BSGS Subroutine**.

Then the outer asymmetric algorithm uses that subroutine to evaluate each outer block polynomial `f^(i)(X)`.

So the paper’s full algorithm is **not**:
- "construct all baby-step powers and then do one final fold"

It is:
- construct small component sets
- delay materialization aggressively
- organize those results into an internal BSGS
- then do an outer BSGS-like assembly

---

## 12. Algorithms in the paper

### Algorithm 1: Internal BSGS Subroutine
This is the inner BSGS evaluator.

It takes:
- a polynomial
- a baby-step-like set
- a giant-step-like set

and computes the polynomial value using the familiar BSGS decomposition internally.

### Algorithm 2: Asymmetric BSGS Algorithm
This is the main single-polynomial algorithm.

It:
1. computes `l`, `l'`, `B`
2. builds each `S1^(j)`
3. modulus-switches each `S1^(j)` to level `l'`
4. builds the outer set `S2`
5. forms `bar(S1)` and `hat(S1)` using tensor products only
6. decomposes the polynomial into outer blocks
7. evaluates each block using Internal BSGS
8. multiplies each block result by the corresponding outer giant-step power
9. sums the results

### Algorithm 3: Multiple polynomials on the same ciphertext
The paper observes that steps 1–19 of Algorithm 2 are independent of the specific polynomial.
So if several polynomials are to be evaluated on the same ciphertext, the expensive precomputation can be reused.

This matters for engineering design:
the precomputation layer and the polynomial-specific evaluation layer should conceptually be separated.

---

## 13. Paper complexity claims

The paper’s main asymptotic claim for a degree-`d` polynomial and small constant `t` is:

- `O(d)` homomorphic scalar multiplications
- `O(d)` homomorphic additions
- `O(sqrt(d))` tensor products
- `O(d^(1/t))` key switches
- `O(d^(1/t))` modulus switches

This is the paper’s central complexity improvement.

Important:
the paper explicitly says it does **not** beat the lower bound on non-scalar multiplication count.
Tensor products remain in `O(sqrt(d))`.

What is reduced is the number of **expensive switches**.

---

## 14. High-order key-switching requirement

This is one of the most important practical consequences.

Inside the paper’s main algorithm, the result `y_i` of Internal BSGS is already a ciphertext polynomial of degree at most `t`.

When this `y_i` is then multiplied by the corresponding outer giant-step ciphertext, the intermediate result becomes degree at most `t + 1`.

Therefore, to relinearize it, the paper explicitly requires key-switching material for:

`KSK_{s^i -> s}` for `i = 2, 3, ..., t + 1`

That is a much stronger requirement than ordinary "only support `s^2 -> s`" relinearization.

The paper explicitly notes that this enlarges the relinearization key size by a factor on the order of `t`.

This must not be hidden or softened in implementation planning.

---

## 15. Example 3.3 in the paper

The paper includes a small illustration with `t = 2` only as an explanatory example.

The paper also explicitly says:
- in practical algorithms, one should choose `t >= 3`

In the example:
- polynomial degree is 32
- `l = 5`
- `l' = 4`
- `B = 4`

Then:
- `S1^(0) = {x, x^2, x^3}`
- `S1^(1) = {x^4, x^8, x^12}`
- `S2 = {x^16, x^32}`

The polynomial is written in outer blocks with `z = x^16`, and each block is evaluated by the internal BSGS over the two baby-step groups.

This example is pedagogical.
It is not a claim that `t = 2` is the intended practical optimum.

---

## 16. The paper’s experimental status

The paper reports experimental instantiation using **HElib**.
It performs experiments on parameter sets from prior BGV/BFV bootstrapping and amortized-bootstrapping contexts.

The paper also says that preliminary experiments found `t = 4` to perform best for most tested polynomial degrees in their setting.

Important:
this is a paper-specific experimental outcome under their setting.
It does **not** automatically mean our OpenFHE/CKKS user-layer project should begin at `t = 4`.

---

## 17. What the paper says about CKKS specifically

The paper treats the algorithm as generally applicable to leveled FHE, including CKKS.

It also provides CKKS-specific consecutive tensor-product analysis in its appendix.
The relevant conclusions are:

- if two CKKS ciphertexts have precisions `p` and `p'`
- then after tensor product, the resulting precision is approximately

  `p + p' - log(2^p + 2^(p'))`

- for the consecutive tensor product of `t` CKKS ciphertexts, each having precision `p`,
  the resulting precision becomes approximately:

  `p - log t`

- the scaling factor becomes `Δ^t`
- the capacity overhead is comparable to consuming `t - 1` modulus levels
- the precision loss is comparable to the binary-tree method

This is the paper’s theoretical basis for saying the algorithm also applies to CKKS.

---

## 18. What the paper did **not** implement

This must remain explicit.

The paper states that the asymmetric BSGS algorithm can also be applied to CKKS bootstrapping and is expected to provide similar performance improvements.

However, the paper explicitly says:

- **they did not implement that CKKS bootstrapping optimization**
- **they leave it for future work**

Therefore:

- the paper gives theory and algorithmic rationale
- but it does **not** give us a finished CKKS/OpenFHE implementation recipe

This repo must supply that engineering evidence itself.

---

# Part II. What this means for our OpenFHE/CKKS repo

## 19. Hard separation: paper theory vs repo reality

In this repo, we must always separate:

### Paper-level facts
These are things the paper explicitly states.

### Repo-level facts
These are things currently present in the repository.

### Repo-level validated facts
These are things that have actually been re-built / re-run in the current repo state.

No code or design decision should silently cross these layers.

---

## 20. Safe project interpretation

For this repo, the safest interpretation of the paper is:

> The paper teaches us that in FHE polynomial evaluation, we should treat key-switch/rescale/modulus-management as first-class optimization targets, and that shallower baby-step material can sometimes be lazily materialized and only later relinearized/rescaled.

This project-safe interpretation is faithful to the paper.

What is **not** safe to assume immediately is:

- full arbitrary-`t` aggressive asymmetric BSGS is already realizable in current OpenFHE user-layer code
- all higher-order ciphertext polynomial operations are already validated in this repo
- high-order relinearization keys are already available or convenient in the current repo

Those are engineering questions, not paper facts.

---

## 21. Conservative implementation policy for this repo

### Policy 1: start from shallow-lazy, not full paper-strength asymmetric BSGS
The first implementation stage should only aim to validate:

- delayed materialization
- shorter NoRelin chains
- one-fold or small-block lazy aggregation
- reduction in relin/rescale count relative to a naive baseline

This is paper-consistent and engineering-safe.

### Policy 2: keep intermediate order low unless newly validated
Because the paper’s full algorithm may require degree-`t` and degree-`t+1` ciphertext polynomials plus corresponding high-order key switching, we should not assume that this repo can immediately jump there.

The project default should be:

- keep intermediate materialization shallow
- keep NoRelin chains short
- expand only after repo-local legality is tested

### Policy 3: baseline-first design
Every lazy evaluator should have a nearby naive or eager-materialization baseline.

The comparison target should not only be final numerical correctness.
It should also include:
- relin count
- rescale count
- state trace
- level evolution
- noiseScaleDeg evolution

This is how the paper’s optimization objective maps into repo-local experiments.

---

## 22. What should count as success in our repo

A repo-local lazy prototype should be considered successful if:

1. it computes the correct polynomial value to acceptable CKKS precision
2. it materially reduces relin/rescale count relative to a matched naive baseline
3. its level and noiseScaleDeg trace are understandable
4. it does not rely on unvalidated higher-order operations
5. it is reproducible in the current repo build

This definition is faithful to the paper’s spirit while staying conservative.

---

## 23. What should **not** count as success

The following do **not** count as success:

- code that numerically works but does not reduce materialization count
- code that claims to "implement the paper" while actually just reordering ordinary eager operations
- code that silently assumes support for high-order relinearization keys
- code that jumps directly to a full paper topology without trace or legality checks
- code that confuses "possible in the paper" with "validated in this repo"

---

# Part III. Concrete implementation guidance for Codex in this repo

## 24. Default workflow for any new evaluator work

When adding or modifying evaluator code in this repo, follow this order:

1. **Reality check**
   - confirm current repo files
   - confirm current helper utilities
   - confirm current evaluator/lazy/baseline state
   - confirm git status if possible

2. **Reuse current helpers**
   - prefer existing helper code
   - do not create a parallel helper framework if not necessary

3. **Match against baseline**
   - if adding a lazy evaluator, identify the matched naive/eager version
   - preserve an apples-to-apples comparison

4. **Add trace**
   - level
   - noiseScaleDeg
   - explicit raw / norelin / relin / rescale step naming
   - timing if cheap to add

5. **Add plaintext comparison**
   - expected plaintext polynomial evaluation
   - max absolute error
   - optionally mean absolute error

6. **Keep the patch narrow**
   - prefer a new `ckks_evaluator_03_*` file
   - avoid destabilizing old experiments

---

## 25. Preferred near-term implementation targets

The recommended next-step order is:

### Stage A. Full re-validation of current targets
Before new algorithm growth, ensure the current targets still build and run in the current working tree.

### Stage B. White-box paired experiments
Maintain paired versions:
- naive / eager materialization
- lazy / delayed materialization

### Stage C. Slightly richer small evaluator
Add a very small new evaluator, such as:
- degree-3 or degree-4 polynomial
- or two quadratic blocks
- still with short lazy chains only

### Stage D. More explicit state-trace survival tests
Before moving toward more paper-like structures, validate:
- longer NoRelin chains
- legality of subsequent ct-ct use
- relin-then-rescale vs rescale-then-relin choices, if relevant
- behavior under FIXEDMANUAL vs COMPOSITESCALINGMANUAL

### Stage E. Only then explore more paper-like decomposition patterns
At this stage, and only after local validation, consider:
- grouped baby-step blocks
- multi-block delayed materialization
- partial internal-BSGS-like organization

---

## 26. What not to implement prematurely

Do **not** prematurely implement:

- full general-`t` asymmetric BSGS
- arbitrary high-order ciphertext polynomial machinery
- claims that we now support `KSK_{s^i -> s}` for all `i=2..t+1`
- a giant "one file does everything" evaluator
- broad refactors of helper or CMake structure

These are too risky before repo-local legality and stability are established.

---

## 27. How to describe current project scope correctly

When summarizing the repo’s goal, use wording like:

> We are conservatively porting the paper’s evaluator-level optimization idea into OpenFHE/CKKS user-layer experiments. The immediate focus is not full paper-strength asymmetric BSGS, but validated shallow-lazy / delayed-materialization prototypes that reduce relin/rescale count while preserving numerical correctness and traceability.

Do **not** use wording like:

> We have already implemented the paper’s full asymmetric BSGS in CKKS.

unless that becomes actually true and validated.

---

## 28. How to write code comments safely

Good code comment:
- "This experiment tests a conservative lazy-materialization pattern inspired by the asymmetric BSGS paper."

Bad code comment:
- "This is the paper’s asymmetric BSGS algorithm."

Good code comment:
- "The paper allows degree-`t` intermediate ciphertext polynomials; this repo currently validates only a shallower restricted form."

Bad code comment:
- "OpenFHE supports the full paper pipeline here."

---

# Part IV. Minimal paper-faithful reference summary

## 29. One-paragraph paper summary

The paper proposes a new **asymmetric BSGS** polynomial-evaluation algorithm for leveled FHE. Its main idea is to exploit the fact that baby-step values are generally shallower than giant-step values, to decompose baby-step computation into smaller sets, and to use aggressive delayed materialization so that many expensive key-switch and modulus-switch operations are postponed or merged. The algorithm keeps tensor products in `O(sqrt(d))` but reduces key-switch and modulus-switch complexity to `O(d^(1/t))` for small constant `t >= 3`. It provides BGV/BFV experiments and CKKS noise/precision analysis, but leaves CKKS bootstrapping implementation for future work.

---

## 30. One-paragraph repo-safe implementation summary

In this repo, the safe implementation reading of the paper is: optimize **materialization schedule**, not just algebraic multiplication count. Start from small, traceable, shallow-lazy user-layer evaluators in OpenFHE/CKKS; compare them against matched naive baselines; measure relin/rescale reduction and numerical stability; and only after repo-local legality is revalidated, move toward more paper-like grouped baby-step and internal-BSGS structures.

---

## 31. Final rule

If there is ever a conflict between:
- "paper-faithful high ambition"
and
- "repo-local validated engineering conservatism"

choose the second first.

The purpose of this repo is not to claim the strongest version soonest.
The purpose is to build a correct, explainable, reproducible path from paper idea to CKKS/OpenFHE evidence.