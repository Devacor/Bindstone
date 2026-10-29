// JaiScript Foundry Test Runner
// Auto-discovers and runs all registered Foundry test suites

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <atomic>
#include <csignal>
#include <cstddef>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cwchar>
#if defined(_MSC_VER)
#pragma comment(lib, "dbghelp.lib")
#endif
#else
#include <unistd.h>
#endif
#include <iostream>
#include <string>
#include <chrono>
#include <sstream>
#include <jaiscript/testing/unattended.hpp>

namespace jai::foundry {

namespace {
    // a host linked without the interpreter (mv_tests) defaults to the vm
    jai::backend_type g_test_backend = jai::detail::interpreter_compiled() ? jai::backend_type::interpreter : jai::backend_type::vm;
    bool g_test_vm_reference = false;   // --backend=vm-reference: the vm in engine::vm_reference_mode
    std::atomic<const char*> g_running_suite{nullptr};
    std::atomic<const char*> g_running_test{nullptr};
}

void note_running_test(const char* suite_name, const char* test_name) {
    g_running_suite.store(suite_name, std::memory_order_relaxed);
    g_running_test.store(test_name, std::memory_order_relaxed);
}

std::shared_ptr<jai::engine> make_engine() {
    auto eng = jai::engine::make();
    eng->set_backend(g_test_backend);
    eng->vm_reference_mode(g_test_vm_reference);
    eng->import_recheck_interval(0);   // test engines touch a file and re-import at once: every reach checks (the throttle has its own tests)
    return eng;
}

bool vm_reference_run() { return g_test_vm_reference; }

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

// A crash names the running test even without --verbose (which hides the run's streams); the text is built without
// allocating, so a corrupt heap still reports
namespace crash_report {

struct report_text {
    char bytes[1024];
    std::size_t length = 0;

    void append(const char* text) {
        for (; text && *text && length < sizeof(bytes); ++text) {
            bytes[length++] = *text;
        }
    }

    void append_number(unsigned long long value, unsigned base) {
        char digits[24];
        int count = 0;
        do {
            digits[count++] = "0123456789ABCDEF"[value % base];
            value /= base;
        } while (value != 0);
        while (count > 0 && length < sizeof(bytes)) {
            bytes[length++] = digits[--count];
        }
    }

    void append_running_test() {
        const char* suite_name = jai::foundry::g_running_suite.load(std::memory_order_relaxed);
        const char* test_name = jai::foundry::g_running_test.load(std::memory_order_relaxed);
        if (!suite_name) {
            append("no test (outside the suites)");
            return;
        }
        append(suite_name);
        append(" :: ");
        append(test_name);
    }
};

#if defined(_WIN32)
wchar_t g_dump_directory[MAX_PATH];
wchar_t g_dump_path[MAX_PATH];
char g_dump_path_text[MAX_PATH * 3];

struct crash_context {
    DWORD code;
    EXCEPTION_POINTERS* exception;
    DWORD thread_id;
};

void write_to_stderr(const report_text& text) {
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.bytes, static_cast<DWORD>(text.length), &written, nullptr);
}

DWORD WINAPI write_crash_report(void* context_pointer) {
    const auto& context = *static_cast<crash_context*>(context_pointer);
    report_text text;
    text.append("\n*** crash 0x");
    text.append_number(context.code, 16);
    text.append(" while running ");
    text.append_running_test();
    text.append("\n");
    write_to_stderr(text);
    if (g_dump_path[0] != 0) {
        CreateDirectoryW(g_dump_directory, nullptr);
        HANDLE file = CreateFileW(g_dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        MINIDUMP_EXCEPTION_INFORMATION exception_information{context.thread_id, context.exception, FALSE};
        const bool dumped = file != INVALID_HANDLE_VALUE && MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
            context.exception ? &exception_information : nullptr, nullptr, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        report_text dump_text;
        dump_text.append(dumped ? "*** minidump written: " : "*** minidump failed: ");
        dump_text.append(g_dump_path_text);
        dump_text.append("\n");
        write_to_stderr(dump_text);
    }
    return 0;
}

void report_crash(DWORD code, EXCEPTION_POINTERS* exception) {
    static std::atomic<bool> reported{false};
    if (reported.exchange(true)) {
        Sleep(INFINITE);   // the first crash reports and ends the process
    }
    crash_context context{code, exception, GetCurrentThreadId()};
    // a fresh thread has the stack an overflowed one lacks, and a bounded wait cannot wedge the run
    if (HANDLE reporter = CreateThread(nullptr, 0, &write_crash_report, &context, 0, nullptr)) {
        WaitForSingleObject(reporter, 60000);
        CloseHandle(reporter);
    } else {
        write_crash_report(&context);
    }
}

LONG WINAPI report_unhandled_exception(EXCEPTION_POINTERS* exception) {
    report_crash(exception->ExceptionRecord->ExceptionCode, exception);
    TerminateProcess(GetCurrentProcess(), exception->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

void report_abort(int) {
#if defined(_MSC_VER) && !defined(NDEBUG)
    print_native_backtrace();
#endif
    report_crash(3, nullptr);   // the CRT exits with 3 once the handler returns
}

void install() {
    const DWORD directory_length = GetEnvironmentVariableW(L"JAI_CRASH_DUMP_DIR", g_dump_directory, MAX_PATH);
    if (directory_length > 0 && directory_length < MAX_PATH) {
        wchar_t executable[MAX_PATH];
        const DWORD executable_length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        wchar_t* stem = executable;
        if (executable_length > 0 && executable_length < MAX_PATH) {
            if (wchar_t* separator = std::wcsrchr(executable, L'\\')) {
                stem = separator + 1;
            }
            if (wchar_t* extension = std::wcsrchr(stem, L'.')) {
                *extension = 0;
            }
        } else {
            executable[0] = 0;
        }
        if (std::swprintf(g_dump_path, MAX_PATH, L"%ls\\%ls_%lu.dmp", g_dump_directory, stem[0] ? stem : L"crash",
                static_cast<unsigned long>(GetCurrentProcessId())) < 0) {
            g_dump_path[0] = 0;
        }
        WideCharToMultiByte(CP_UTF8, 0, g_dump_path, -1, g_dump_path_text, sizeof(g_dump_path_text), nullptr, nullptr);
    }
    SetUnhandledExceptionFilter(&report_unhandled_exception);
    signal(SIGABRT, &report_abort);
}
#else
alignas(16) char g_alternate_stack[1 << 16];

void report_crash_signal(int signal_number) {
    report_text text;
    text.append("\n*** crash signal ");
    text.append_number(static_cast<unsigned long long>(signal_number), 10);
    text.append(" while running ");
    text.append_running_test();
    text.append("\n");
    const ssize_t written = write(STDERR_FILENO, text.bytes, text.length);
    (void)written;
    raise(signal_number);   // SA_RESETHAND restored the default action: the process dies of this signal
}

void install() {
    stack_t alternate_stack{};
    alternate_stack.ss_sp = g_alternate_stack;
    alternate_stack.ss_size = sizeof(g_alternate_stack);
    sigaltstack(&alternate_stack, nullptr);   // a main-thread stack overflow still reports
    struct sigaction action{};
    action.sa_handler = &report_crash_signal;
    action.sa_flags = SA_RESETHAND | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal_number : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) {
        sigaction(signal_number, &action, nullptr);
    }
}
#endif

} // namespace crash_report

int main(int argc, char** argv) {
    using namespace jai::foundry;

    route_faults_to_stderr();
    crash_report::install();
#if defined(_MSC_VER) && !defined(NDEBUG)
    // An access violation names its call path too (the first one prints, the process then
    // dies as it would have)
    AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS info) -> LONG {
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) { print_native_backtrace(); }
        return EXCEPTION_CONTINUE_SEARCH;
    });
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
    int shard_index = 0;
    int shard_count = 1;

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
        // backend selection: --backend=interpreter|vm|vm-reference
        else if (arg.rfind("--backend=", 0) == 0) {
            std::string backend_name = arg.substr(10);
            if (backend_name == "interpreter") {
                g_test_backend = jai::backend_type::interpreter;
            } else if (backend_name == "vm") {
                g_test_backend = jai::backend_type::vm;
            } else if (backend_name == "vm-reference") {
                g_test_backend = jai::backend_type::vm;
                g_test_vm_reference = true;
            } else {
                std::cout << "Unknown backend: \"" << backend_name << "\" (expected interpreter, vm or vm-reference)\n";
                return 1;
            }
        }
        // --shard=i/n: runs every n-th suite starting at suite i (0-based), so n processes cover the suites once
        else if (arg.rfind("--shard=", 0) == 0) {
            auto slash = arg.find('/', 8);
            try {
                shard_index = slash == std::string::npos ? -1 : std::stoi(arg.substr(8, slash - 8));
                shard_count = slash == std::string::npos ? 0 : std::stoi(arg.substr(slash + 1));
            } catch (...) {
                shard_count = 0;
            }
            if (shard_count < 1 || shard_index < 0 || shard_index >= shard_count) {
                std::cout << "Bad shard: \"" << arg.substr(8) << "\" (expected i/n with 0 <= i < n)\n";
                return 1;
            }
        }
        // positional argument (filter)
        else if (arg[0] != '-') {
            filter = test_filter_config::parse(arg);
        }
    }

    if (g_test_backend == jai::backend_type::interpreter) {
        try {
            jai::engine::make()->set_backend(g_test_backend);
        } catch (const std::exception& e) {
            std::cout << e.what() << "\n";
            return 1;
        }
    }

    // Run all test suites
    auto suites = test_registry::instance().create_all_suites();

    std::cout << "/*----------------------------*\\\n";
    std::cout << "| JaiScript Foundry Test Suite |\n";
    std::cout << "\\*----------------------------*/\n";
    std::cout << "Backend: " << (g_test_vm_reference ? "vm-reference" : g_test_backend == jai::backend_type::vm ? "vm" : "interpreter") << "\n";

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

    for (size_t suite_position = 0; suite_position < suites.size(); ++suite_position) {
        auto& suite = suites[suite_position];
        if (static_cast<int>(suite_position % shard_count) != shard_index) {
            continue;
        }
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
