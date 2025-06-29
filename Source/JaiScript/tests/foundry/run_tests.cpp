// Quench - The main test runner for foundry tests
// Test suites automatically register themselves - no need to manually update this file!

#include <jaiscript/testing/foundry.hpp>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace jai::foundry;
    
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║       JaiScript Foundry Tests          ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    // Get all auto-registered test suites
    auto suites = test_registry::instance().create_all_suites();
    
    // Run tests based on command line args
    std::string filter;
    if (argc > 1) {
        filter = argv[1];
        std::cout << "\nRunning tests matching: " << filter << "\n";
    }
    
    int total_failures = 0;
    for (auto& test_suite : suites) {
        // Skip if filter doesn't match
        if (!filter.empty() && test_suite->get_name().find(filter) == std::string::npos) {
            continue;
        }
        
        total_failures += test_suite->quench();
    }
    
    std::cout << "\n" << (total_failures == 0 ? "✅ All tests passed!" : "❌ Some tests failed") << "\n";
    return total_failures > 0 ? 1 : 0;
}