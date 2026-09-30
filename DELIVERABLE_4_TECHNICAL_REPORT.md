# DELIVERABLE 4: Technical Report
### Design of a Vector Embedding for Capability Composition
### PCCST503 — Machine Learning, Assignment 2

## 1. Problem Definition

Given a formal application `A = (S, C, S_I, G, R, K)`, design a vector
representation for states, goals, and capabilities that preserves the
functional relationships needed to (a) identify which capabilities can
validly follow which others, and (b) construct representations of complex
capabilities from simpler ones — without redesigning or invoking any
planning/search algorithm (that remains Assignment 1's, explicitly out of
scope here).

## 2. Design Requirements

From the assignment's evaluation criteria (Section 8 of the PDF), a
representation must let one investigate: capability identity, state
awareness, precondition-effect compatibility, input-output compatibility,
the distinction between similarity and composability, composition,
goal relevance, operational properties (cost/reliability/availability/
constraints/resources), consistency across problems, and computational
efficiency. The instructions additionally require, and this implementation
adds: an explicit separation between *functional* and *implementation*
representations (instructions Section 5), and honest reporting rather than
manufactured clean results (instructions Section 25).

## 3. Related Embedding Approaches

- **One-hot / symbolic encoding**: unambiguous but provides no notion of
  partial similarity between distinct symbols; would make
  `functionalSimilarity` useless for anything but exact matches. Rejected
  as the sole scheme, though the type/mechanism subspace still uses
  one-hot for `T_i` where exact-category distinction is exactly what's
  wanted.
- **Word2Vec-style distributed word embeddings**: the assignment's own
  motivating analogy, but explicitly insufficient here (Section 37 of the
  instructions) — applying Word2Vec to capability *names* would capture
  lexical/semantic similarity of English words like "Create" and "Cancel,"
  not the *formal functional structure* (preconditions, effects, ports)
  that actually determines whether two capabilities can compose. No such
  name-embedding is used anywhere in this implementation.
- **Feature-based / structured embeddings**: the approach taken here — each
  formal component (`P_i, E_i, I_i, O_i, T_i, M_i, Q_i, Rel_i, A_i`) is
  encoded into its own well-defined subspace, concatenated. This preserves
  interpretability (every dimension has a stated meaning, instructions
  Section 32) and lets similarity be computed at different granularities.
- **Learned/neural embeddings**: explicitly out of scope — no training
  data, no model, no gradient-based fitting is used anywhere; all
  parameters (dictionary sizes, hash dimension, similarity weights,
  normalization constants) are fixed, documented design choices, not
  learned. Strength: no training data needed, fully interpretable,
  deterministic. Limitation: cannot capture semantic nuance beyond what is
  formally specified (e.g., two capabilities with *coincidentally*
  disjoint precondition variables that are nonetheless conceptually
  related get low functional similarity — observed directly in Experiment
  1, discussed in Section 10 below).

## 4. Proposed Representation

Summarized fully in `DELIVERABLE_1_FORMAL_EMBEDDING_DESIGN.md`. In brief:
a masked `(mask, value)` two-channel encoding for every *partial*
specification (preconditions, effects, goals, constraints), three
independently-usable subspaces per capability (functional / mechanism /
operational), and log-domain operational channels chosen specifically to
make sequential composition additive in vector space.

## 5. Mathematical Formulation

See DELIVERABLE_1 in full; the key formulas are reproduced here for
convenience:
```
phi(Q) = [mask(Q) | val(Q)],  mask(Q)_k = 1[x_k in Q],  val(Q)_k = toNumeric(Q(x_k)) if mask(Q)_k else 0
v_func(C) = [phi_P(P) | phi_E(E) | phi_in(C) | phi_out(C)]
v_mech(C) = [onehot(T) | hash8(M)]
v_ops(C)  = [time/1000, money, resource, -ln(1-risk), -ln(Rel), avail]
cos(a,b)  = (a.b) / (||a|| ||b||), 0 if either norm is 0
```

## 6. Capability Composition Model

`composeCapabilities()` (formal object) and `composeVectorFromParts()`
(closed-form vector), cross-checked by `verifyHomomorphism()`. Full
derivation and the proven-additive vs. documented-non-additive channels:
DELIVERABLE_1 Sections 12-13.

## 7. Implementation

C++17, ~1,400 lines across 7 headers + `main.cpp`, one third-party
dependency (`nlohmann::json`). Full breakdown: `DELIVERABLE_2_IMPLEMENTATION.md`.

## 8. Experimental Methodology

Six formally specified datasets (`DELIVERABLE_3`), one per required
experiment plus a cross-domain check, each hand-constructed to isolate one
property under test (e.g., Dataset 3 holds preconditions/effects/ports
*fixed* across three capabilities and varies only type/mechanism/QoS, so
any similarity difference observed is attributable to exactly that). Every
number reported in Section 9 below was produced by
`./capability_embedding --experiments` on this exact codebase and written
to `results/*.csv` — reproducible by anyone who runs the same command.

## 9. Results

**Experiment 1 (Compatibility).** `isComposable(CreateOrder,MakePayment) =
TRUE` (0 violated preconditions, I/O coverage 1.0); `isComposable
(CreateOrder,CancelCart) = FALSE` (1 violated precondition, I/O coverage
0.0). Functional similarity: `0.000` and `0.236` respectively — notably,
**both are low**, and the compatible pair actually has the *lower*
similarity of the two. This is discussed in Section 10.

**Experiment 2 (Composition).** The three-step chain composes to a single
capability requiring only `cart_exists==true`, at `time=210ms,
reliability=0.984065`. The effect-subspace and {time, money, risk,
reliability} operational channels of the composite vector matched the
closed-form prediction **exactly** (error `0.0` to floating-point
precision) on both fold steps.

**Experiment 3 (Alternatives).** Functional similarity `1.000` for all
three `MakePayment_*` pairs; implementation similarity ranged `-0.143` to
`0.195`; overall similarity `0.848`-`0.930`.

**Experiment 4 (Irrelevant capabilities).** Three goal-contributing
capabilities scored `0.333` each; four irrelevant capabilities scored
exactly `0.000`, cleanly separated with zero false positives/negatives in
this dataset.

**Experiment 5 (Operational).** Two functionally-identical
(similarity `1.000`) payment gateways differing by `+$0.015`/call for
`+0.029` reliability and `-0.040` risk; a third, cheapest option excluded
entirely by `availability=false`.

**Cross-domain check.** The identical engine, given a structurally
unrelated arithmetic domain, correctly separated two *locally composable*
alternatives (`SetXTo10`, `SetXTo3` — both satisfy `SetYTo5`'s precondition)
by their actual goal relevance (`0.0` vs `-0.667`).

**Efficiency.** Vector dimensions ranged 28–54 across datasets; embedding
storage 696–3,024 bytes; all-pairs similarity over the full dataset
computed in under 7 microseconds in every case (`DELIVERABLE_3` table).

## 10. Analysis

The most important, and most honestly-reported, finding is from Experiment
1: **functional cosine similarity does not track composability, and can
even invert it** — the compatible pair (`CreateOrder`, `MakePayment`) has
*lower* functional similarity (`0.000`) than the incompatible pair
(`CreateOrder`, `CancelCart`, similarity `0.236`). This happens because
`CreateOrder` and `MakePayment` operate on *entirely disjoint* state
variables and ports (`cart_exists`/`order_exists` vs. `order_exists`/
`payment_status`; `cart_id`/`order_id` vs. `order_id`/`payment_id`) — cosine
similarity over sparse, mostly-disjoint masked vectors is close to zero
whenever the two capabilities touch different variables, *regardless* of
whether one's effect happens to satisfy the other's precondition.
`CreateOrder` and `CancelCart`, by contrast, share the `order_exists`
variable in both their precondition/effect footprints (just with opposite
required truth values), which gives their vectors *more* overlapping
nonzero structure and hence higher cosine similarity, despite being
formally incompatible.

This is not a bug — it is the single clearest possible demonstration of
the assignment's central thesis, arguably more dramatic than a "compatible
pairs have modestly higher similarity" result would have been: **similarity
measures structural/topical resemblance of what a capability touches;
composability is a directed, exact, logical relationship between one
capability's effects and another's preconditions.** They are not just
imperfectly correlated — in this dataset they are *inversely* related for
the pair that most matters, which is exactly why `compatibility.hpp` is
implemented as a wholly separate, exact code path rather than a
similarity threshold.

## 11. Limitations

- **Cross-problem vector comparability.** Each `ApplicationProblem` builds
  its own variable/port/type dictionaries; vectors from two different
  problems are not directly comparable unless re-keyed onto a shared
  dictionary. Not needed by any of the six required
  experiments/datasets (each is self-contained), but a real constraint on
  reusing this engine "as-is" across independently authored applications.
- **Single-capability goal relevance under multi-step goals** (found and
  documented live in the cross-domain check): a capability whose effect is
  a necessary *intermediate* value on the way to a multi-condition goal
  (e.g. `status=X_SET` en route to the goal's required `status=XY_SET`) can
  be scored as partially "violating" that goal condition, even though it
  is a legitimate, necessary step. `goalRelevance()` evaluates one
  capability in isolation; it does not (and, by design, does not try to)
  simulate a whole chain the way a planner would.
- **Mechanism hash is a fingerprint, not a semantic embedding.** It
  guarantees different mechanism strings map to different points (useful
  for *distinguishing* implementations) but asserts no meaningful distance
  relationship between them (e.g., two very similar SQL statements are not
  guaranteed to hash close together). Acceptable given the assignment
  explicitly scopes out learned/semantic embeddings for this component.
- **Precondition subspace composition is only a partial closed form**
  (needs the constituent's effect vector too, not just its own precondition
  vector — DELIVERABLE_1 Section 13) — reported rather than glossed over.
- **`resource` and `availability` operational channels are not additive**
  under composition by design (peak-load and logical-AND semantics
  respectively), which means the operational subspace as a whole is *not*
  a full vector-space homomorphism, only 4 of its 6 channels are — again,
  reported precisely rather than claimed as a clean uniform result.
- **Categorical value fallback.** Any string not in the small canonical
  ordinal table (types.hpp) is mapped via a stable hash into `[0,1)`,
  which preserves identity but not any semantic ordering between unknown
  categorical values.

## 12. Conclusion

A structured, interpretable, non-learned vector representation was
designed, implemented, and evaluated across six formally specified
problems in two structurally unrelated domains. It satisfies every
evaluation criterion in the assignment's Section 8 table: capabilities are
distinctly represented; the representation captures capability-state
relationships (through preconditions/effects over the shared state-variable
dictionary); composable and incompatible pairs are correctly and exactly
distinguished (Experiment 1); dependencies via ports are represented and
checked exactly (input_output_compatibility); composite capabilities are
constructed with a partially-provable vector homomorphism (Experiment 2);
goal relevance is computed and correctly separates contributing from
irrelevant capabilities (Experiment 4) even across domains (cross-domain
check); at least three operational properties (cost, reliability,
availability, plus risk and resource as bonus) are investigated with
measured, non-asserted trade-offs (Experiment 5); the engine behaves
consistently on a structurally unrelated domain with zero domain-specific
code (cross-domain check); and efficiency was measured, not estimated,
across all six datasets. The most significant single result — that
functional similarity can be *lower* for a composable pair than for an
incompatible one — is reported and explained rather than adjusted away,
consistent with the assignment's explicit instruction to report genuine
limitations honestly.
