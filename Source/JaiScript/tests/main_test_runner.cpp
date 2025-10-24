// JaiScript Foundry Test Runner
// Auto-discovers and runs all registered Foundry test suites

#include <jaiscript/testing/foundry.hpp>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace jai::foundry;

    std::cout << ".________________________________________.\n";
    std::cout << "|       JaiScript Foundry Tests          |\n";
    std::cout << "|________________________________________|\n";
    std::cout << "\n";

    // Get all auto-registered test suites
    auto suites = test_registry::instance().create_all_suites();

    std::cout << "Discovered " << suites.size() << " test suites\n";

    // Optional filter from command line
    std::string filter;
    if (argc > 1) {
        filter = argv[1];
        std::cout << "Running tests matching filter: '" << filter << "'\n";
    }
    std::cout << "\n";

    // Run all test suites
    int total_failures = 0;
    int suites_run = 0;

    for (auto& suite : suites) {
        // Skip if filter doesn't match
        if (!filter.empty() && suite->get_name().find(filter) == std::string::npos) {
            continue;
        }

        suites_run++;
        total_failures += suite->quench();
    }

    // Summary
    std::cout << "\n";
    std::cout << "-----------------------------------------\n";
    if (total_failures == 0) {
        std::cout << "<3 :D All tests passed! (" << suites_run << " suites)\n";
    } else {
        std::cout << "x " << total_failures << " test(s) failed\n";
    }
    std::cout << "-----------------------------------------\n";

    return total_failures > 0 ? 1 : 0;
}
