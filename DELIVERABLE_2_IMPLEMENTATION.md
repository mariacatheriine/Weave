# DELIVERABLE 2: Implementation
### PCCST503 — Machine Learning, Assignment 2

## 1. Architecture

```
Weave/
├── include/
│   ├── types.hpp          Value, State, Condition, Effect, Goal, Port,
│   │                      QualityAttributes, Capability, ApplicationProblem
│   ├── embedding.hpp       EmbeddingEngine: phi_S, phi_G, phi_C, similarity()
│   ├── compatibility.hpp   precondition_effect_compatibility, input_output_
│   │                      compatibility, goalRelevance -- EXACT, non-vector
│   ├── composition.hpp     composeCapabilities, composeVectorFromParts,
│   │                      verifyHomomorphism, composeChain
│   ├── dataset.hpp         JSON -> ApplicationProblem loader
│   ├── experiments.hpp     Experiments 1-5 + cross-domain check + efficiency
│   └── engine.hpp          printVector/inspectCapabilityVector, CsvWriter,
│                          measureEfficiency
├── src/
│   └── main.cpp            CLI: --vectors, --similarity, --compatibility,
│                          --compose, --experiments, --help
├── tests/
│   └── test_cases.cpp      35 validation tests (Section 45-46 of the
│                          instructions)
├── data/                   6 JSON datasets (5 experiment + 1 cross-domain)
├── results/                CSV output from --experiments
├── third_party/nlohmann/json.hpp   single-header JSON library (only dependency)
├── Makefile
├── README.md, USAGE.md
└── DELIVERABLE_1..4.md
```

No planning/search code exists anywhere in this project (verified:
`grep -rli "a_star\|astar\|dijkstra\|bfs\|dfs\|lpa_star\|d_star" include/ src/`
returns nothing).

## 2. Data structures

`Value` (types.hpp) is a small tagged union (`enum class Kind`) over
`bool/int64_t/double/std::string`, used instead of `std::variant` directly
so that `toNumeric()`/`asString()`/JSON round-tripping can be defined as
plain member functions rather than `std::visit` boilerplate — a
readability trade-off explicitly chosen for viva clarity (instructions
Section 48).

`State` wraps `unordered_map<string, Value>`. `Condition`/`Effect` are a
`(variable, op, value)` triple each, with `evaluate()`/`apply()` doing the
obvious thing. `Capability` is the literal 11-tuple, field-for-field.
`ApplicationProblem` holds `S_I`, `G`, `C`, `R`, `K` together with a
`find(id)` helper (linear scan — fine at the scale these datasets are, see
Complexity below).

## 3. Embedding engine

`EmbeddingEngine` owns three growable dictionaries (`varIndex`, `typeIndex`,
`portIndex`), populated once by `initialize(app)` by scanning every state
variable/type/port name that appears anywhere in the problem. Every vector
produced afterward is defined relative to these fixed dictionaries — this
is *why* `initialize()` must run before any `encode*()` call, and why two
different `ApplicationProblem`s generally produce vectors of *different*
dimension (each has its own dictionary) — comparing vectors across
completely different problems is therefore only meaningful after re-keying
onto a shared dictionary, a known, documented limitation (see
DELIVERABLE_4 Limitations).

`MaskedVector{mask, val}` is the two-channel primitive from DELIVERABLE_1
Section 4, with a `.flatten()` that concatenates `[mask|val]` for use inside
the full capability vector, while compatibility.hpp/composition.hpp work
with the unflattened `{mask,val}` pair directly (they need `mask` and `val`
as separate arrays, not one interleaved blob).

`EmbeddingEngine::CapabilityVector` bundles `functional`, `mechanism`,
`operational` (the three subspaces) plus the unflattened `pre`/`eff`/`in`/
`out` pieces, so composition.hpp can operate on exactly the pieces it needs
without re-deriving them.

## 4. Compatibility engine

Deliberately kept in a **separate file, on raw Capability structs**, never
routed through cosine similarity — see DELIVERABLE_1 Section 10 and the
design-philosophy note at the top of compatibility.hpp. The one shared
primitive, `checkConditionsAgainstEffects`, is reused for both
precondition-effect compatibility and goal relevance (DELIVERABLE_1 Section
11) — one code path answering two different formal questions, which is
also directly useful in a viva ("how many different ways do you check
condition satisfaction?" -> "one, reused twice").

## 5. Composition engine

`composeCapabilities()` builds the actual composite `Capability` (the "real
answer"). `composeVectorFromParts()` builds the composite's vector via
closed-form update rules directly from the constituents' vectors (the
"investigative answer" to Section 14). `verifyHomomorphism()` cross-checks
the two independently computed results and reports the maximum
floating-point discrepancy — used both in `tests/test_cases.cpp` (asserted
< 1e-6) and printed live in Experiment 2's console output, so the claim is
demonstrated on every run, not just asserted once in a hidden test.

## 6. Dataset loader

`loadProblem(path)` (dataset.hpp) parses the JSON schema documented in
DELIVERABLE_3, with explicit, caught error paths for: file-not-found,
malformed JSON (nlohmann's parse exception is caught and rewrapped with the
file path), a missing `"capabilities"` array, an empty/duplicate capability
id. None of these crash the program (see `tests/test_cases.cpp`'s edge-case
tests, and `main.cpp`'s `try/catch` around every load).

## 7. Experiment harness

`experiments.hpp` implements all five required experiments as free
functions taking a dataset path and a `CsvWriter&`, plus a cross-domain
consistency check and an efficiency report; `runAll()` (called by
`--experiments`) wires all six together and writes six CSV files under
`results/`. Every printed number is computed from a value read at runtime
from the loaded JSON — no experiment result is a literal constant in the
source (verified by inspection; also see instructions Section 25's
"do not fake results" and the honestly-reported partial/limitation cases in
Experiments 2 and the cross-domain check, rather than only clean successes).

## 8. CLI

```
./capability_embedding <dataset.json> --vectors
./capability_embedding <dataset.json> --similarity <id1> <id2>
./capability_embedding <dataset.json> --compatibility <id1> <id2>
./capability_embedding <dataset.json> --compose <id1> <id2> [id3 ...]
./capability_embedding --experiments
./capability_embedding --help
```
Every mode validates its arguments and prints a clear error (never a stack
trace / segfault) for unknown capability ids, missing files, or malformed
JSON — verified interactively (see DELIVERABLE_4 / the final verification
transcript).

## 9. Key API signatures (assignment Section 7 / Deliverable 2 requirement)

```cpp
// embedding.hpp
std::vector<double> EmbeddingEngine::encode(const State&) const;
std::vector<double> EmbeddingEngine::encode(const Goal&) const;
std::vector<double> EmbeddingEngine::encode(const Capability&) const;
static double EmbeddingEngine::similarity(const std::vector<double>&, const std::vector<double>&);
double EmbeddingEngine::functionalSimilarity(const Capability&, const Capability&) const;
double EmbeddingEngine::implementationSimilarity(const Capability&, const Capability&) const;
double EmbeddingEngine::overallSimilarity(const Capability&, const Capability&) const;

// compatibility.hpp
CompatibilityResult preconditionEffectCompatibility(const Capability&, const Capability&);
IOCompatibilityResult inputOutputCompatibility(const Capability&, const Capability&);
FullCompatibility compatibility(const Capability&, const Capability&);
GoalRelevance goalRelevance(const Capability&, const Goal&);

// composition.hpp
Capability composeCapabilities(const Capability&, const Capability&);
Capability composeChain(const std::vector<Capability>&);
CompositeVectorParts composeVectorFromParts(const EmbeddingEngine::CapabilityVector&, const EmbeddingEngine::CapabilityVector&);
HomomorphismCheck verifyHomomorphism(const EmbeddingEngine&, const Capability&, const Capability&);
```

## 10. Complexity (see DELIVERABLE_1 Section 15 for the summary; derived here)

Let `d_s, d_p, d_t` be the state/port/type dictionary sizes for a given
problem, `|P_i|,|E_i|,|I_i|,|O_i|` a capability's own precondition/effect/
input/output counts.

- `encode(State)`: one pass over `d_s` variables — `O(d_s)`.
- `encode(Capability)`: building the masked vectors is `O(|P_i|+|E_i|)` to
  populate plus `O(d_s)` to allocate/zero them (dominates) — `O(d_s + d_p +
  d_t)` overall.
- `cosine(a,b)`: `O(dim(a))`.
- `preconditionEffectCompatibility(C1,C2)`: `O(|P2| * |E1|)` (nested scan;
  no index was built since these counts are small per capability in every
  dataset here — documented as the scaling limit if a future capability had
  hundreds of preconditions).
- `inputOutputCompatibility(C1,C2)`: `O(|I2| * |O1|)`, same caveat.
- `composeCapabilities(C1,C2)`: `O(|P1|+|P2|+|E1|+|E2|+|I1|+|I2|+|O1|+|O2|)`.
- `composeChain(L capabilities)`: `O(L)` compose calls, each bounded as
  above.
- Pairwise comparison across `N` capabilities (Experiment 3, efficiency
  report): `O(N^2 * d)` for cosine-based comparisons, `d` the relevant
  vector's dimension — measured directly by `measureEfficiency()`, not
  estimated (DELIVERABLE_3 has the actual numbers for every dataset).

## 11. Memory

`measureEfficiency()` reports `totalEmbeddingBytes = numCapabilities *
fullVectorDim * sizeof(double)` — an exact count of the embedding storage
for the loaded problem (not counting the raw Capability structs
themselves, which are comparatively small and JSON-derived). Measured, not
invented, per dataset (DELIVERABLE_3).

## 12. Dependencies

C++17 standard library only, plus the single-header
`third_party/nlohmann/json.hpp` (the same library the professor's example
projects use, reused as-is — it is infrastructure, not part of the
assignment's intellectual content). No other third-party code.

## 13. Build process

`make` builds both `capability_embedding` and `test_runner`, or invoke
`g++ -std=c++17 -O2 -Iinclude -I. src/main.cpp -o capability_embedding`
directly. `make test` runs the validation suite; `make experiments` runs
`--experiments`. Verified to build with zero warnings under `-Wall -Wextra`
on GCC 13.3.0 / Ubuntu 24.04 (see USAGE.md for full command reference).
