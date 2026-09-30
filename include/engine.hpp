#pragma once
// =============================================================================
// engine.hpp -- presentation/export utilities: readable vector inspection
// (instructions Section 32: "clearly label every subspace"), CSV export of
// experiment results (instructions Section 43), and simple memory/timing
// measurement (Section 34-35).
// =============================================================================
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include "composition.hpp"
#include "embedding.hpp"
#include "types.hpp"

namespace capembed {

inline void printVector(const std::string& label, const std::vector<double>& v, int width = 6) {
    std::cout << label << " (dim=" << v.size() << "): [";
    for (size_t i = 0; i < v.size(); ++i) {
        std::cout << std::fixed << std::setprecision(2) << std::setw(width) << v[i];
        if (i + 1 < v.size()) std::cout << ",";
    }
    std::cout << "]\n";
}

// Full, clearly-labeled inspection of one capability's embedding, split by
// subspace exactly as designed (instructions Section 32).
inline void inspectCapabilityVector(const EmbeddingEngine& eng, const Capability& c) {
    auto cv = eng.encodeCapability(c);
    std::cout << "\nCapability: " << c.id << "  (" << c.name << ")\n";
    std::cout << std::string(70, '-') << "\n";
    std::cout << "  Precondition mask : [";
    for (size_t i = 0; i < cv.pre.mask.size(); ++i) std::cout << (cv.pre.mask[i] > 0.5 ? "1" : "0") << (i + 1 < cv.pre.mask.size() ? "," : "");
    std::cout << "]\n";
    printVector("  Precondition value", cv.pre.val);
    std::cout << "  Effect mask        : [";
    for (size_t i = 0; i < cv.eff.mask.size(); ++i) std::cout << (cv.eff.mask[i] > 0.5 ? "1" : "0") << (i + 1 < cv.eff.mask.size() ? "," : "");
    std::cout << "]\n";
    printVector("  Effect value      ", cv.eff.val);
    printVector("  Input ports       ", cv.in);
    printVector("  Output ports      ", cv.out);
    printVector("  Mechanism (type+hash)", cv.mechanism);
    std::cout << "  Operational [time,money,resource,riskExp,unrelExp,avail]:\n  ";
    printVector("", cv.operational);
    std::cout << "  FULL vector dim = " << cv.full().size()
              << "  (functional=" << cv.functional.size()
              << ", mechanism=" << cv.mechanism.size()
              << ", operational=" << cv.operational.size() << ")\n";
}

// -----------------------------------------------------------------------
// Simple CSV writer helper.
// -----------------------------------------------------------------------
class CsvWriter {
public:
    explicit CsvWriter(const std::string& path) : out_(path) {
        if (!out_.is_open()) throw std::runtime_error("Cannot open results file: " + path);
    }
    void header(const std::vector<std::string>& cols) { writeRow(cols); }
    void row(const std::vector<std::string>& cols) { writeRow(cols); }

private:
    std::ofstream out_;
    void writeRow(const std::vector<std::string>& cols) {
        for (size_t i = 0; i < cols.size(); ++i) {
            out_ << cols[i];
            if (i + 1 < cols.size()) out_ << ",";
        }
        out_ << "\n";
    }
};

inline std::string dbl(double v, int prec = 6) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

// -----------------------------------------------------------------------
// Complexity / efficiency measurement (instructions Section 34-35).
// Reports actual measured values -- never invented ones.
// -----------------------------------------------------------------------
struct EfficiencyReport {
    size_t numCapabilities = 0;
    size_t stateVarDim = 0;
    size_t portDim = 0;
    size_t typeDim = 0;
    size_t fullVectorDim = 0;
    size_t totalEmbeddingBytes = 0; // measured: numCapabilities * fullVectorDim * sizeof(double)
    double pairwiseSimilarityMicroseconds = 0.0; // measured wall time for all-pairs similarity
};

inline EfficiencyReport measureEfficiency(const EmbeddingEngine& eng, const ApplicationProblem& app) {
    EfficiencyReport r;
    r.numCapabilities = app.capabilities.size();
    r.stateVarDim = eng.stateDim();
    r.portDim = eng.portDim();
    r.typeDim = eng.typeDim();
    if (!app.capabilities.empty()) r.fullVectorDim = eng.encode(app.capabilities.front()).size();
    r.totalEmbeddingBytes = r.numCapabilities * r.fullVectorDim * sizeof(double);

    std::vector<std::vector<double>> vecs;
    vecs.reserve(app.capabilities.size());
    for (const auto& c : app.capabilities) vecs.push_back(eng.encode(c));

    auto t0 = std::chrono::high_resolution_clock::now();
    volatile double sink = 0.0;
    for (size_t i = 0; i < vecs.size(); ++i)
        for (size_t j = 0; j < vecs.size(); ++j)
            sink += EmbeddingEngine::cosine(vecs[i], vecs[j]);
    auto t1 = std::chrono::high_resolution_clock::now();
    (void)sink;
    r.pairwiseSimilarityMicroseconds = std::chrono::duration<double, std::micro>(t1 - t0).count();
    return r;
}

} // namespace capembed
