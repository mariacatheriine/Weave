#pragma once
// =============================================================================
// compatibility.hpp -- exact, formal compatibility functions, operating on
// the RAW Capability structures (not on embedding vectors). Assignment
// Section 6.1 property 5 draws a hard line between SIMILARITY (a vector
// notion, approximate/functional-resemblance) and COMPOSABILITY (a formal,
// exact, logical notion: can C1 causally precede C2). These are
// deliberately kept as two independent code paths so that a bug or an
// approximation in the embedding can never silently change what counts as
// formally composable -- composability is decided here, on the source
// data, not on cosine similarity of any vector.
// =============================================================================
#include "types.hpp"

namespace capembed {

// Result of comparing C1's effects against C2's preconditions (or, when
// reused for goal relevance, against a Goal's conditions -- see
// goalRelevance() below, which calls this same primitive). This ONE
// primitive is reused in three places in this codebase: precondition-
// effect compatibility, goal relevance, and (via composition.hpp)
// precondition-reduction during composition -- deliberately, so a viva
// question about "how do you check condition satisfaction" has one
// answer, not three ad hoc ones.
struct ConditionSatisfaction {
    int satisfied = 0;  // effect directly produces exactly the required value
    int violated = 0;   // effect directly produces a CONFLICTING value
    int neutral = 0;    // condition's variable is untouched by the effects
    int total = 0;

    [[nodiscard]] bool hasConflict() const { return violated > 0; }
    [[nodiscard]] double score() const {
        // in [-1, 1]; >0 net-favorable, <0 net-conflicting, 0 neutral/empty.
        return total == 0 ? 1.0 : static_cast<double>(satisfied - violated) / static_cast<double>(total);
    }
};

// Checks a set of conditions (preconditions, a goal, ...) against a set of
// effects. This is the exact ground truth Ei => Pj / Ei => Gj check
// (assignment Section 6.1 property 3, and the worked example in Section 7
// of the PDF: CreateOrder's effect OrderExists=true satisfies MakePayment's
// precondition OrderExists=true, while CancelCart's precondition
// OrderExists=false is directly VIOLATED by that same effect).
inline ConditionSatisfaction checkConditionsAgainstEffects(
    const std::vector<Condition>& conditions, const std::vector<Effect>& effects) {
    ConditionSatisfaction r;
    r.total = static_cast<int>(conditions.size());
    for (const auto& cond : conditions) {
        bool found = false;
        for (const auto& eff : effects) {
            if (eff.variable != cond.variable) continue;
            found = true;
            if (cond.op == "==" && eff.resultingValue() == cond.expected) r.satisfied++;
            else if (cond.op == "!=" && eff.resultingValue() != cond.expected) r.satisfied++;
            else if (cond.op == "==" ) r.violated++;        // effect sets a DIFFERENT value than required
            else if (cond.op == "!=" && eff.resultingValue() == cond.expected) r.violated++;
            else r.neutral++; // inequality ops (>,<, etc.) on a touched var: treated as neutral, not asserted
            break;
        }
        if (!found) r.neutral++;
    }
    return r;
}

// -----------------------------------------------------------------------
// precondition_effect_compatibility(c1, c2): does c1's effect set satisfy,
// conflict with, or leave untouched each of c2's preconditions?
//   isComposable  == true  iff there is NO direct conflict (a hard rule:
//                    a single genuine conflict blocks composability,
//                    however many other preconditions happen to be
//                    satisfied -- unlike a naive averaged score, which
//                    could let one conflict be "outvoted").
//   degree        in [-1,1], the soft strength of the relationship,
//                    useful for RANKING several composable candidates
//                    against each other, never for overriding isComposable.
// -----------------------------------------------------------------------
struct CompatibilityResult {
    bool isComposable = true;
    double degree = 0.0;
    int satisfiedCount = 0, violatedCount = 0, neutralCount = 0, totalPreconditions = 0;
};

inline CompatibilityResult preconditionEffectCompatibility(const Capability& c1, const Capability& c2) {
    ConditionSatisfaction cs = checkConditionsAgainstEffects(c2.preconditions, c1.effects);
    CompatibilityResult r;
    r.isComposable = !cs.hasConflict();
    r.degree = cs.score();
    r.satisfiedCount = cs.satisfied;
    r.violatedCount = cs.violated;
    r.neutralCount = cs.neutral;
    r.totalPreconditions = cs.total;
    return r;
}

// -----------------------------------------------------------------------
// input_output_compatibility(c1, c2): fraction of c2's REQUIRED inputs
// that are exactly matched (by name AND type) by one of c1's outputs.
// Optional inputs are not required to be matched by c1, but a match still
// contributes partial credit, since it demonstrates a real usable link.
// This does NOT gate composability on its own (see file header): many
// composable pairs have no I/O link at all, e.g. CreateOrder->MakePayment
// in the assignment's own worked example composes purely through
// precondition/effect, with no shared port). It is reported as an
// independent, additional signal (assignment Section 6.1 property 4).
// -----------------------------------------------------------------------
struct IOCompatibilityResult {
    double coverage = 1.0;      // fraction of REQUIRED inputs satisfied, in [0,1]; 1.0 if c2 has no required inputs
    int matchedRequired = 0, totalRequired = 0;
    int matchedOptional = 0, totalOptional = 0;
    std::vector<std::string> matchedPortNames;
};

inline IOCompatibilityResult inputOutputCompatibility(const Capability& c1, const Capability& c2) {
    IOCompatibilityResult r;
    for (const auto& in : c2.inputs) {
        bool matched = false;
        for (const auto& out : c1.outputs) {
            if (in.name == out.name && in.type == out.type) { matched = true; break; }
        }
        if (in.required) {
            r.totalRequired++;
            if (matched) { r.matchedRequired++; r.matchedPortNames.push_back(in.name); }
        } else {
            r.totalOptional++;
            if (matched) { r.matchedOptional++; r.matchedPortNames.push_back(in.name); }
        }
    }
    r.coverage = r.totalRequired == 0 ? 1.0 : static_cast<double>(r.matchedRequired) / static_cast<double>(r.totalRequired);
    return r;
}

// -----------------------------------------------------------------------
// compatibility(c1, c2): the combined, top-level verdict used by the
// experiments to print one clear line. Deliberately keeps the two
// underlying signals visible rather than collapsing everything into one
// number that would hide WHY two capabilities are or are not compatible.
// -----------------------------------------------------------------------
struct FullCompatibility {
    CompatibilityResult preEff;
    IOCompatibilityResult io;
    [[nodiscard]] bool isComposable() const { return preEff.isComposable; }
};
inline FullCompatibility compatibility(const Capability& c1, const Capability& c2) {
    FullCompatibility f;
    f.preEff = preconditionEffectCompatibility(c1, c2);
    f.io = inputOutputCompatibility(c1, c2);
    return f;
}

// -----------------------------------------------------------------------
// Goal relevance: reuses the SAME checkConditionsAgainstEffects primitive,
// treating the goal's conditions exactly like a downstream capability's
// preconditions (formally identical: both are Condition sets a state must
// satisfy). Assignment Section 6.1 property 7.
// -----------------------------------------------------------------------
struct GoalRelevance {
    double score = 0.0;      // in [-1,1]; see ConditionSatisfaction::score()
    int satisfiedCount = 0, violatedCount = 0, neutralCount = 0, totalGoalConditions = 0;
    [[nodiscard]] bool contributesToGoal() const { return satisfiedCount > 0; }
    [[nodiscard]] bool conflictsWithGoal() const { return violatedCount > 0; }
};
inline GoalRelevance goalRelevance(const Capability& c, const Goal& g) {
    ConditionSatisfaction cs = checkConditionsAgainstEffects(g.conditions, c.effects);
    GoalRelevance r;
    r.score = cs.score();
    r.satisfiedCount = cs.satisfied;
    r.violatedCount = cs.violated;
    r.neutralCount = cs.neutral;
    r.totalGoalConditions = cs.total;
    return r;
}

} // namespace capembed
