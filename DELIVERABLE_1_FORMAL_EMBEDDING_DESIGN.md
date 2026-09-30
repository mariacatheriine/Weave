# DELIVERABLE 1: Formal Embedding Design
### PCCST503 — Machine Learning, Assignment 2

## 1. Formal entities restated

`A = (S, C, S_I, G, R, K)`. A state `S = {(x_1,v_1),...,(x_n,v_n)}`. A goal
`G = {g_1,...,g_m}` is a **partial** state specification. A capability is
the 11-tuple `C_i = (T_i, I_i, O_i, P_i, E_i, K_i, R_i, Q_i, Rel_i, A_i, M_i)`.
All are implemented verbatim in `include/types.hpp`.

## 2. Variable dictionary

Let `Vars` be the set of all state-variable names appearing anywhere in a
problem `P = (S,G,C)` — in the initial state, the goal, or any capability's
preconditions/effects/constraints. Fix an arbitrary but stable order
`x_1,...,x_n = Vars` (`n = |Vars|`); this ordering is what makes every
vector produced afterward comparable to every other. Built once per problem
by `EmbeddingEngine::initialize()`.

Similarly fix a **port-name dictionary** `Ports` (union of every input/output
port name across all capabilities) and a **type dictionary** `Types` (union
of every `T_i` seen). `d_s = |Vars|`, `d_p = |Ports|`, `d_t = |Types|`.

## 3. phi_S(S): raw state encoding

`phi_S(S) in R^{d_s}`, `phi_S(S)_k = toNumeric(S(x_k))` if `x_k in dom(S)`,
else `0`. A concrete state specifies every variable it tracks, so there is
no "unspecified" ambiguity to resolve at this level — masking (Section 4) is
needed only for **partial** specifications.

`toNumeric` (Value::toNumeric, types.hpp) maps: `bool -> {0,1}`,
`int/double -> value`, and `string ->` a documented canonical-ordinal table
for the recurring status-lifecycle strings used in these datasets
(`NOT_STARTED->0, CREATED->1, ..., FAILED->-1, CANCELLED->-2`, full table in
types.hpp), falling back to a **stable hash** in `[0,1)` for any string not
in that table. The hash fallback carries **no ordinal meaning** — it only
guarantees two different unknown strings map to (almost certainly)
different numbers, which is enough for equality-based Condition checks
(the only place raw numeric state values are compared) but is explicitly
*not* claimed to preserve any ordering for unknown categorical values. This
is a documented limitation, not a hidden assumption.

## 4. The masked (mask, value) encoding — phi for partial specifications

A precondition set, an effect set, or a goal is **not** a full state — most
variables are simply unmentioned, and instructions Section 9 is explicit:
*"do not treat unspecified variables as equivalent to false."* We therefore
encode any such condition set `Q` (over the fixed dictionary `Vars`) as
**two** parallel vectors, not one:

```
mask(Q) in {0,1}^{d_s},   mask(Q)_k = 1  iff  x_k is mentioned in Q
val(Q)  in R^{d_s},       val(Q)_k  = toNumeric(required/produced value of x_k)  if mask(Q)_k = 1
                                    = 0                                          otherwise (never read)
```//

Flattened, `phi(Q) = [mask(Q) | val(Q)] in R^{2 d_s}`. This is used for:

- **phi_P(P_i)** — preconditions (`encodePreconditionsMasked`)
- **phi_E(E_i)** — effects, `val_k` = the *resulting* value the effect sets
  (`encodeEffectsMasked`)
- **phi_G(G)** — goals (`encodeGoalMasked`), since a goal is formally
  identical in shape to a precondition set (both are `Condition` lists a
  state must satisfy)
- **phi_K(K_i)** — capability-specific constraints (`encodeConstraintsMasked`)

**Why two channels, not the bipolar `{+1,-1,0}` scheme in the assignment
materials?** The bipolar scheme is unambiguous for *booleans*
(`+1`=required-true, `-1`=required-false, `0`=don't-care) but for a
*numeric* variable it cannot distinguish "don't care" from "required value
= 0" — both would encode as `0`. The explicit mask channel resolves this
for every value kind at the cost of doubling dimensionality per variable, a
documented, deliberate trade-off (Section 9 of the assignment explicitly
permits any masked scheme that avoids this ambiguity, without prescribing
which one).

## 5. Input/Output port subspace

Over the port dictionary `Ports` (`d_p = |Ports|`):

```
phi_in(C_i)_k  = 1.0   if port k is a REQUIRED input of C_i
               = 0.5   if port k is an OPTIONAL input of C_i
               = 0.0   otherwise
phi_out(C_i)_k = 1.0   if port k is an output of C_i
               = 0.0   otherwise
```

This vector is a **correlational proxy** for I/O compatibility, not the
formal ground truth — the exact ground truth (name **and** type match) is
computed directly on the raw `Port` structures by
`inputOutputCompatibility()` in `compatibility.hpp` (Section 9 below). The
vector is still useful: it lets cosine similarity pick up shared-port
structure, and Experiment 1 demonstrates the two signals agree.

## 6. Functional subspace

```
v_func(C_i) = [ phi_P(P_i) | phi_E(E_i) | phi_in(C_i) | phi_out(C_i) ]  in R^{4 d_s + 2 d_p}
```
(the input/output blocks are each `d_p`-dimensional, so the total is
`2*(2*d_s) + 2*d_p = 4d_s + 2d_p`; the report uses `d_s` generically since
`d_p` is a separate, smaller dictionary in practice).

This is the ONLY subspace `functionalSimilarity()` uses.

## 7. Mechanism (implementation) subspace

```
v_type(C_i) in {0,1}^{d_t}          one-hot over Types
v_hash(C_i) in [-1,1]^{8}            deterministic hash-projection of M_i (mechanism string)
v_mech(C_i) = [ v_type(C_i) | v_hash(C_i) ]
```
`v_hash` is **not** a semantic embedding of the mechanism string (that would
need learned representations, explicitly out of scope, Section 37) — it is
a coarse **fingerprint**: identical mechanism strings map to identical
vectors; different strings map (with overwhelming probability) to
different vectors. Its only job is to make `implementationSimilarity()`
capable of *distinguishing* different mechanisms, not to assert any
semantic relationship between them.

## 8. Operational subspace (log-domain, see Deliverable 1 Section 6 below for why)

```
v_ops(C_i) = [ time_i/1000, money_i, resource_i, riskExposure_i, unrelExposure_i, avail_i ]  in R^6
  time_i/1000        -- milliseconds rescaled to "seconds" units (fixed constant, not fitted)
  money_i            -- currency units, unscaled
  resource_i         -- raw resource-cost units (peak, not cumulative -- see composition)
  riskExposure_i     = -ln(1 - risk_i)          (see Deliverable 1 Section 6)
  unrelExposure_i    = -ln(reliability_i)       (see Deliverable 1 Section 6)
  avail_i            = 1.0 if A_i else 0.0
```

## 9. Full capability vector and three similarity granularities

```
v(C_i) = [ v_func(C_i) | v_mech(C_i) | v_ops(C_i) ]
```

```
functionalSimilarity(C1,C2)     = cos( v_func(C1), v_func(C2) )
implementationSimilarity(C1,C2) = cos( v_mech(C1), v_mech(C2) )
overallSimilarity(C1,C2)        = cos( [w_f*v_func | w_m*v_mech | w_o*v_ops](C1),
                                        [w_f*v_func | w_m*v_mech | w_o*v_ops](C2) )
                                   with w_f=1.0, w_m=0.35, w_o=0.15
```
`cos(a,b) = (a . b) / (||a|| ||b||)`, `0` if either vector has zero norm
(never NaN — tested explicitly, `tests/test_cases.cpp`).

`w_f > w_m, w_o` is the direct implementation of instructions Section 5:
*"functional information must dominate implementation details."* The
weights are a stated design choice, not fitted to any data; Experiment 3
reports functional/implementation/overall **separately**, precisely so a
reader can see exactly what the weighting does and does not change, rather
than trusting one opaque blended number.

## 10. Compatibility (NOT a vector operation — Deliverable 1 Section 9)

`compatibility.hpp` computes two **exact**, formal signals directly from
the raw `Capability` structures:

**precondition_effect_compatibility(C1,C2)**: for every precondition
`p in P2`, check `C1`'s effects: if some effect on `p`'s variable produces
exactly `p`'s required value, `p` is *satisfied*; if it produces a
*different* value under an equality condition, `p` is *violated*
(a genuine conflict); otherwise (untouched, or an inequality condition on a
touched variable) it is *neutral*.
```
isComposable(C1,C2) = (violatedCount == 0)                         -- HARD rule
degree(C1,C2) = (satisfiedCount - violatedCount) / |P2|  in [-1,1]  -- soft ranking signal only
```
A single genuine conflict makes `isComposable` false regardless of how many
other preconditions are satisfied — deliberately not an averaged score,
because a real conflict should never be "outvoted."

**input_output_compatibility(C1,C2)**: `coverage = matchedRequired /
totalRequired` (1.0 if `C2` has no required inputs), where a match requires
**exact name AND type equality** between one of `C2`'s inputs and one of
`C1`'s outputs.

These two signals are reported **separately**, never silently merged into
one number — the assignment's own worked example
(`CreateOrder -> MakePayment` composable purely through a precondition/
effect link, with **no shared port at all**) shows I/O compatibility is not
required for composability; it is an independent, additional signal
(evaluation criterion table, assignment Section 8, row "Input–output
compatibility").

## 11. Goal relevance (Section 6.1 property 7)

`goalRelevance(C,G)` reuses the identical primitive
(`checkConditionsAgainstEffects`) used for precondition-effect
compatibility, treating `G`'s conditions exactly like a downstream
capability's preconditions — formally the same kind of object.
```
score(C,G) = (satisfiedGoalConditions - violatedGoalConditions) / |G|
```
`score > 0` => goal-contributing; `score == 0` with `satisfied==violated==0`
=> irrelevant (never touches a goal variable); `score < 0` => actively
conflicts with the goal.

## 12. Composition — the composite Capability (Section 13)

Given `C1: S0->S1`, `C2: S1->S2`, `composeCapabilities(C1,C2)` builds:
- `preconditions(C12) = P1 union { p in P2 : p not guaranteed by E1 }`
  (a precondition of `C2` already directly satisfied by `C1`'s effects is
  dropped — the worked example from instructions Section 13: a chain
  requiring `A`, producing `B`, then requiring `B`, producing `C`, should
  require only `A` externally, not `A` and `B`).
- `effects(C12)`: `E1`, then every effect of `E2` **overrides** any prior
  effect on the same variable (later effect wins).
- `inputs(C12) = I1 union { i in I2 : i not satisfied by O1 }` — this fixes
  a real bug found in the professor's Example 3 reference implementation,
  whose `compose()` silently discarded `C2`'s own unsatisfied inputs
  entirely (`comp.inputs = c1.inputs`), understating what a composite
  actually still needs from outside.
- `outputs(C12) = O1 union O2` (a downstream capability may still depend on
  an output produced partway through the chain).
- `constraints(C12) = K1 union K2` (conservative: both remain active).
- `resources(C12) = R1 union R2` (deduplicated).
- `time(C12) = time(C1)+time(C2)`, `money(C12) = money(C1)+money(C2)`
  (additive: sequential wait/spend).
- `resource(C12) = max(resource(C1), resource(C2))` (peak concurrent load,
  **not** cumulative — the two steps do not hold resources
  simultaneously in a purely sequential composition; a deliberate,
  documented exception to the "everything sums" pattern).
- `risk(C12) = 1 - (1-risk(C1))(1-risk(C2))` (probability at least one step
  fails, assuming independent risk events).
- `Rel(C12) = Rel(C1) x Rel(C2)` (independent sequential success
  probabilities — matches the assignment PDF's own stated formula
  directly).
- `A(C12) = A(C1) AND A(C2)` (both must be available).

## 13. Composition — the composite VECTOR, and what is provably linear

This is the assignment's central investigative question (Section 14):
*"does `v(C2 o C1)` have a meaningful relationship with `v(C1)`, `v(C2)`?"*
The answer is derived and **checked programmatically**, not asserted:

**Effect subspace — EXACT closed form (a genuine algebraic homomorphism):**
```
mask_eff(C12)[k] = mask_eff(C1)[k]  OR  mask_eff(C2)[k]
val_eff(C12)[k]  = val_eff(C2)[k]   if mask_eff(C2)[k]=1  else  val_eff(C1)[k]
```
This needs *only* `v_eff(C1)` and `v_eff(C2)` — no access to the raw
Capability structs — because a SET effect's final value at index `k` is
literally "whatever the last capability that touches `k` set it to," which
is exactly what mask-driven override encodes. `composition.hpp`'s
`composeEffectsVector()` implements this, and `verifyHomomorphism()`
cross-checks it against re-encoding the actual composite Capability from
scratch — bit-for-bit identical in every run (`tests/test_cases.cpp`,
Experiment 2).

**Operational subspace — 4 of 6 channels are EXACT closed forms:**
```
time(C12)/1000  = time(C1)/1000 + time(C2)/1000            ADDITIVE
money(C12)      = money(C1) + money(C2)                     ADDITIVE
riskExposure(C12)   = riskExposure(C1) + riskExposure(C2)    ADDITIVE  (log-domain risk)
unrelExposure(C12)  = unrelExposure(C1) + unrelExposure(C2)  ADDITIVE  (log-domain reliability)
resource(C12)   = max(resource(C1), resource(C2))            NOT additive (documented exception)
avail(C12)      = min(avail(C1), avail(C2))                  NOT additive (logical AND)
```
The riskExposure/unrelExposure channels are additive **specifically
because** we store `-ln(1-risk)` and `-ln(reliability)` rather than the raw
probabilities: `-ln(1-risk(C12)) = -ln((1-risk(C1))(1-risk(C2))) =
-ln(1-risk(C1)) + -ln(1-risk(C2))`, and likewise
`-ln(Rel(C12)) = -ln(Rel(C1)*Rel(C2)) = -ln(Rel(C1)) + -ln(Rel(C2))`. This
is precisely why the operational subspace is defined in log-domain in
Section 8 above — it is a deliberate encoding choice made *in order to*
achieve this additivity, not a coincidence discovered afterward.

**Precondition subspace — only a PARTIAL closed form (an honestly reported
limitation, not concealed):** deciding whether a precondition of `C2`
"survives" into the composite requires checking it against `C1`'s
*effects*, so the closed form needs `v_pre(C1)`, `v_pre(C2)`, **and**
`v_eff(C1)` together — it is still computable purely from vectors (no raw
structs needed, `composePreconditionsVector()`), but it is not a function of
`v_pre(C1)` and `v_pre(C2)` *alone* the way the effect subspace is. This
distinction is reported explicitly in Experiment 2's output rather than
glossed over.

## 14. Recursive composition

`C123 = C3 o C2 o C1` folds left-to-right: `composeChain({C1,C2,C3}) =
composeCapabilities(composeCapabilities(C1,C2), C3)`. Associativity is not
claimed or needed — the assignment only requires a well-defined sequential
result, which left-fold composition provides for any chain length.

## 15. Efficiency

See DELIVERABLE_2 Section "Complexity" for the full derivation;
summary: encoding one capability is `O(d_s + d_p + d_t)`; a similarity
computation is `O(d)` for the relevant subspace's dimension `d`; composing
a chain of length `L` is `O(L * (|P|+|E|+|I|+|O|))`; comparing `N`
capabilities pairwise is `O(N^2 d)`, measured directly in
`measureEfficiency()` (see DELIVERABLE_3 for actual numbers per dataset).
