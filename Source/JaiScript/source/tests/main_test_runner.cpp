// JaiScript Foundry Test Runner
// Auto-discovers and runs all registered Foundry test suites

#include <jaiscript/testing/foundry.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <sstream>

// Parse filter from command line arguments
// Supports:
//   --gtest_filter=<pattern>  (gtest style)
//   --jaitest_filter=<pattern> (jaitest style, synonym)
//   --filter=<pattern>        (explicit)
//   <pattern>                 (positional)
// Pattern format: suite_filter.test_filter or just pattern for both
// Wildcard * matches anything
struct test_filter_config {
    std::string suite_pattern;
    std::string test_pattern;

    test_filter_config() : suite_pattern("*"), test_pattern("*") {}

    static test_filter_config parse(const std::string& filter_str) {
        test_filter_config result;
        if (filter_str.empty()) {
            return result;
        }

        // Check for dot separator (suite.test pattern)
        size_t dot_pos = filter_str.find('.');
        if (dot_pos != std::string::npos) {
            result.suite_pattern = filter_str.substr(0, dot_pos);
            result.test_pattern = filter_str.substr(dot_pos + 1);
        } else {
            // No dot - use as filter for both suites and tests
            result.suite_pattern = filter_str;
            result.test_pattern = filter_str;
        }

        // Handle empty parts as wildcards
        if (result.suite_pattern.empty()) result.suite_pattern = "*";
        if (result.test_pattern.empty()) result.test_pattern = "*";

        return result;
    }

    // Simple wildcard matching (* matches any sequence)
    static bool matches(const std::string& pattern, const std::string& text) {
        if (pattern == "*") return true;

        // If no wildcards, do substring match
        if (pattern.find('*') == std::string::npos) {
            return text.find(pattern) != std::string::npos;
        }

        // Simple wildcard matching
        size_t pi = 0, ti = 0;
        size_t star_pi = std::string::npos, star_ti = 0;

        while (ti < text.size()) {
            if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
                pi++;
                ti++;
            } else if (pi < pattern.size() && pattern[pi] == '*') {
                star_pi = pi++;
                star_ti = ti;
            } else if (star_pi != std::string::npos) {
                pi = star_pi + 1;
                ti = ++star_ti;
            } else {
                return false;
            }
        }

        while (pi < pattern.size() && pattern[pi] == '*') {
            pi++;
        }

        return pi == pattern.size();
    }

    bool matches_suite(const std::string& suite_name) const {
        return matches(suite_pattern, suite_name);
    }

    bool matches_test(const std::string& test_name) const {
        return matches(test_pattern, test_name);
    }
};

int main(int argc, char** argv) {
    using namespace jai::foundry;

    // Parse command line flags
    // FOUNDRY_VERBOSE can be set at compile time (e.g., for benchmark builds)
#ifdef FOUNDRY_VERBOSE
    bool verbose = true;
#else
    bool verbose = false;
#endif
    test_filter_config filter;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        // Verbose flag
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
            continue;
        }

        // gtest style: --gtest_filter=pattern
        if (arg.rfind("--gtest_filter=", 0) == 0) {
            filter = test_filter_config::parse(arg.substr(15));
        }
        // jaitest style (synonym): --jaitest_filter=pattern
        else if (arg.rfind("--jaitest_filter=", 0) == 0) {
            filter = test_filter_config::parse(arg.substr(17));
        }
        // explicit: --filter=pattern
        else if (arg.rfind("--filter=", 0) == 0) {
            filter = test_filter_config::parse(arg.substr(9));
        }
        // positional argument (filter)
        else if (arg[0] != '-') {
            filter = test_filter_config::parse(arg);
        }
    }

    // Run all test suites
    auto suites = test_registry::instance().create_all_suites();

    std::cout << "/*----------------------------*\\\n";
    std::cout << "| JaiScript Foundry Test Suite |\n";
    std::cout << "\\*----------------------------*/\n";

    if (!verbose) {
        std::cout << "Performing Tests, One Moment ..." << std::flush;
    }

    auto start_time = std::chrono::steady_clock::now();

    int total_failures = 0;
    int total_passed = 0;
    int suites_run = 0;
    std::vector<failure_info> all_failures;

    // Redirect stdout/stderr when not verbose to suppress test output
    std::streambuf* original_cout = nullptr;
    std::streambuf* original_cerr = nullptr;
    std::ostringstream null_stream;
    if (!verbose) {
        original_cout = std::cout.rdbuf(null_stream.rdbuf());
        original_cerr = std::cerr.rdbuf(null_stream.rdbuf());
    }

    for (auto& suite : suites) {
        // Skip if filter doesn't match suite
        if (!filter.matches_suite(suite->get_name())) {
            continue;
        }

        suites_run++;
        auto result = suite->quench(filter.test_pattern, verbose);
        total_failures += result.failed;
        total_passed += result.passed;

        // Collect failures
        for (const auto& f : result.failures) {
            all_failures.push_back(f);
        }
    }

    // Restore stdout/stderr
    if (original_cout) {
        std::cout.rdbuf(original_cout);
        std::cerr.rdbuf(original_cerr);
    }

    // Summary
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double seconds = duration.count() / 1000.0;

    int total_tests = total_passed + total_failures;

    // Show failures (always, if any)
    if (!all_failures.empty()) {
        std::cout << "\nFailed tests:\n";
        for (const auto& f : all_failures) {
            std::cout << "  x " << f.suite_name << " :: " << f.test_name << "\n";
            std::cout << "      " << f.error_message << "\n";
        }
    }

    std::cout << "\n-----------------------------------------\n";
    if (total_failures == 0) {
        std::cout << "<3 :D All tests passed! (" << suites_run << " suites, " << total_tests << " tests, " << seconds << "s)\n";
    } else {
        std::cout << "x " << total_failures << " of " << total_tests << " test(s) failed (" << seconds << "s)\n";
    }
    std::cout << "-----------------------------------------\n";

    return total_failures > 0 ? 1 : 0;
}
