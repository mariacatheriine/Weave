# DELIVERABLE 3: Experimental Dataset
### PCCST503 — Machine Learning, Assignment 2

Six formally specified `ApplicationProblem` instances, one per required
experiment plus one for the cross-domain consistency check. Every field
below is taken directly from the corresponding `data/*.json` file — nothing
here is aspirational or simplified relative to what the code actually
loads and runs.

---

## Dataset 1 — `data/compatibility.json` (Experiment 1)

**Problem:** `OrderCompatibilityCheck`, domain E-Commerce Order Fulfillment.
**State variables:** `cart_exists`(bool), `cart_item_count`(int),
`order_exists`(bool), `payment_status`(string).
**Initial state:** `cart_exists=true, cart_item_count=2, order_exists=false,
payment_status=NOT_STARTED`.
**Goal:** `order_exists==true, payment_status==SUCCESS`.
**Resources:** `database, payment_gateway`.

| Capability | Preconditions | Effects | Inputs | Outputs | Type | Reliability | Risk |
|---|---|---|---|---|---|---|---|
| CreateOrder | `cart_exists==true` | `order_exists=true` | `cart_id:UUID` (req.) | `order_id:UUID` | SERVICE | 0.99 | 0.01 |
| MakePayment | `order_exists==true` | `payment_status=SUCCESS` | `order_id:UUID` (req.) | `payment_id:UUID` | API | 0.995 | 0.02 |
| CancelCart | `order_exists==false` | `cart_exists=false` | `cart_id:UUID` (req.) | — | SERVICE | 0.99 | 0.01 |

**Relationship under test:** `CreateOrder`'s effect directly satisfies
`MakePayment`'s precondition (`order_exists==true`) — composable. The same
effect directly **conflicts** with `CancelCart`'s precondition
(`order_exists==false`) — incompatible. This is the assignment PDF's own
worked example, reproduced exactly (Section 7 of the PDF).

---

## Dataset 2 — `data/composition.json` (Experiment 2)

**Problem:** `CompletePurchaseAndInvoice`.
**State variables:** `cart_exists, cart_item_count, order_exists,
order_status, payment_status, invoice_generated`.
**Initial state:** `cart_exists=true, cart_item_count=3, order_exists=false,
order_status=NOT_STARTED, payment_status=NOT_STARTED,
invoice_generated=false`.
**Goal:** `order_status==PAID, payment_status==SUCCESS,
invoice_generated==true`.
**Resources:** `database, payment_gateway, invoice_service`.

| Capability | Preconditions | Effects | I/O | QoS (time,money,risk) | Reliability |
|---|---|---|---|---|---|
| CreateOrder | `cart_exists==true` | `order_exists=true, order_status=CREATED` | out: `order_id` | 50ms, $0, 0.01 | 0.99 |
| MakePayment_API | `order_exists==true` | `payment_status=SUCCESS, order_status=PAID` | in: `order_id`(req), out: `payment_id` | 120ms, $0.02, 0.02 | 0.995 |
| GenerateInvoice | `payment_status==SUCCESS` | `invoice_generated=true` | in: `payment_id`(req), out: `invoice_id` | 40ms, $0, 0.005 | 0.999 |

**Relationship under test:** the full three-step chain
`CreateOrder -> MakePayment_API -> GenerateInvoice` composes into a single
capability requiring **only** `cart_exists==true` externally (both
intermediate preconditions are internally guaranteed), producing all four
accumulated effects, at `time=210ms, money=$0.02, reliability=0.984065`
(measured `= 0.99*0.995*0.999`). This is where the vector homomorphism
(DELIVERABLE_1 Section 13) is checked live, twice (once per fold step).

---

## Dataset 3 — `data/alternatives.json` (Experiment 3)

**Problem:** `PaymentAlternativeImplementations`.
**State variables:** `order_exists, payment_status`.
**Initial state:** `order_exists=true, payment_status=NOT_STARTED`.
**Goal:** `payment_status==SUCCESS`.
**Resources:** `database, payment_gateway, ui_session`.

| Capability | Type | Mechanism | Time | Money | Risk | Reliability |
|---|---|---|---|---|---|---|
| MakePayment_API | API | `POST /payments` | 120ms | $0.02 | 0.02 | 0.995 |
| MakePayment_DB | DATABASE | `INSERT INTO payments...` | 40ms | $0 | 0.01 | 0.999 |
| MakePayment_GUI | GUI | `CLICK #pay-now-button` | 8000ms | $0.02 | 0.08 | 0.95 |

All three share **identical** preconditions, effects, and I/O ports
(`order_id` in, `payment_id` out) — only type/mechanism/QoS differ.

**Relationship under test:** functional similarity ≈ 1.000 for every pair
(measured); implementation similarity is markedly lower and pair-specific
(measured: API-vs-DB = -0.035, API-vs-GUI = 0.195, DB-vs-GUI = -0.143 —
negative values occur because the mechanism hash-fingerprint has no
enforced sign structure, which is fine since it only needs to
*distinguish*, not *order*, mechanisms); overall similarity stays high
(0.85-0.93) without ever reaching 1.0, so the three are never treated as
identical.

---

## Dataset 4 — `data/irrelevant.json` (Experiment 4)

**Problem:** `GoalRelevanceFiltering`.
**State variables:** `cart_exists, order_exists, payment_status,
notification_sent, wishlist_updated, audit_logged,
recommendation_generated, marketing_email_sent`.
**Goal:** `order_exists==true, payment_status==SUCCESS,
notification_sent==true`.
**Resources:** `database, payment_gateway, notification_service`.

Three goal-contributing capabilities (`CreateOrder, MakePayment,
SendNotification`, chained via preconditions on each other exactly as in
Dataset 2's pattern) plus four **irrelevant** capabilities that touch only
non-goal variables: `AuditLog` (`audit_logged`), `UpdateWishlist`
(`wishlist_updated`), `GenerateRecommendation`
(`recommendation_generated`), `SendMarketingEmail`
(`marketing_email_sent`).

**Relationship under test:** the three goal-contributing capabilities each
score `0.333` (`1/3` goal conditions satisfied, `0` violated); all four
irrelevant capabilities score exactly `0.000` with zero satisfied *and*
zero violated — the representation separates them by *reason*, not just by
raw score, without running any search.

---

## Dataset 5 — `data/operational.json` (Experiment 5)

**Problem:** `OperationalTradeoffs`.
**Goal:** `payment_status==SUCCESS, package_dispatched==true`.
**Resources:** `payment_gateway_a/b/c, carrier_express, carrier_economy`.

Two independent operational trade-offs, investigating **three** operational
attributes (cost, reliability, availability) plus risk and time as a bonus
fourth/fifth:

| Capability | Time | Money | Reliability | Risk | Available |
|---|---|---|---|---|---|
| PaymentGateway_Premium | 150ms | $0.020 | 0.999 | 0.010 | yes |
| PaymentGateway_Budget | 90ms | $0.005 | 0.970 | 0.050 | yes |
| PaymentGateway_Unavailable | 70ms | $0.003 | 0.960 | 0.060 | **no** |
| Dispatch_Express | 200ms | $12.50 | 0.995 | 0.020 | yes |
| Dispatch_Economy | 4000ms | $2.00 | 0.985 | 0.040 | yes |

**Relationship under test:** the two payment gateways are functionally
identical (measured similarity 1.000) yet trade cost against reliability
and risk; the third, cheapest option is unusable purely because
`availability=false` — availability is a hard gate, not a weighted trade-off
axis. Shipping options provide a second, independent time-vs-money
trade-off. No option is asserted "universally better."

---

## Dataset 6 — `data/cross_domain_arithmetic.json` (cross-domain consistency)

**Problem:** `ArithmeticRegisterAssignment`, domain "Arithmetic State
Transitions" — a domain with **zero** structural relationship to
e-commerce, run through the exact same engine with no domain-specific code.
**State variables:** `x, y (real), status (string), log_count (int)`.
**Initial state:** `x=0, y=0, status=INIT, log_count=0`.
**Goal:** `x==10, y==5, status==XY_SET`.

| Capability | Preconditions | Effects |
|---|---|---|
| SetXTo10 | `status==INIT` | `x=10, status=X_SET` |
| SetXTo3 | `status==INIT` | `x=3, status=X_SET` |
| SetYTo5 | `status==X_SET` | `y=5, status=XY_SET` |
| LogCheckpoint | (none) | `log_count=1` |

**Relationship under test:** `SetXTo10` and `SetXTo3` are **both** locally
composable with `SetYTo5` (identical precondition satisfaction), yet only
`SetXTo10` genuinely advances toward the goal — `goalRelevance` correctly
ranks them differently (`0.0` vs `-0.667`) even though `isComposable` alone
cannot. `LogCheckpoint` scores exactly `0.0` (irrelevant). This experiment
also surfaced and **documents** (rather than hides) a real limitation: a
single capability's score can be diluted by an intermediate "staging"
effect value that a later step will further change (see
DELIVERABLE_4 Limitations).

---

## Measured efficiency across all six datasets (one program run)

| Dataset | Capabilities | State dim | Port dim | Full vector dim | Storage (bytes) | All-pairs similarity (µs) |
|---|---|---|---|---|---|---|
| OrderCompatibilityCheck | 3 | 4 | 3 | 38 | 912 | 1.25 |
| CompletePurchaseAndInvoice | 3 | 6 | 4 | 49 | 1,176 | 1.40 |
| PaymentAlternativeImplementations | 3 | 2 | 2 | 29 | 696 | 0.90 |
| GoalRelevanceFiltering | 7 | 8 | 1 | 54 | 3,024 | 6.86 |
| OperationalTradeoffs | 5 | 3 | 0 | 28 | 1,120 | 2.22 |
| ArithmeticRegisterAssignment | 4 | 4 | 0 | 32 | 1,024 | 1.64 |

All values measured live by `measureEfficiency()` on the run that produced
this document (reproducible via `./capability_embedding --experiments`) —
none are estimated or invented (instructions Section 35).
