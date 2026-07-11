// JaiScript Foundry Test Runner
// Auto-discovers and runs all registered Foundry test suites

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#if defined(_MSC_VER) && !defined(NDEBUG)
#include <atomic>
#include <crtdbg.h>
#include <csignal>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif
#include <iostream>
#include <string>
#include <chrono>
#include <sstream>

namespace jai::foundry {

namespace {
    jai::backend_type g_test_backend = jai::backend_type::interpreter;
}

std::shared_ptr<jai::engine> make_engine() {
    auto eng = jai::engine::make();
    if (g_test_backend != jai::backend_type::interpreter) {
        eng->set_backend(g_test_backend);
    }
    return eng;
}

} // namespace jai::foundry

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
    bool has_dot = false;   // true only for an explicit "suite.test" filter

    test_filter_config() : suite_pattern("*"), test_pattern("*") {}

    static test_filter_config parse(const std::string& filter_str) {
        test_filter_config result;
        if (filter_str.empty()) {
            return result;
        }

        // Check for dot separator (suite.test pattern)
        size_t dot_pos = filter_str.find('.');
        if (dot_pos != std::string::npos) {
            result.has_dot = true;
            result.suite_pattern = filter_str.substr(0, dot_pos);
            result.test_pattern = filter_str.substr(dot_pos + 1);
        } else {
            // No dot - ADDITIVE bare pattern: match the suite name OR a test name (handled in the
            // run loop). Both fields hold the bare pattern; the loop decides per suite.
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

#if defined(_MSC_VER) && !defined(NDEBUG)
static void print_native_backtrace() {
    static std::atomic<bool> printing{false};
    bool expected = false;
    if (!printing.compare_exchange_strong(expected, true)) {
        return;   // racing losers continue; the winner prints and terminates below
    }
    void* frames[62];
    const USHORT n = CaptureStackBackTrace(0, 62, frames, nullptr);
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    fprintf(stderr, "--- abort backtrace (%u frames) ---\n", n);
    for (USHORT i = 0; i < n; ++i) {
        DWORD64 disp = 0;
        if (SymFromAddr(proc, reinterpret_cast<DWORD64>(frames[i]), &disp, sym)) {
            fprintf(stderr, "  %02u %s +0x%llx\n", i, sym->Name, static_cast<unsigned long long>(disp));
        } else {
            fprintf(stderr, "  %02u %p\n", i, frames[i]);
        }
    }
    fflush(stderr);
#ifdef JAISCRIPT_DIAG_XTHREAD_RC
    TerminateProcess(GetCurrentProcess(), 3);   // canary hook owns termination
#endif
}
#endif

int main(int argc, char** argv) {
    using namespace jai::foundry;

#if defined(_MSC_VER) && !defined(NDEBUG)
    // CRT asserts print to stderr and abort instead of hanging the run on a dialog
    // (a Debug assert in a headless/CI invocation was an invisible wedge)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    // Direct abort() calls must reach the SIGABRT handler below, not Watson fast-fail
    _set_abort_behavior(0, _CALL_REPORTFAULT);
    // Symbolized native stack on abort: an assert in a headless run names its call path
    signal(SIGABRT, [](int) { print_native_backtrace(); });
#ifdef JAISCRIPT_DIAG_XTHREAD_RC
    // Canary builds report directly at the racing touch (signals lose concurrent aborts)
    jai::detail::diag_xthread_report = &print_native_backtrace;
#endif
#endif

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
        // backend selection: --backend=interpreter|vm
        else if (arg.rfind("--backend=", 0) == 0) {
            std::string backend_name = arg.substr(10);
            if (backend_name == "interpreter") {
                g_test_backend = jai::backend_type::interpreter;
            } else if (backend_name == "vm") {
                g_test_backend = jai::backend_type::vm;
            } else {
                std::cout << "Unknown backend: \"" << backend_name << "\" (expected interpreter or vm)\n";
                return 1;
            }
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
    std::cout << "Backend: " << (g_test_backend == jai::backend_type::vm ? "vm" : "interpreter") << "\n";

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
        bool suite_matches = filter.matches_suite(suite->get_name());

        std::string test_pattern_for_suite;
        if (filter.has_dot) {
            // Explicit "suite.test": the suite must match, then filter tests within it.
            if (!suite_matches) {
                continue;
            }
            test_pattern_for_suite = filter.test_pattern;
        } else {
            // Bare pattern is ADDITIVE: if it names this suite, run ALL of the suite's tests;
            // otherwise fall back to matching individual test names (so e.g. a bare test-name
            // filter still selects matching tests across every suite). This never SUBTRACTS the
            // tests of a matched suite.
            test_pattern_for_suite = suite_matches ? std::string("*") : filter.test_pattern;
        }

        auto result = suite->quench(test_pattern_for_suite, verbose);
        if (result.total() > 0) {
            suites_run++;
        }
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
    if (total_tests == 0 && filter.suite_pattern != "*") {
        std::cout << "No tests matched filter: \"" << filter.suite_pattern
                  << (filter.has_dot ? ("." + filter.test_pattern) : std::string()) << "\"\n\n";
        std::cout << "Available suites:\n";
        for (const auto& suite : suites) {
            std::cout << "  - " << suite->get_name() << "\n";
        }
        std::cout << "-----------------------------------------\n";
        return 1;
    } else if (total_failures == 0) {
        std::cout << "<3 :D All tests passed! (" << suites_run << " suites, " << total_tests << " tests, " << seconds << "s)\n";
    } else {
        std::cout << "x " << total_failures << " of " << total_tests << " test(s) failed (" << seconds << "s)\n";
    }
    std::cout << "-----------------------------------------\n";

    return total_failures > 0 ? 1 : 0;
}
