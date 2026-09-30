#pragma once
// =============================================================================
// embedding.hpp -- phi_S, phi_G, phi_C and similarity() (assignment Section 6,
// Deliverable 1 & 2).
//
// DESIGN SUMMARY (full derivation in DELIVERABLE_1_FORMAL_EMBEDDING_DESIGN.md):
//
// 1. MASKED (mask,value) ENCODING for preconditions / effects / goals.
//    A precondition/effect/goal is a PARTIAL specification: most variables
//    are simply not mentioned ("don't care"), which must never be confused
//    with "required to be false/zero" (instructions Section 9). Each
//    variable therefore contributes TWO scalar channels, not one:
//        mask_i  in {0,1}   -- is variable i constrained at all?
//        val_i   in R       -- the required/produced value, if mask_i=1
//                              (0 otherwise, and never read when mask_i=0)
//    This is a deliberate refinement of the {+1,-1,0} bipolar scheme
//    suggested in the assignment materials: bipolar alone is unambiguous
//    for booleans but conflates "don't care" with "required value 0" for
//    numeric variables. The explicit mask channel removes that ambiguity
//    for every value kind, at the cost of doubling dimensionality per
//    variable -- an explicitly accepted, documented trade-off.
//
// 2. THREE SEPARATED SUBSPACES per capability (instructions Section 5 /
//    assignment Section 6.1 property 5): a capability vector is the
//    concatenation of three INDEPENDENTLY usable blocks:
//        v_func(C)  = [ v_pre | v_eff | v_input | v_output ]   FUNCTION
//        v_mech(C)  = [ v_type | v_mechanism_hash ]            MECHANISM
//        v_ops(C)   = [ time, money, resource, riskExposure,
//                       unreliabilityExposure, availability ]  OPERATIONAL
//    functionalSimilarity() uses v_func ONLY, so "are these the same
//    operation done differently" and "are these built the same way" are
//    answerable independently -- this is what lets MakePayment_API and
//    MakePayment_DB be judged functionally near-identical while remaining
//    distinguishable overall (Experiment 3).
//
// 3. LOG-DOMAIN OPERATIONAL CHANNELS. Sequential composition sums time and
//    money, and MULTIPLIES reliability and (1-risk). Storing reliability
//    and risk directly would make composition non-additive in vector
//    space. Storing riskExposure = -ln(1-risk) and unreliabilityExposure =
//    -ln(reliability) instead makes ALL FOUR of {time, money, riskExposure,
//    unreliabilityExposure} additive under composition (Section 14) --
//    proved and unit-tested in composition.hpp.
// =============================================================================
#include <cmath>
#include <numeric>
#include "types.hpp"

namespace capembed {

// A masked (mask,value) block over the registered variable dictionary.
struct MaskedVector {
    std::vector<double> mask; // 0/1 per variable
    std::vector<double> val;  // value per variable (only meaningful where mask=1)

    // Flattened [mask | val] representation used inside the full capability
    // vector; kept as two parallel arrays elsewhere because the formal
    // compatibility functions (compatibility.hpp) need mask and value
    // separately and re-deriving mask from "is val exactly 0" would
    // reintroduce exactly the ambiguity this design avoids.
    [[nodiscard]] std::vector<double> flatten() const {
        std::vector<double> out;
        out.reserve(mask.size() + val.size());
        out.insert(out.end(), mask.begin(), mask.end());
        out.insert(out.end(), val.begin(), val.end());
        return out;
    }
};

class EmbeddingEngine {
public:
    // --- Variable dictionary (state variables appearing anywhere) ---
    std::unordered_map<std::string, size_t> varIndex;
    std::vector<std::string> indexVar;

    // --- Type dictionary (capability T_i values seen) ---
    std::unordered_map<std::string, size_t> typeIndex;
    std::vector<std::string> indexType;

    // --- Port-name dictionary (union of all input/output port names) ---
    std::unordered_map<std::string, size_t> portIndex;
    std::vector<std::string> indexPort;

    // Normalization constants (documented, fixed -- not fitted to data, so
    // results stay comparable across datasets; see DELIVERABLE_1 Section 5).
    static constexpr double TIME_SCALE_MS = 1000.0;   // 1.0 == 1 second of latency
    static constexpr double MONEY_SCALE = 1.0;        // already in currency units
    static constexpr double MECH_HASH_DIM = 8.0;      // dimension of mechanism hash block

    size_t registerVariable(const std::string& v) {
        auto it = varIndex.find(v);
        if (it != varIndex.end()) return it->second;
        size_t idx = indexVar.size();
        varIndex[v] = idx;
        indexVar.push_back(v);
        return idx;
    }
    size_t registerType(const std::string& t) {
        auto it = typeIndex.find(t);
        if (it != typeIndex.end()) return it->second;
        size_t idx = indexType.size();
        typeIndex[t] = idx;
        indexType.push_back(t);
        return idx;
    }
    size_t registerPort(const std::string& p) {
        auto it = portIndex.find(p);
        if (it != portIndex.end()) return it->second;
        size_t idx = indexPort.size();
        portIndex[p] = idx;
        indexPort.push_back(p);
        return idx;
    }

    [[nodiscard]] size_t stateDim() const { return indexVar.size(); }
    [[nodiscard]] size_t typeDim() const { return indexType.size(); }
    [[nodiscard]] size_t portDim() const { return indexPort.size(); }

    // Registers every variable/type/port that appears anywhere in the
    // problem so all vectors produced afterward share one fixed dictionary
    // (a precondition of comparing vectors at all).
    void initialize(const ApplicationProblem& app) {
        for (const auto& [k, v] : app.initialState.vars) { (void)v; registerVariable(k); }
        for (const auto& c : app.goal.conditions) registerVariable(c.variable);
        for (const auto& cap : app.capabilities) {
            registerType(cap.type);
            for (const auto& p : cap.preconditions) registerVariable(p.variable);
            for (const auto& e : cap.effects) registerVariable(e.variable);
            for (const auto& k : cap.constraints) registerVariable(k.variable);
            for (const auto& p : cap.inputs) registerPort(p.name);
            for (const auto& o : cap.outputs) registerPort(o.name);
        }
    }

    // ---------------------------------------------------------------
    // phi_S(S) -> R^{d_s}: raw state encoding (full values, no masking --
    // a concrete state specifies EVERY variable it tracks, so there is no
    // "don't care" ambiguity to resolve here; masking is only needed for
    // the inherently PARTIAL specifications: preconditions/effects/goals).
    // ---------------------------------------------------------------
    [[nodiscard]] std::vector<double> encodeState(const State& s) const {
        std::vector<double> v(stateDim(), 0.0);
        for (size_t i = 0; i < stateDim(); ++i) v[i] = s.getOr(indexVar[i], Value(0.0)).toNumeric();
        return v;
    }

    // ---------------------------------------------------------------
    // phi_G(G) -> R^{2 d_s}: masked encoding, since a goal is a PARTIAL
    // state specification exactly like a precondition set.
    // ---------------------------------------------------------------
    [[nodiscard]] MaskedVector encodeConditionsMasked(const std::vector<Condition>& conds) const {
        MaskedVector mv;
        mv.mask.assign(stateDim(), 0.0);
        mv.val.assign(stateDim(), 0.0);
        for (const auto& c : conds) {
            auto it = varIndex.find(c.variable);
            if (it == varIndex.end()) continue;
            mv.mask[it->second] = 1.0;
            mv.val[it->second] = c.expected.toNumeric();
        }
        return mv;
    }
    [[nodiscard]] MaskedVector encodeGoalMasked(const Goal& g) const { return encodeConditionsMasked(g.conditions); }
    [[nodiscard]] std::vector<double> encodeGoal(const Goal& g) const { return encodeGoalMasked(g).flatten(); }

    // ---------------------------------------------------------------
    // Preconditions P_i and Effects E_i: same masked scheme.
    // ---------------------------------------------------------------
    [[nodiscard]] MaskedVector encodePreconditionsMasked(const Capability& c) const { return encodeConditionsMasked(c.preconditions); }
    [[nodiscard]] MaskedVector encodeConstraintsMasked(const Capability& c) const { return encodeConditionsMasked(c.constraints); }

    [[nodiscard]] MaskedVector encodeEffectsMasked(const Capability& c) const {
        MaskedVector mv;
        mv.mask.assign(stateDim(), 0.0);
        mv.val.assign(stateDim(), 0.0);
        for (const auto& e : c.effects) {
            auto it = varIndex.find(e.variable);
            if (it == varIndex.end()) continue;
            mv.mask[it->second] = 1.0;
            mv.val[it->second] = e.resultingValue().toNumeric();
        }
        return mv;
    }

    // ---------------------------------------------------------------
    // Input/Output port subspace: multi-hot over the port-name dictionary.
    //   input dim value  = 1.0 required, 0.5 optional, 0.0 absent
    //   output dim value = 1.0 present,  0.0 absent
    // Exact name+type matching (the formal ground truth) is done
    // separately in compatibility.hpp; this vector only needs to
    // *correlate* with that ground truth for cosine similarity to be
    // meaningful (verified empirically in Experiment 1/3).
    // ---------------------------------------------------------------
    [[nodiscard]] std::vector<double> encodeInputs(const Capability& c) const {
        std::vector<double> v(portDim(), 0.0);
        for (const auto& p : c.inputs) {
            auto it = portIndex.find(p.name);
            if (it != portIndex.end()) v[it->second] = p.required ? 1.0 : 0.5;
        }
        return v;
    }
    [[nodiscard]] std::vector<double> encodeOutputs(const Capability& c) const {
        std::vector<double> v(portDim(), 0.0);
        for (const auto& p : c.outputs) {
            auto it = portIndex.find(p.name);
            if (it != portIndex.end()) v[it->second] = 1.0;
        }
        return v;
    }

    // ---------------------------------------------------------------
    // Mechanism subspace: one-hot(type) concatenated with a small,
    // deterministic hash-projection of the mechanism string. The hash
    // projection is NOT meant to encode mechanism *semantics* (that would
    // need learned embeddings, out of scope) -- only to make different
    // mechanism strings land at different points so implementation
    // vectors of otherwise-identical capabilities are distinguishable
    // (Experiment 3), while identical mechanism strings coincide exactly.
    // ---------------------------------------------------------------
    [[nodiscard]] std::vector<double> encodeMechanism(const Capability& c) const {
        std::vector<double> typeVec(typeDim(), 0.0);
        auto it = typeIndex.find(c.type);
        if (it != typeIndex.end()) typeVec[it->second] = 1.0;

        std::vector<double> hashVec(static_cast<size_t>(MECH_HASH_DIM), 0.0);
        std::hash<std::string> hasher;
        size_t h = hasher(c.mechanism);
        for (size_t k = 0; k < hashVec.size(); ++k) {
            // Simple multiplicative re-hash per dimension -> deterministic,
            // reproducible, bounded in [-1,1]. Documented as a coarse
            // "implementation fingerprint," not a semantic embedding.
            size_t hk = h ^ (0x9E3779B97F4A7C15ULL * (k + 1));
            hashVec[k] = (static_cast<double>(hk % 2000) / 1000.0) - 1.0;
        }
        std::vector<double> out = typeVec;
        out.insert(out.end(), hashVec.begin(), hashVec.end());
        return out;
    }

    // ---------------------------------------------------------------
    // Operational subspace Q_i, Rel_i, A_i -- log-domain for additive
    // composition (see file header point 3).
    // ---------------------------------------------------------------
    [[nodiscard]] std::vector<double> encodeOperational(const Capability& c) const {
        double riskExposure = -std::log(std::max(1e-6, 1.0 - std::min(c.qos.risk, 1.0 - 1e-6)));
        double unrelExposure = -std::log(std::max(1e-6, c.reliability));
        return {
            c.qos.timeMs / TIME_SCALE_MS,
            c.qos.money / MONEY_SCALE,
            c.qos.resource,          // NOT additive under composition (peak, not cumulative) -- see composition.hpp
            riskExposure,
            unrelExposure,
            c.availability ? 1.0 : 0.0,
        };
    }

    // ---------------------------------------------------------------
    // Full capability vector: v(C) = [ v_pre | v_eff | v_in | v_out |
    //                                  v_type | v_mech_hash | v_ops ]
    // Subspace boundaries are returned alongside so callers can slice
    // without recomputing (used by similarity() and the CLI's --vectors
    // inspection view).
    // ---------------------------------------------------------------
    struct CapabilityVector {
        std::vector<double> functional; // v_pre | v_eff | v_in | v_out
        std::vector<double> mechanism;  // v_type | v_mech_hash
        std::vector<double> operational;// v_ops
        MaskedVector pre, eff;          // kept unflattened for compatibility.hpp
        std::vector<double> in, out;

        [[nodiscard]] std::vector<double> full() const {
            std::vector<double> v = functional;
            v.insert(v.end(), mechanism.begin(), mechanism.end());
            v.insert(v.end(), operational.begin(), operational.end());
            return v;
        }
    };

    [[nodiscard]] CapabilityVector encodeCapability(const Capability& c) const {
        CapabilityVector cv;
        cv.pre = encodePreconditionsMasked(c);
        cv.eff = encodeEffectsMasked(c);
        cv.in = encodeInputs(c);
        cv.out = encodeOutputs(c);
        cv.functional = cv.pre.flatten();
        auto effFlat = cv.eff.flatten();
        cv.functional.insert(cv.functional.end(), effFlat.begin(), effFlat.end());
        cv.functional.insert(cv.functional.end(), cv.in.begin(), cv.in.end());
        cv.functional.insert(cv.functional.end(), cv.out.begin(), cv.out.end());
        cv.mechanism = encodeMechanism(c);
        cv.operational = encodeOperational(c);
        return cv;
    }

    // Convenience overloads matching the assignment's required API names.
    [[nodiscard]] std::vector<double> encode(const State& s) const { return encodeState(s); }
    [[nodiscard]] std::vector<double> encode(const Goal& g) const { return encodeGoal(g); }
    [[nodiscard]] std::vector<double> encode(const Capability& c) const { return encodeCapability(c).full(); }

    // ---------------------------------------------------------------
    // similarity(x,y): cosine similarity. Provided at three granularities;
    // instructions Section 5/6 explicitly require function vs.
    // implementation to be separately answerable.
    // ---------------------------------------------------------------
    static double cosine(const std::vector<double>& a, const std::vector<double>& b) {
        if (a.size() != b.size() || a.empty()) return 0.0;
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (size_t i = 0; i < a.size(); ++i) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
        if (na < 1e-12 || nb < 1e-12) return 0.0;
        return dot / (std::sqrt(na) * std::sqrt(nb));
    }
    static double similarity(const std::vector<double>& a, const std::vector<double>& b) { return cosine(a, b); }

    [[nodiscard]] double functionalSimilarity(const Capability& c1, const Capability& c2) const {
        return cosine(encodeCapability(c1).functional, encodeCapability(c2).functional);
    }
    [[nodiscard]] double implementationSimilarity(const Capability& c1, const Capability& c2) const {
        return cosine(encodeCapability(c1).mechanism, encodeCapability(c2).mechanism);
    }
    // Weighted overall similarity: functional subspace dominates, per
    // instructions Section 5 ("functional information must dominate
    // implementation details"). Weights are a documented design choice,
    // not fitted; Experiment 3 reports functional/implementation/overall
    // separately so the reader can see exactly what the weighting changes.
    static constexpr double W_FUNC = 1.0, W_MECH = 0.35, W_OPS = 0.15;
    [[nodiscard]] double overallSimilarity(const Capability& c1, const Capability& c2) const {
        auto v1 = encodeCapability(c1), v2 = encodeCapability(c2);
        auto weighted = [&](const std::vector<double>& v, double w) {
            std::vector<double> out(v.size());
            for (size_t i = 0; i < v.size(); ++i) out[i] = v[i] * w;
            return out;
        };
        std::vector<double> a = weighted(v1.functional, W_FUNC);
        auto am = weighted(v1.mechanism, W_MECH); a.insert(a.end(), am.begin(), am.end());
        auto ao = weighted(v1.operational, W_OPS); a.insert(a.end(), ao.begin(), ao.end());
        std::vector<double> b = weighted(v2.functional, W_FUNC);
        auto bm = weighted(v2.mechanism, W_MECH); b.insert(b.end(), bm.begin(), bm.end());
        auto bo = weighted(v2.operational, W_OPS); b.insert(b.end(), bo.begin(), bo.end());
        return cosine(a, b);
    }
};

} // namespace capembed
