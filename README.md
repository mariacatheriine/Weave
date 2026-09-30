# Weave - Vector Embedding for Capability Composition

**PCCST503 — Machine Learning, Assignment 2**

A C++17 system that encodes formally specified **states**, **goals**, and
**capabilities** into vector representations, and investigates whether
those vectors preserve the functional relationships needed to (a) identify
compatible capabilities and (b) construct complex capabilities from simple
ones — **without** any path-search or planning algorithm (that is
Assignment 1's job, explicitly out of scope here).

## Student Details

Name: Catherine Maria Benny

Register Number: TCR24CS020

Course: Machine Learning

## Problem Statement

Given a formal application `A = (S, C, S_I, G, R, K)`, design, implement,
and evaluate a vector representation for states, goals, and capabilities
`C_i = (T_i, I_i, O_i, P_i, E_i, K_i, R_i, Q_i, Rel_i, A_i, M_i)` such that
the representation supports: capability identity, precondition-effect
compatibility, input-output compatibility, composition, goal relevance, and
operational-property trade-offs (cost, reliability, availability, risk).

## Architecture

```
include/types.hpp          Formal data model: State, Goal, Capability (11-tuple)
include/embedding.hpp       phi_S, phi_G, phi_C -- masked encoding + 3 subspaces
include/compatibility.hpp   Exact compatibility functions (NOT vector similarity)
include/composition.hpp     Compose operation + provable vector homomorphism
include/dataset.hpp         JSON -> ApplicationProblem loader
include/experiments.hpp     The 5 required experiments + cross-domain check
include/engine.hpp          Vector inspection printing, CSV export, efficiency
src/main.cpp                CLI
tests/test_cases.cpp        35 validation tests
data/*.json                 6 datasets (5 experiments + 1 cross-domain)
results/*.csv               Experiment output (generated, reproducible)
```

## Embedding Design (full derivation: `DELIVERABLE_1_FORMAL_EMBEDDING_DESIGN.md`)

- **Masked `(mask, value)` encoding** for every *partial* specification
  (preconditions, effects, goals): an explicit `mask` channel says whether a
  variable is constrained at all, so "don't care" is never confused with
  "required false/zero" — a refinement of the bipolar `{+1,-1,0}` scheme
  that resolves its ambiguity for numeric variables.
- **Three separated subspaces** per capability — `functional`
  (preconditions/effects/I-O ports), `mechanism` (type + a mechanism-string
  fingerprint), `operational` (time/money/resource/risk/reliability/
  availability) — so "are these the same operation" and "are these built
  the same way" are independently answerable
  (`functionalSimilarity()` vs. `implementationSimilarity()`).
- **Log-domain operational channels**: storing `-ln(1-risk)` and
  `-ln(reliability)` instead of the raw probabilities makes sequential
  composition **additive** in vector space for those channels (proved and
  tested, not just claimed — see `verifyHomomorphism()`).
- **Compatibility is never computed from cosine similarity.** It is a
  separate, exact function over the raw formal structures
  (`compatibility.hpp`) — Experiment 1 shows these two notions can actually
  *disagree* in direction, which is the whole point.

## Directory Structure

See the tree above; full detail in `DELIVERABLE_2_IMPLEMENTATION.md`.

## Requirements

- A C++17 compiler (built and tested with GCC 13.3.0 on Ubuntu 24.04).
- No external dependencies beyond the bundled single-header
  `third_party/nlohmann/json.hpp`.

## Compilation

```bash
make
```
or directly:
```bash
g++ -std=c++17 -O2 -Iinclude -I. src/main.cpp -o capability_embedding
g++ -std=c++17 -O2 -Iinclude -I. tests/test_cases.cpp -o test_runner
```

## Running

```bash
./capability_embedding data/compatibility.json --vectors
./capability_embedding data/compatibility.json --similarity CreateOrder MakePayment
./capability_embedding data/compatibility.json --compatibility CreateOrder CancelCart
./capability_embedding data/composition.json --compose CreateOrder MakePayment_API GenerateInvoice
./capability_embedding --experiments
```
Full command reference: `USAGE.md`.

## Experiments

1. **Capability Compatibility** — `CreateOrder -> MakePayment` (composable)
   vs. `CreateOrder -> CancelCart` (incompatible), the assignment's own
   worked example.
2. **Capability Composition** — a 3-step chain folded into one composite,
   with a live-verified vector homomorphism for the effect and
   time/money/risk/reliability subspaces.
3. **Alternative Implementations** — three `MakePayment_*` capabilities
   (API/DB/GUI) that are functionally identical but implementation-distinct.
4. **Irrelevant Capabilities** — goal-contributing vs. irrelevant
   capabilities separated by `goalRelevance()`, with zero search.
5. **Operational Attributes** — cost/reliability/risk/availability
   trade-offs across payment gateways and shipping carriers, with no option
   claimed universally best.

Plus a **cross-domain consistency check** on a structurally unrelated
arithmetic domain, using the exact same engine with zero domain-specific
code.

## Example Commands and Output Interpretation

`--vectors` prints every capability's embedding broken down by labeled
subspace (precondition mask/value, effect mask/value, input/output ports,
mechanism, operational) so every dimension's meaning is traceable.
`--experiments` prints a full narrative for each experiment and writes
`results/*.csv` with the same numbers, so results are both human-readable
and machine-checkable. A `homomorphism check: EXACT` line in Experiment 2's
output means the composite vector, built two independent ways, agreed to
floating-point precision on that run.

## Verification

35/35 validation tests pass (`make test`); the whole pipeline (build, test,
all six experiments, CSV export) was run end-to-end on this exact codebase
— see the final response for the full transcript.

## Limitations

See `DELIVERABLE_4_TECHNICAL_REPORT.md` Section 11 for the complete,
honestly-reported list — most notably that functional similarity can
actually be *lower* for a composable pair than for an incompatible one in
these datasets (Experiment 1), which is discussed as the central finding
rather than a defect to hide.
