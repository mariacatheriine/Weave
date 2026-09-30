#pragma once
// =============================================================================
// dataset.hpp -- loads a formally specified ApplicationProblem from a JSON
// file matching the schema documented in DELIVERABLE_3_EXPERIMENTAL_DATASET.md.
// =============================================================================
#include <fstream>
#include <stdexcept>
#include "types.hpp"

namespace capembed {

inline ApplicationProblem loadProblem(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open dataset file: " + path);
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error("Malformed JSON in " + path + ": " + e.what());
    }

    ApplicationProblem app;
    app.problemName = j.value("problemName", "UnnamedProblem");
    app.domain = j.value("domain", "Unspecified");

    if (j.contains("initialState") && j["initialState"].is_object()) {
        for (auto& [k, v] : j["initialState"].items()) app.initialState.set(k, Value::fromJson(v));
    }
    if (j.contains("goal") && j["goal"].is_array()) {
        for (const auto& g : j["goal"]) app.goal.conditions.push_back(Condition::fromJson(g));
    }
    if (j.contains("resources") && j["resources"].is_array()) {
        for (const auto& r : j["resources"]) app.resources.push_back(r.get<std::string>());
    }
    if (j.contains("globalConstraints") && j["globalConstraints"].is_array()) {
        for (const auto& k : j["globalConstraints"]) app.globalConstraints.push_back(Condition::fromJson(k));
    }
    if (j.contains("capabilities") && j["capabilities"].is_array()) {
        for (const auto& c : j["capabilities"]) {
            Capability cap = Capability::fromJson(c);
            if (cap.id.empty()) throw std::runtime_error("Capability with empty id in " + path);
            if (app.find(cap.id) != nullptr) throw std::runtime_error("Duplicate capability id '" + cap.id + "' in " + path);
            app.capabilities.push_back(cap);
        }
    } else {
        throw std::runtime_error("Dataset " + path + " has no 'capabilities' array");
    }
    return app;
}

} // namespace capembed
