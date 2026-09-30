// =============================================================================
// main.cpp -- CLI for the Assignment 2 capability-embedding system.
//
//   ./capability_embedding data/compatibility.json --vectors
//   ./capability_embedding data/compatibility.json --similarity C1 C2
//   ./capability_embedding data/compatibility.json --compatibility C1 C2
//   ./capability_embedding data/composition.json --compose C1 C2 C3
//   ./capability_embedding --experiments
//   ./capability_embedding --help
// =============================================================================
#include <iostream>
#include "compatibility.hpp"
#include "composition.hpp"
#include "dataset.hpp"
#include "embedding.hpp"
#include "engine.hpp"
#include "experiments.hpp"

using namespace capembed;

static void printHelp() {
    std::cout <<
        "SafePath Assignment 2 -- Vector Embedding for Capability Composition\n\n"
        "Usage:\n"
        "  ./capability_embedding <dataset.json> --vectors\n"
        "      Print the labeled, subspace-by-subspace embedding of every capability.\n\n"
        "  ./capability_embedding <dataset.json> --similarity <id1> <id2>\n"
        "      Print functional / implementation / overall cosine similarity.\n\n"
        "  ./capability_embedding <dataset.json> --compatibility <id1> <id2>\n"
        "      Print precondition-effect and input-output compatibility.\n\n"
        "  ./capability_embedding <dataset.json> --compose <id1> <id2> [id3 ...]\n"
        "      Compose a chain and print the resulting composite capability\n"
        "      and the vector-homomorphism check.\n\n"
        "  ./capability_embedding --experiments\n"
        "      Run all five required experiments plus the cross-domain check,\n"
        "      exporting results/*.csv.\n\n"
        "  ./capability_embedding --help\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help") { printHelp(); return 0; }

    if (args[0] == "--experiments") {
        try {
            runAll("data");
        } catch (const std::exception& e) {
            std::cerr << "Error running experiments: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    // All remaining modes take a dataset path first.
    std::string datasetPath = args[0];
    ApplicationProblem app;
    try {
        app = loadProblem(datasetPath);
    } catch (const std::exception& e) {
        std::cerr << "Error loading dataset: " << e.what() << "\n";
        return 1;
    }
    EmbeddingEngine eng;
    eng.initialize(app);

    std::cout << "Loaded problem: " << app.problemName << "  (domain: " << app.domain << ")\n";
    std::cout << "  Capabilities: " << app.capabilities.size()
              << "  | State variable dim: " << eng.stateDim()
              << "  | Port dim: " << eng.portDim()
              << "  | Type dim: " << eng.typeDim() << "\n";

    if (args.size() < 2) { std::cout << "\n(no mode flag given -- pass --vectors, --similarity, --compatibility, --compose, or --help)\n"; return 0; }
    std::string mode = args[1];

    if (mode == "--vectors") {
        for (const auto& c : app.capabilities) inspectCapabilityVector(eng, c);
        return 0;
    }

    if (mode == "--similarity") {
        if (args.size() < 4) { std::cerr << "Usage: --similarity <id1> <id2>\n"; return 1; }
        const Capability* c1 = app.find(args[2]);
        const Capability* c2 = app.find(args[3]);
        if (!c1 || !c2) { std::cerr << "Unknown capability id.\n"; return 1; }
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "\nfunctionalSimilarity(" << c1->id << ", " << c2->id << ") = " << eng.functionalSimilarity(*c1, *c2) << "\n";
        std::cout << "implementationSimilarity(" << c1->id << ", " << c2->id << ") = " << eng.implementationSimilarity(*c1, *c2) << "\n";
        std::cout << "overallSimilarity(" << c1->id << ", " << c2->id << ") = " << eng.overallSimilarity(*c1, *c2) << "\n";
        return 0;
    }

    if (mode == "--compatibility") {
        if (args.size() < 4) { std::cerr << "Usage: --compatibility <id1> <id2>\n"; return 1; }
        const Capability* c1 = app.find(args[2]);
        const Capability* c2 = app.find(args[3]);
        if (!c1 || !c2) { std::cerr << "Unknown capability id.\n"; return 1; }
        FullCompatibility fc = compatibility(*c1, *c2);
        std::cout << "\nprecondition_effect_compatibility(" << c1->id << ", " << c2->id << "):\n";
        std::cout << "  satisfied=" << fc.preEff.satisfiedCount << " violated=" << fc.preEff.violatedCount
                  << " neutral=" << fc.preEff.neutralCount << " degree=" << fc.preEff.degree << "\n";
        std::cout << "  isComposable = " << (fc.isComposable() ? "TRUE" : "FALSE") << "\n";
        std::cout << "input_output_compatibility(" << c1->id << ", " << c2->id << "):\n";
        std::cout << "  coverage=" << fc.io.coverage << "  (" << fc.io.matchedRequired << "/" << fc.io.totalRequired << " required inputs matched)\n";
        return 0;
    }

    if (mode == "--compose") {
        if (args.size() < 4) { std::cerr << "Usage: --compose <id1> <id2> [id3 ...]\n"; return 1; }
        std::vector<Capability> chain;
        for (size_t i = 2; i < args.size(); ++i) {
            const Capability* c = app.find(args[i]);
            if (!c) { std::cerr << "Unknown capability id: " << args[i] << "\n"; return 1; }
            chain.push_back(*c);
        }
        Capability composite = composeChain(chain);
        std::cout << "\nComposite: " << composite.id << "\n";
        std::cout << "  Preconditions: ";
        for (const auto& p : composite.preconditions) std::cout << p.variable << p.op << p.expected.asString() << " ";
        std::cout << "\n  Effects: ";
        for (const auto& e : composite.effects) std::cout << e.variable << "=" << e.value.asString() << " ";
        std::cout << "\n  Time=" << composite.qos.timeMs << "ms  Money=$" << composite.qos.money
                  << "  Reliability=" << composite.reliability << "  Availability=" << (composite.availability ? "true" : "false") << "\n";
        if (chain.size() == 2) {
            HomomorphismCheck h = verifyHomomorphism(eng, chain[0], chain[1]);
            std::cout << "  Homomorphism check: effects exact=" << h.effectsMatch
                      << " time/money exact=" << h.operationalTimeMoneyMatch
                      << " risk/reliability exact=" << h.operationalRiskReliabilityMatch << "\n";
        }
        return 0;
    }

    std::cerr << "Unknown mode: " << mode << "\n";
    printHelp();
    return 1;
}
