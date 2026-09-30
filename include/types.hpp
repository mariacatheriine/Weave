#pragma once
// =============================================================================
// types.hpp -- Formal data model for the Assignment 2 capability-embedding
// system: A = (S, C, S_I, G, R, K)  (assignment PDF, Section 3)
//
// IMPORTANT DISTINCTION (assignment Section 2 / instructions Section 2):
//   STATES ARE NOT CAPABILITIES.
//   A State describes a condition of the application. A Capability is an
//   operation/transition S --C--> S' that can transform one state into
//   another. This file keeps that distinction structural, not just
//   conceptual: Capability never inherits from or is stored as a State.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "third_party/nlohmann/json.hpp"

namespace capembed {

using json = nlohmann::json;

// -----------------------------------------------------------------------
// Value: a single state-variable value. Supports the four scalar kinds the
// assignment PDF (Section 3.1) explicitly names: boolean, integer,
// real-valued, and categorical (string/enumerated). "Structured" values are
// intentionally out of scope (Section 47: do not overengineer).
// -----------------------------------------------------------------------
struct Value {
    enum class Kind { Null, Bool, Int, Real, Text };
    Kind kind = Kind::Null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    std::string s;

    Value() = default;
    Value(bool v) : kind(Kind::Bool), b(v) {}
    Value(int v) : kind(Kind::Int), i(v) {}
    Value(int64_t v) : kind(Kind::Int), i(v) {}
    Value(double v) : kind(Kind::Real), d(v) {}
    Value(const std::string& v) : kind(Kind::Text), s(v) {}
    Value(const char* v) : kind(Kind::Text), s(v) {}

    [[nodiscard]] bool isNull() const { return kind == Kind::Null; }

    // A single canonical ordinal table for known categorical/enumerated
    // domain values that recur in the datasets (order/payment status
    // lifecycles). This is a DOCUMENTED, deliberate engineering choice
    // (assignment instructions Section 47: "explain every formula"), not a
    // hidden magic table -- see DELIVERABLE_1 Section 3.2 for the full list.
    // Any string not in the table falls back to a stable hash-based
    // pseudo-ordinal in [0,1), which keeps distinct unknown strings
    // distinguishable without asserting a false ordinal relationship
    // between them (documented limitation: hash-based values carry no
    // semantic ordering, only identity).
    [[nodiscard]] double toNumeric() const {
        switch (kind) {
            case Kind::Bool: return b ? 1.0 : 0.0;
            case Kind::Int:  return static_cast<double>(i);
            case Kind::Real: return d;
            case Kind::Text: {
                static const std::unordered_map<std::string, double> canonical = {
                    {"NOT_STARTED", 0.0}, {"PENDING", 0.5}, {"CREATED", 1.0},
                    {"VALIDATED", 2.0}, {"PROCESSING", 2.5}, {"PAID", 3.0},
                    {"SUCCESS", 1.0}, {"FULFILLED", 4.0}, {"DISPATCHED", 4.5},
                    {"DELIVERED", 5.0}, {"FAILED", -1.0}, {"CANCELLED", -2.0},
                    {"REFUNDED", -1.5}, {"true", 1.0}, {"false", 0.0},
                    {"TRUE", 1.0}, {"FALSE", 0.0},
                };
                auto it = canonical.find(s);
                if (it != canonical.end()) return it->second;
                try { return std::stod(s); } catch (...) {}
                // Stable hash fallback -> [0,1); documented as identity-only.
                std::hash<std::string> hasher;
                return static_cast<double>(hasher(s) % 100000) / 100000.0;
            }
            default: return 0.0;
        }
    }

    [[nodiscard]] std::string asString() const {
        switch (kind) {
            case Kind::Bool: return b ? "true" : "false";
            case Kind::Int:  return std::to_string(i);
            case Kind::Real: {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(3) << d;
                return ss.str();
            }
            case Kind::Text: return s;
            default: return "null";
        }
    }

    bool operator==(const Value& o) const {
        if (kind == Kind::Bool && o.kind == Kind::Bool) return b == o.b;
        if (kind == Kind::Text && o.kind == Kind::Text) return s == o.s;
        return std::abs(toNumeric() - o.toNumeric()) < 1e-6;
    }
    bool operator!=(const Value& o) const { return !(*this == o); }

    static Value fromJson(const json& v) {
        if (v.is_boolean()) return Value(v.get<bool>());
        if (v.is_number_integer()) return Value(v.get<int64_t>());
        if (v.is_number_float()) return Value(v.get<double>());
        if (v.is_string()) return Value(v.get<std::string>());
        return Value();
    }
};

// -----------------------------------------------------------------------
// State: S = {(x1,v1), ..., (xn,vn)}  (Section 3.1)
// -----------------------------------------------------------------------
struct State {
    std::unordered_map<std::string, Value> vars;

    [[nodiscard]] bool has(const std::string& k) const { return vars.count(k) != 0; }
    [[nodiscard]] Value getOr(const std::string& k, const Value& def) const {
        auto it = vars.find(k);
        return it != vars.end() ? it->second : def;
    }
    void set(const std::string& k, const Value& v) { vars[k] = v; }

    [[nodiscard]] std::string signature() const {
        std::map<std::string, std::string> sorted;
        for (const auto& [k, v] : vars) sorted[k] = v.asString();
        std::string sig;
        for (const auto& [k, v] : sorted) sig += k + "=" + v + ";";
        return sig;
    }
};

// -----------------------------------------------------------------------
// Condition: a single predicate on a state variable, used for both
// Preconditions P_i, Constraints K_i, and Goal conditions g_i.
// -----------------------------------------------------------------------
struct Condition {
    std::string variable;
    std::string op = "=="; // "==", "!=", ">", "<", ">=", "<="
    Value expected;

    [[nodiscard]] bool evaluate(const State& s) const {
        if (!s.has(variable)) return false;
        Value cur = s.getOr(variable, Value{});
        if (op == "==") return cur == expected;
        if (op == "!=") return cur != expected;
        if (op == ">")  return cur.toNumeric() > expected.toNumeric();
        if (op == "<")  return cur.toNumeric() < expected.toNumeric();
        if (op == ">=") return cur.toNumeric() >= expected.toNumeric();
        if (op == "<=") return cur.toNumeric() <= expected.toNumeric();
        return false;
    }

    static Condition fromJson(const json& j) {
        Condition c;
        c.variable = j.value("variable", "");
        c.op = j.value("op", "==");
        if (j.contains("value")) c.expected = Value::fromJson(j["value"]);
        return c;
    }
};

// -----------------------------------------------------------------------
// Effect: a state change SET/INCREMENT/DECREMENT applied by a capability.
// -----------------------------------------------------------------------
struct Effect {
    std::string variable;
    std::string op = "SET"; // "SET", "INCREMENT", "DECREMENT"
    Value value;

    void apply(State& s) const {
        if (op == "SET") { s.set(variable, value); return; }
        double cur = s.getOr(variable, Value(0.0)).toNumeric();
        if (op == "INCREMENT") s.set(variable, Value(cur + value.toNumeric()));
        else if (op == "DECREMENT") s.set(variable, Value(cur - value.toNumeric()));
    }

    // The RESULTING value this effect leaves in the variable -- needed by
    // the embedding (which encodes effects as a target-state description,
    // not as a raw delta), and by precondition-effect compatibility.
    // NOTE: for INCREMENT/DECREMENT the resulting value is only knowable
    // relative to a base state; encode() uses value.toNumeric() directly
    // as a *documented approximation* for those (rare in these datasets,
    // which use SET almost exclusively for boolean/status effects).
    [[nodiscard]] Value resultingValue() const { return value; }

    static Effect fromJson(const json& j) {
        Effect e;
        e.variable = j.value("variable", "");
        e.op = j.value("op", "SET");
        if (j.contains("value")) e.value = Value::fromJson(j["value"]);
        return e;
    }
};

// -----------------------------------------------------------------------
// Goal: G = {g1, ..., gm}, a PARTIAL state specification (Section 3.2 /
// instructions Section 9). Reuses Condition directly since a goal
// condition and a precondition are the same formal object: a predicate
// that must hold of some state.
// -----------------------------------------------------------------------
struct Goal {
    std::vector<Condition> conditions;
    [[nodiscard]] bool isSatisfied(const State& s) const {
        for (const auto& c : conditions) if (!c.evaluate(s)) return false;
        return true;
    }
};

// -----------------------------------------------------------------------
// Port: an input or output specification (Section 4.2).
//   input  i = (name, type, domain, required)
//   output o = (name, type, domain)
// -----------------------------------------------------------------------
struct Port {
    std::string name;
    std::string type;   // e.g. "UUID", "double", "string"
    std::string domain; // documentation only, not used in matching
    bool required = true; // meaningful for inputs; ignored for outputs

    static Port fromJson(const json& j) {
        Port p;
        p.name = j.value("name", "");
        p.type = j.value("type", "string");
        p.domain = j.value("domain", "any");
        p.required = j.value("required", true);
        return p;
    }
};

// -----------------------------------------------------------------------
// QualityAttributes: Q_i = (C_time, C_resource, C_money, C_risk) (Section
// 4.5). C_energy is omitted -- not meaningfully distinct from C_resource
// for the domains modeled here (documented simplification).
// -----------------------------------------------------------------------
struct QualityAttributes {
    double timeMs = 100.0;
    double money = 0.01;
    double resource = 1.0;
    double risk = 0.05;

    static QualityAttributes fromJson(const json& j) {
        QualityAttributes q;
        if (j.is_null()) return q;
        q.timeMs = j.value("timeMs", 100.0);
        q.money = j.value("money", 0.01);
        q.resource = j.value("resource", 1.0);
        q.risk = j.value("risk", 0.05);
        return q;
    }
};

// -----------------------------------------------------------------------
// Capability: the formal 11-tuple (Section 4).
//   C_i = (T_i, I_i, O_i, P_i, E_i, K_i, R_i, Q_i, Rel_i, A_i, M_i)
// A capability is an OPERATION, never a State -- see file header.
// -----------------------------------------------------------------------
struct Capability {
    std::string id;
    std::string name;
    std::string description;

    std::string type = "SERVICE";           // T_i in {API,DATABASE,GUI,EVENT,FUNCTION,FILE,COMPUTATION,MESSAGE,SERVICE}
    std::vector<Port> inputs;               // I_i
    std::vector<Port> outputs;              // O_i
    std::vector<Condition> preconditions;   // P_i
    std::vector<Effect> effects;            // E_i
    std::vector<Condition> constraints;     // K_i
    std::vector<std::string> resources;     // R_i
    QualityAttributes qos;                  // Q_i
    double reliability = 0.99;              // Rel_i in [0,1]
    bool availability = true;               // A_i in {0,1} (Section 4.6: static case; time-dependent A_i(t) is a documented non-goal, Section 16)
    std::string mechanism;                  // M_i, e.g. "POST /orders", "INSERT orders"

    [[nodiscard]] bool isApplicable(const State& s) const {
        for (const auto& p : preconditions) if (!p.evaluate(s)) return false;
        for (const auto& k : constraints) if (!k.evaluate(s)) return false;
        return availability;
    }

    [[nodiscard]] State apply(const State& s) const {
        State next = s;
        for (const auto& e : effects) e.apply(next);
        return next;
    }

    static Capability fromJson(const json& j) {
        Capability c;
        c.id = j.value("id", "");
        c.name = j.value("name", c.id);
        c.description = j.value("description", "");
        c.type = j.value("type", "SERVICE");
        c.mechanism = j.value("mechanism", "");
        c.reliability = j.value("reliability", 0.99);
        c.availability = j.value("availability", true);
        if (j.contains("qos")) c.qos = QualityAttributes::fromJson(j["qos"]);
        if (j.contains("inputs")) for (const auto& x : j["inputs"]) c.inputs.push_back(Port::fromJson(x));
        if (j.contains("outputs")) for (const auto& x : j["outputs"]) c.outputs.push_back(Port::fromJson(x));
        if (j.contains("preconditions")) for (const auto& x : j["preconditions"]) c.preconditions.push_back(Condition::fromJson(x));
        if (j.contains("effects")) for (const auto& x : j["effects"]) c.effects.push_back(Effect::fromJson(x));
        if (j.contains("constraints")) for (const auto& x : j["constraints"]) c.constraints.push_back(Condition::fromJson(x));
        if (j.contains("resources")) for (const auto& x : j["resources"]) c.resources.push_back(x.get<std::string>());
        return c;
    }
};

// -----------------------------------------------------------------------
// ApplicationProblem: the formal application A = (S, C, S_I, G, R, K)
// (Section 3). "S" (the full state space) is implicit -- it is never
// enumerated, only ever referenced through S_I, goal conditions, and
// capability pre/effects, exactly as real formal-planning specifications
// do (the full state space is combinatorially large and never needed
// explicitly for this assignment's embedding-only scope).
// -----------------------------------------------------------------------
struct ApplicationProblem {
    std::string problemName;
    std::string domain;
    State initialState;                       // S_I
    Goal goal;                                 // G
    std::vector<Capability> capabilities;      // C
    std::vector<std::string> resources;        // R
    std::vector<Condition> globalConstraints;  // K

    [[nodiscard]] const Capability* find(const std::string& id) const {
        for (const auto& c : capabilities) if (c.id == id) return &c;
        return nullptr;
    }
};

} // namespace capembed
