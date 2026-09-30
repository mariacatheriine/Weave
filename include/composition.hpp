#pragma once
// =============================================================================
// composition.hpp -- C12 = C2 o C1 (assignment Section 5 / instructions
// Section 13-14).
//
// Two distinct things are computed here, and the file is organized to keep
// them visibly separate:
//
//   (A) composeCapabilities(c1,c2): builds the actual COMPOSITE Capability
//       object (new preconditions/effects/ports/QoS), which is what would
//       actually be registered and reasoned about as a new capability.
//
//   (B) composeVectorFromParts(...): builds the composite's EMBEDDING
//       VECTOR directly from v(c1) and v(c2), via explicit closed-form
//       update rules -- NOT by re-encoding the composite Capability from
//       scratch, and NOT by naive concatenation (instructions Section 14
//       explicitly forbids both "call it composition" without a real rule,
//       and warns against pure concatenation).
//
//   verifyHomomorphism() cross-checks (A) and (B): it re-encodes the
//   composite Capability built in (A) with the ordinary embedding engine,
//   and asserts it EQUALS (within floating-point tolerance) the vector
//   built by the closed-form update rule in (B), for every subspace where
//   a closed form is claimed. This is the actual, testable answer to "how
//   does v(C2 o C1) relate to v(C1), v(C2)" -- not an assertion, a checked
//   equality (see tests/test_cases.cpp and Experiment 2).
// =============================================================================
#include "compatibility.hpp"
#include "embedding.hpp"
#include "types.hpp"

namespace capembed {

// -----------------------------------------------------------------------
// (A) Formal composition of the Capability structures themselves.
// -----------------------------------------------------------------------
inline Capability composeCapabilities(const Capability& c1, const Capability& c2) {
    Capability comp;
    comp.id = c1.id + "_THEN_" + c2.id;
    comp.name = "(" + c2.name + " o " + c1.name + ")";
    comp.type = "COMPOSITE";
    comp.description = "Composite capability: execute " + c1.id + ", then " + c2.id + ".";
    comp.mechanism = "ORCHESTRATION(" + c1.id + " -> " + c2.id + ")";

    // --- Preconditions: c1's preconditions, PLUS only the preconditions
    // of c2 that c1's effects do NOT already guarantee (PDF Section 13's
    // worked example: A->B->C should require only A externally, not A
    // and B, since B is produced internally by the first step). A
    // precondition of c2 that c1's effects directly CONFLICT with is kept
    // (composition is still constructed; whether it is a *valid*
    // composition is a separate question answered by isComposable() /
    // preconditionEffectCompatibility(), checked by the caller before
    // composing -- see experiments.hpp).
    comp.preconditions = c1.preconditions;
    for (const auto& p2 : c2.preconditions) {
        bool guaranteedByC1 = false;
        for (const auto& e1 : c1.effects) {
            if (e1.variable == p2.variable && p2.op == "==" && e1.resultingValue() == p2.expected) {
                guaranteedByC1 = true;
                break;
            }
        }
        if (!guaranteedByC1) comp.preconditions.push_back(p2);
    }

    // --- Effects: c1's effects, then c2's effects OVERRIDE any effect on
    // the same variable (later effect wins -- the state truly was changed
    // again by c2). This "masked override" rule is exactly what
    // composeVectorFromParts() reproduces in vector form below.
    comp.effects = c1.effects;
    for (const auto& e2 : c2.effects) {
        bool overwritten = false;
        for (auto& e : comp.effects) {
            if (e.variable == e2.variable) { e = e2; overwritten = true; break; }
        }
        if (!overwritten) comp.effects.push_back(e2);
    }

    // --- Constraints: union (conservative -- both remain active).
    comp.constraints = c1.constraints;
    comp.constraints.insert(comp.constraints.end(), c2.constraints.begin(), c2.constraints.end());

    // --- Inputs: c1's own inputs, PLUS whichever of c2's inputs are NOT
    // satisfied by c1's outputs (the ones that genuinely still need to
    // come from outside the composite). This FIXES a real bug found in
    // the professor's Example 3 reference (its compose() silently used
    // `comp.inputs = c1.inputs`, dropping c2's unsatisfied inputs
    // entirely -- meaning a composite could claim to need fewer inputs
    // than it actually does).
    comp.inputs = c1.inputs;
    IOCompatibilityResult io = inputOutputCompatibility(c1, c2);
    for (const auto& in2 : c2.inputs) {
        bool suppliedByC1 = false;
        for (const auto& name : io.matchedPortNames) if (name == in2.name) { suppliedByC1 = true; break; }
        if (!suppliedByC1) comp.inputs.push_back(in2);
    }
    // --- Outputs: union of both -- an output produced partway through the
    // chain is still a real output of the composite (a downstream
    // capability might depend on c1's output even though c2 runs after).
    comp.outputs = c1.outputs;
    for (const auto& o2 : c2.outputs) comp.outputs.push_back(o2);

    // --- Resources: union, de-duplicated.
    comp.resources = c1.resources;
    for (const auto& r : c2.resources)
        if (std::find(comp.resources.begin(), comp.resources.end(), r) == comp.resources.end())
            comp.resources.push_back(r);

    // --- QoS aggregation (assignment Section 13):
    //   time(composite)  = time(c1) + time(c2)              [additive: sequential wait]
    //   money(composite) = money(c1) + money(c2)             [additive: sequential spend]
    //   resource(composite) = max(resource(c1), resource(c2)) [NOT additive: peak concurrent
    //                          load, since c1 and c2 do not hold resources
    //                          simultaneously in a purely sequential composition]
    //   risk(composite)   = 1 - (1-risk(c1))(1-risk(c2))     [probability that AT LEAST
    //                          ONE step fails, assuming independent risk events]
    comp.qos.timeMs = c1.qos.timeMs + c2.qos.timeMs;
    comp.qos.money = c1.qos.money + c2.qos.money;
    comp.qos.resource = std::max(c1.qos.resource, c2.qos.resource);
    comp.qos.risk = 1.0 - (1.0 - c1.qos.risk) * (1.0 - c2.qos.risk);

    // --- Reliability: product of independent sequential success probabilities.
    comp.reliability = c1.reliability * c2.reliability;
    // --- Availability: composite is available iff BOTH constituents are
    // (logical AND on the static {0,1} flag -- see types.hpp Section 4.6 note).
    comp.availability = c1.availability && c2.availability;

    return comp;
}

// -----------------------------------------------------------------------
// (B) Closed-form composite VECTOR, built directly from v(c1), v(c2),
// WITHOUT re-encoding the composite Capability.
// -----------------------------------------------------------------------
struct CompositeVectorParts {
    MaskedVector pre;              // NOT a closed form of v_pre(c1),v_pre(c2) alone -- see note below
    MaskedVector eff;               // CLOSED FORM: masked-override (proved in verifyHomomorphism)
    std::vector<double> in, out;    // set-union in vector form (element-wise max, since ports are multi-hot)
    std::vector<double> operational;// CLOSED FORM: additive in 4 of 6 channels (see below)
};

// eff(C2 o C1): masked-override rule.
//   mask[i] = mask1[i] OR mask2[i]
//   val[i]  = val2[i] if mask2[i]==1 else val1[i]
// This is an EXACT closed form: it needs no information beyond v_eff(c1)
// and v_eff(c2) themselves (not the raw Capability structs), because SET
// effects are literally "the composite's final value at i is whatever the
// LAST capability that touches i set it to" -- which is precisely what
// mask-driven override encodes.
inline MaskedVector composeEffectsVector(const MaskedVector& e1, const MaskedVector& e2) {
    MaskedVector out;
    out.mask.resize(e1.mask.size());
    out.val.resize(e1.mask.size());
    for (size_t i = 0; i < e1.mask.size(); ++i) {
        bool m2 = e2.mask[i] > 0.5;
        out.mask[i] = (e1.mask[i] > 0.5 || m2) ? 1.0 : 0.0;
        out.val[i] = m2 ? e2.val[i] : e1.val[i];
    }
    return out;
}

// pre(C2 o C1): this one is explicitly NOT a closed form of v_pre(c1) and
// v_pre(c2) alone -- deciding whether a precondition of c2 "survives" into
// the composite requires checking it against c1's EFFECTS (raw structure,
// or equivalently eff(c1)'s masked vector together with which entries
// match). We compute it from the masked vectors of pre(c1), pre(c2), and
// eff(c1) together (still no need to touch inputs/outputs/QoS), which is
// a *partial* closed form: a genuine, documented example of a subspace
// where composition is NOT simply linear (reported honestly in Experiment
// 2 / the technical report, per instructions Section 25: "if a result does
// not strongly demonstrate the desired property, explain the limitation").
inline MaskedVector composePreconditionsVector(const MaskedVector& p1, const MaskedVector& p2, const MaskedVector& e1) {
    MaskedVector out = p1; // start from c1's preconditions
    for (size_t i = 0; i < p2.mask.size(); ++i) {
        if (p2.mask[i] < 0.5) continue; // c2 has no precondition on i
        bool guaranteedByC1 = (e1.mask[i] > 0.5) && (std::abs(e1.val[i] - p2.val[i]) < 1e-6);
        if (guaranteedByC1) continue; // dropped, exactly like composeCapabilities()
        // union in: if c1 already had a (different) precondition on i, c2's
        // is layered on top (mirrors composeCapabilities appending both).
        out.mask[i] = 1.0;
        out.val[i] = p2.val[i];
    }
    return out;
}

// operational(C2 o C1): channel-by-channel.
//   [0] time     -> ADDITIVE   (op1[0] + op2[0])
//   [1] money    -> ADDITIVE   (op1[1] + op2[1])
//   [2] resource -> max(op1[2], op2[2])   NOT additive (documented, see composeCapabilities)
//   [3] riskExposure -> ADDITIVE (log-domain risk, see embedding.hpp)
//   [4] unreliabilityExposure -> ADDITIVE (log-domain reliability)
//   [5] availability -> min(op1[5], op2[5])  (logical AND, NOT additive)
inline std::vector<double> composeOperationalVector(const std::vector<double>& o1, const std::vector<double>& o2) {
    return {
        o1[0] + o2[0],
        o1[1] + o2[1],
        std::max(o1[2], o2[2]),
        o1[3] + o2[3],
        o1[4] + o2[4],
        std::min(o1[5], o2[5]),
    };
}

// Full closed-form composite vector assembly.
inline CompositeVectorParts composeVectorFromParts(const EmbeddingEngine::CapabilityVector& v1,
                                                    const EmbeddingEngine::CapabilityVector& v2) {
    CompositeVectorParts out;
    out.eff = composeEffectsVector(v1.eff, v2.eff);
    out.pre = composePreconditionsVector(v1.pre, v2.pre, v1.eff);
    out.in.resize(v1.in.size());
    out.out.resize(v1.out.size());
    for (size_t i = 0; i < v1.in.size(); ++i) out.in[i] = std::max(v1.in[i], v2.in[i]);
    for (size_t i = 0; i < v1.out.size(); ++i) out.out[i] = std::max(v1.out[i], v2.out[i]);
    out.operational = composeOperationalVector(v1.operational, v2.operational);
    return out;
}

// -----------------------------------------------------------------------
// Cross-check: does the closed-form vector (B) match re-encoding the
// actual composite Capability (A)? This IS the experiment, not a demo --
// see experiments.hpp Experiment 2 and tests/test_cases.cpp.
// -----------------------------------------------------------------------
struct HomomorphismCheck {
    bool effectsMatch = false;
    bool operationalTimeMoneyMatch = false;
    bool operationalRiskReliabilityMatch = false;
    double maxEffectsError = 0.0;
    double maxOperationalError = 0.0;
};

inline HomomorphismCheck verifyHomomorphism(const EmbeddingEngine& eng, const Capability& c1, const Capability& c2) {
    Capability composite = composeCapabilities(c1, c2);
    auto v1 = eng.encodeCapability(c1);
    auto v2 = eng.encodeCapability(c2);
    auto vComposite = eng.encodeCapability(composite); // ground truth: re-encoded from scratch
    auto parts = composeVectorFromParts(v1, v2);        // closed form

    HomomorphismCheck check;
    double maxEffErr = 0.0;
    for (size_t i = 0; i < vComposite.eff.mask.size(); ++i) {
        maxEffErr = std::max(maxEffErr, std::abs(vComposite.eff.mask[i] - parts.eff.mask[i]));
        if (vComposite.eff.mask[i] > 0.5 && parts.eff.mask[i] > 0.5)
            maxEffErr = std::max(maxEffErr, std::abs(vComposite.eff.val[i] - parts.eff.val[i]));
    }
    check.maxEffectsError = maxEffErr;
    check.effectsMatch = maxEffErr < 1e-6;

    double maxOpErr = 0.0;
    for (size_t i = 0; i < 6; ++i) maxOpErr = std::max(maxOpErr, std::abs(vComposite.operational[i] - parts.operational[i]));
    check.maxOperationalError = maxOpErr;
    check.operationalTimeMoneyMatch = std::abs(vComposite.operational[0] - parts.operational[0]) < 1e-6 &&
                                       std::abs(vComposite.operational[1] - parts.operational[1]) < 1e-6;
    check.operationalRiskReliabilityMatch = std::abs(vComposite.operational[3] - parts.operational[3]) < 1e-6 &&
                                             std::abs(vComposite.operational[4] - parts.operational[4]) < 1e-6;
    return check;
}

// Recursive composition: C123 = C3 o C2 o C1, etc. Folds left to right.
inline Capability composeChain(const std::vector<Capability>& chain) {
    if (chain.empty()) throw std::invalid_argument("composeChain: empty capability list");
    Capability acc = chain.front();
    for (size_t i = 1; i < chain.size(); ++i) acc = composeCapabilities(acc, chain[i]);
    return acc;
}

} // namespace capembed
