# Usage Guide — Vector Embedding for Capability Composition

Assumes Linux (tested Ubuntu 24.04 / GCC 13.3.0). Run all commands from the
project root.

## 1. Build

```bash
make
```
This builds two binaries: `capability_embedding` (the CLI) and
`test_runner` (the validation suite). Verified to compile with zero
warnings under `-Wall -Wextra`.

## 2. Run the validation tests

```bash
make test
```
or directly: `./test_runner`. Expect `35 passed, 0 failed.`

## 3. Inspect a dataset's embeddings

```bash
./capability_embedding data/compatibility.json --vectors
```
Prints every capability's embedding, subspace by subspace, with every
dimension labeled (precondition mask/value, effect mask/value, input/output
ports, mechanism, operational).

## 4. Compare two capabilities' similarity

```bash
./capability_embedding data/alternatives.json --similarity MakePayment_API MakePayment_DB
```
Prints `functionalSimilarity`, `implementationSimilarity`, and
`overallSimilarity` — the three granularities described in
`DELIVERABLE_1_FORMAL_EMBEDDING_DESIGN.md`.

## 5. Check formal compatibility (not similarity)

```bash
./capability_embedding data/compatibility.json --compatibility CreateOrder CancelCart
```
Prints the precondition-effect compatibility (satisfied/violated/neutral
counts, the hard `isComposable` verdict) and the input-output coverage —
two independent signals, never merged into one opaque score.

## 6. Compose a chain of capabilities

```bash
./capability_embedding data/composition.json --compose CreateOrder MakePayment_API GenerateInvoice
```
Prints the resulting composite's preconditions, effects, aggregated QoS,
and (for a 2-capability chain) the live vector-homomorphism check.

## 7. Run all five required experiments plus the cross-domain check

```bash
./capability_embedding --experiments
```
or `make experiments`. This:
- Runs Experiment 1 (Compatibility) on `data/compatibility.json`
- Runs Experiment 2 (Composition) on `data/composition.json`
- Runs Experiment 3 (Alternative Implementations) on `data/alternatives.json`
- Runs Experiment 4 (Irrelevant Capabilities) on `data/irrelevant.json`
- Runs Experiment 5 (Operational Attributes) on `data/operational.json`
- Runs the cross-domain consistency check on `data/cross_domain_arithmetic.json`
- Prints an efficiency/complexity measurement table across all six datasets
- Writes machine-readable results to:
  - `results/compatibility_results.csv`
  - `results/composition_results.csv`
  - `results/alternative_results.csv`
  - `results/irrelevant_results.csv`
  - `results/operational_results.csv`
  - `results/cross_domain_results.csv`

## 8. Help

```bash
./capability_embedding --help
```

## 9. Clean build artifacts

```bash
make clean
```

## 10. Interpreting results/*.csv

Every CSV column corresponds directly to a value printed in the
console narrative for that experiment — e.g.
`compatibility_results.csv`'s `isComposable` column is `1`/`0` for the
same hard verdict shown as `TRUE`/`FALSE` in the console output. This lets
you either read the experiment narrative for understanding, or load the
CSV directly (e.g. into a spreadsheet or a plotting script) for further
analysis, without re-running anything.
