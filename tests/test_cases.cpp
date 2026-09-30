// =============================================================================
// tests/test_cases.cpp -- validation tests (assignment instructions Section
// 45-46). Minimal, dependency-free assert-based runner, matching Section 47
// ("do not overengineer").
// =============================================================================
#include <cmath>
#include <iostream>
#include <string>
#include "compatibility.hpp"
#include "composition.hpp"
#include "dataset.hpp"
#include "embedding.hpp"

using namespace capembed;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool cond, const std::string& name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::cout << "[PASS] " << name << "\n"; }
    else { ++g_fail; std::cout << "[FAIL] " << name << (detail.empty() ? "" : " -- " + detail) << "\n"; }
}
} // namespace

// --- Test 1: Valid compatibility (CreateOrder -> MakePayment) ---
static void test1_ValidCompatibility() {
    auto app = loadProblem("data/compatibility.json");
    auto* c1 = app.find("CreateOrder");
    auto* c2 = app.find("MakePayment");
    auto r = preconditionEffectCompatibility(*c1, *c2);
    check(r.isComposable, "Test1: CreateOrder->MakePayment is composable");
    check(r.violatedCount == 0, "Test1: zero violated preconditions");
}

// --- Test 2: Invalid compatibility (CreateOrder -> CancelCart) ---
static void test2_InvalidCompatibility() {
    auto app = loadProblem("data/compatibility.json");
    auto* c1 = app.find("CreateOrder");
    auto* c3 = app.find("CancelCart");
    auto r = preconditionEffectCompatibility(*c1, *c3);
    check(!r.isComposable, "Test2: CreateOrder->CancelCart is NOT composable");
    check(r.violatedCount == 1, "Test2: exactly one violated precondition");
}

// --- Test 3: Composition correctness (C3 o C2 o C1) ---
static void test3_CompositionCorrectness() {
    auto app = loadProblem("data/composition.json");
    auto c1 = *app.find("CreateOrder");
    auto c2 = *app.find("MakePayment_API");
    auto c3 = *app.find("GenerateInvoice");
    Capability composite = composeChain({c1, c2, c3});
    // The composite must require ONLY cart_exists externally (order_exists,
    // payment_status are internally guaranteed by earlier steps).
    check(composite.preconditions.size() == 1, "Test3: composite has exactly 1 external precondition",
          "got " + std::to_string(composite.preconditions.size()));
    check(composite.preconditions[0].variable == "cart_exists", "Test3: the surviving precondition is cart_exists");
    check(composite.effects.size() == 4, "Test3: composite has all 4 accumulated effects");
    bool hasInvoice = false;
    for (auto& e : composite.effects) if (e.variable == "invoice_generated") hasInvoice = true;
    check(hasInvoice, "Test3: composite effects include invoice_generated");
}

// --- Test 4: Reliability multiplication ---
static void test4_ReliabilityMultiplication() {
    auto app = loadProblem("data/composition.json");
    auto c1 = *app.find("CreateOrder");
    auto c2 = *app.find("MakePayment_API");
    Capability comp = composeCapabilities(c1, c2);
    double expected = c1.reliability * c2.reliability;
    check(std::abs(comp.reliability - expected) < 1e-9, "Test4: reliability(composite) == 0.99 x 0.995",
          std::to_string(comp.reliability) + " vs " + std::to_string(expected));
}

// --- Test 5: Cost addition ---
static void test5_CostAddition() {
    auto app = loadProblem("data/composition.json");
    auto c1 = *app.find("CreateOrder");
    auto c2 = *app.find("MakePayment_API");
    Capability comp = composeCapabilities(c1, c2);
    check(std::abs(comp.qos.timeMs - (c1.qos.timeMs + c2.qos.timeMs)) < 1e-9, "Test5: time(composite) == time(C1)+time(C2)");
    check(std::abs(comp.qos.money - (c1.qos.money + c2.qos.money)) < 1e-9, "Test5: money(composite) == money(C1)+money(C2)");
}

// --- Test 6: Input/output type mismatch ---
static void test6_IOTypeMismatch() {
    Capability a, b;
    a.outputs = { Port::fromJson(json{{"name","order_id"},{"type","UUID"}}) };
    b.inputs = { Port::fromJson(json{{"name","order_id"},{"type","STRING"}, {"required", true}}) }; // same name, different type
    auto r = inputOutputCompatibility(a, b);
    check(r.matchedRequired == 0, "Test6: name match with type mismatch does NOT count as satisfied");
    check(r.coverage == 0.0, "Test6: coverage is 0 when the only required input has a type mismatch");
}

// --- Test 7: Unavailable capability ---
static void test7_UnavailableCapability() {
    auto app = loadProblem("data/operational.json");
    auto* c = app.find("PaymentGateway_Unavailable");
    check(c != nullptr, "Test7: dataset loads the unavailable-gateway capability");
    check(!c->availability, "Test7: PaymentGateway_Unavailable.availability == false");
    State s = app.initialState;
    check(!c->isApplicable(s), "Test7: isApplicable() is false purely due to availability, even though preconditions hold");
}

// --- Test 8: Goal-relevance behavior ---
static void test8_GoalRelevanceBehavior() {
    auto app = loadProblem("data/irrelevant.json");
    auto* createOrder = app.find("CreateOrder");
    auto* auditLog = app.find("AuditLog");
    auto grContrib = goalRelevance(*createOrder, app.goal);
    auto grIrrelevant = goalRelevance(*auditLog, app.goal);
    check(grContrib.contributesToGoal(), "Test8: CreateOrder is flagged as goal-contributing");
    check(!grIrrelevant.contributesToGoal() && !grIrrelevant.conflictsWithGoal(),
          "Test8: AuditLog is flagged as neither contributing nor conflicting (irrelevant)");
    check(std::abs(grIrrelevant.score - 0.0) < 1e-9, "Test8: irrelevant capability scores exactly 0.0");
}

// --- Additional edge cases (Section 46) ---
static void test_EmptyVectorCosine() {
    std::vector<double> empty;
    check(EmbeddingEngine::cosine(empty, empty) == 0.0, "Edge: cosine of two empty vectors is 0.0, not NaN/crash");
}
static void test_ZeroNormVector() {
    std::vector<double> a = {0, 0, 0};
    std::vector<double> b = {1, 2, 3};
    check(EmbeddingEngine::cosine(a, b) == 0.0, "Edge: cosine with a zero-norm vector is 0.0, not NaN");
}
static void test_MissingStateVariable() {
    State s;
    s.set("x", Value(1.0));
    Value v = s.getOr("y", Value(-999.0));
    check(v.toNumeric() == -999.0, "Edge: getOr returns the provided default for a missing variable");
}
static void test_UnspecifiedGoalVariableNotFalse() {
    // A goal that only mentions 'a' must not treat 'b' as required-false.
    Goal g;
    g.conditions.push_back(Condition::fromJson(json{{"variable","a"},{"op","=="},{"value", true}}));
    EmbeddingEngine eng;
    eng.registerVariable("a");
    eng.registerVariable("b");
    auto mv = eng.encodeGoalMasked(g);
    check(mv.mask[eng.varIndex["a"]] > 0.5, "Edge: goal mask is 1 for the mentioned variable 'a'");
    check(mv.mask[eng.varIndex["b"]] < 0.5, "Edge: goal mask is 0 (don't-care), not 'required false', for unmentioned 'b'");
}
static void test_DuplicateCapabilityId() {
    bool threw = false;
    try {
        json j;
        j["capabilities"] = json::array({ json{{"id","X"}}, json{{"id","X"}} });
        std::ofstream f("/tmp/dup_test.json");
        f << j.dump();
        f.close();
        loadProblem("/tmp/dup_test.json");
    } catch (const std::exception&) { threw = true; }
    check(threw, "Edge: duplicate capability id in a dataset throws a clear error, not silent overwrite");
}
static void test_MalformedJson() {
    bool threw = false;
    try {
        std::ofstream f("/tmp/malformed_test.json");
        f << "{not valid json";
        f.close();
        loadProblem("/tmp/malformed_test.json");
    } catch (const std::exception&) { threw = true; }
    check(threw, "Edge: malformed JSON throws a clear error rather than crashing");
}
static void test_MissingCapabilitiesField() {
    bool threw = false;
    try {
        std::ofstream f("/tmp/nocaps_test.json");
        f << "{\"problemName\":\"X\"}";
        f.close();
        loadProblem("/tmp/nocaps_test.json");
    } catch (const std::exception&) { threw = true; }
    check(threw, "Edge: a dataset with no 'capabilities' array throws a clear error");
}
static void test_ConflictingEffectsOverride() {
    // In a chain, a later effect on the same variable must OVERRIDE an
    // earlier one, not both remain active.
    Capability c1, c2;
    c1.id = "A"; c2.id = "B";
    c1.effects = { Effect::fromJson(json{{"variable","x"},{"op","SET"},{"value", 1}}) };
    c2.effects = { Effect::fromJson(json{{"variable","x"},{"op","SET"},{"value", 2}}) };
    Capability comp = composeCapabilities(c1, c2);
    check(comp.effects.size() == 1, "Edge: conflicting effects on the same variable collapse to one entry");
    check(comp.effects[0].value.toNumeric() == 2.0, "Edge: the LATER effect (c2's) wins the override");
}
static void test_SingleCapabilityComposition() {
    // composeChain with a single capability should just return it unchanged.
    Capability c1; c1.id = "Solo";
    Capability result = composeChain({c1});
    check(result.id == "Solo", "Edge: composeChain([C1]) returns C1 unchanged");
}
static void test_EmptyCompositionThrows() {
    bool threw = false;
    try { composeChain({}); } catch (const std::exception&) { threw = true; }
    check(threw, "Edge: composeChain([]) throws rather than returning a garbage capability");
}
static void test_InvalidReliabilityHandledGracefully() {
    // reliability slightly above 1 (bad input data) should not produce NaN
    // in the log-domain operational encoding.
    Capability c; c.id = "Bad"; c.reliability = 1.5; // invalid, but must not crash
    EmbeddingEngine eng;
    auto ops = eng.encodeOperational(c);
    bool anyNaN = false;
    for (double v : ops) if (std::isnan(v)) anyNaN = true;
    check(!anyNaN, "Edge: an out-of-range reliability value does not produce NaN in the operational vector");
}

// --- Homomorphism proof (the core mathematical claim of composition.hpp) ---
static void test_EffectHomomorphismExact() {
    auto app = loadProblem("data/composition.json");
    EmbeddingEngine eng; eng.initialize(app);
    auto c1 = *app.find("CreateOrder");
    auto c2 = *app.find("MakePayment_API");
    HomomorphismCheck h = verifyHomomorphism(eng, c1, c2);
    check(h.effectsMatch, "Homomorphism: effect subspace of v(composite) exactly matches the masked-override closed form");
    check(h.operationalTimeMoneyMatch, "Homomorphism: time/money channels are exactly additive");
    check(h.operationalRiskReliabilityMatch, "Homomorphism: log-domain risk/reliability channels are exactly additive");
}

int main() {
    std::cout << "Running Assignment 2 validation tests...\n\n";
    test1_ValidCompatibility();
    test2_InvalidCompatibility();
    test3_CompositionCorrectness();
    test4_ReliabilityMultiplication();
    test5_CostAddition();
    test6_IOTypeMismatch();
    test7_UnavailableCapability();
    test8_GoalRelevanceBehavior();
    test_EmptyVectorCosine();
    test_ZeroNormVector();
    test_MissingStateVariable();
    test_UnspecifiedGoalVariableNotFalse();
    test_DuplicateCapabilityId();
    test_MalformedJson();
    test_MissingCapabilitiesField();
    test_ConflictingEffectsOverride();
    test_SingleCapabilityComposition();
    test_EmptyCompositionThrows();
    test_InvalidReliabilityHandledGracefully();
    test_EffectHomomorphismExact();

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
