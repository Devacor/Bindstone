#pragma once

#include <concepts>
#include <source_location>
#include <string_view>
#include <format>
#include <vector>
#include <functional>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <exception>
#include <memory>
#include <algorithm>
#include <sstream>

namespace JaiScript::Testing {

using namespace std::chrono_literals;

// C++20 Concepts for better compile-time checks
template<typename T>
concept Printable = requires(T t) {
    { std::cout << t } -> std::same_as<std::ostream&>;
};

template<typename T>
concept EqualityComparable = requires(T a, T b) {
    { a == b } -> std::convertible_to<bool>;
    { a != b } -> std::convertible_to<bool>;
};

// Test result types
struct TestResult {
    std::string name;
    bool passed;
    std::string error_message;
    std::source_location location;
    std::chrono::nanoseconds duration;
};

struct BenchmarkResult {
    std::string name;
    std::size_t iterations;
    std::chrono::nanoseconds total_time;
    std::chrono::nanoseconds min_time;
    std::chrono::nanoseconds max_time;
    std::chrono::nanoseconds avg_time;
};

// Forward declarations
class TestRegistry;
class TestReporter;

// Test case interface
class ITestCase {
public:
    virtual ~ITestCase() = default;
    virtual TestResult run() = 0;
    virtual std::string_view getName() const = 0;
    virtual std::string_view getSuite() const = 0;
};

// Benchmark case interface
class IBenchmarkCase {
public:
    virtual ~IBenchmarkCase() = default;
    virtual BenchmarkResult run(std::size_t iterations) = 0;
    virtual std::string_view getName() const = 0;
};

// Test Registry (Singleton)
class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }

    void registerTest(std::unique_ptr<ITestCase> test) {
        tests_.push_back(std::move(test));
    }

    void registerBenchmark(std::unique_ptr<IBenchmarkCase> benchmark) {
        benchmarks_.push_back(std::move(benchmark));
    }

    const auto& getTests() const { return tests_; }
    const auto& getBenchmarks() const { return benchmarks_; }

private:
    TestRegistry() = default;
    std::vector<std::unique_ptr<ITestCase>> tests_;
    std::vector<std::unique_ptr<IBenchmarkCase>> benchmarks_;
};

// Test failure exception
class TestFailure : public std::exception {
public:
    TestFailure(std::string message, std::source_location loc = std::source_location::current())
        : message_(std::move(message)), location_(loc) {}
    
    const char* what() const noexcept override { return message_.c_str(); }
    const std::source_location& where() const noexcept { return location_; }

private:
    std::string message_;
    std::source_location location_;
};

// Assertion utilities
namespace detail {
    template<typename T>
    std::string stringify(const T& value) {
        if constexpr (Printable<T>) {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        } else {
            return "<unprintable>";
        }
    }
}

// Modern C++20 assertion functions (no macros!)
template<typename T, typename U>
    requires EqualityComparable<T> && EqualityComparable<U>
void expect_eq(const T& expected, const U& actual, 
               std::source_location loc = std::source_location::current()) {
    if (!(expected == actual)) {
        throw TestFailure(
            std::format("Expected: {}, Actual: {}", 
                detail::stringify(expected), 
                detail::stringify(actual)), 
            loc
        );
    }
}

template<typename T, typename U>
    requires EqualityComparable<T> && EqualityComparable<U>
void expect_ne(const T& expected, const U& actual,
               std::source_location loc = std::source_location::current()) {
    if (expected == actual) {
        throw TestFailure(
            std::format("Expected not equal to: {}", detail::stringify(expected)), 
            loc
        );
    }
}

inline void expect_true(bool condition, std::string_view message = "",
                       std::source_location loc = std::source_location::current()) {
    if (!condition) {
        throw TestFailure(
            message.empty() ? "Expected true" : std::string(message), 
            loc
        );
    }
}

inline void expect_false(bool condition, std::string_view message = "",
                        std::source_location loc = std::source_location::current()) {
    if (condition) {
        throw TestFailure(
            message.empty() ? "Expected false" : std::string(message), 
            loc
        );
    }
}

template<typename Func, typename Exception>
    requires std::invocable<Func>
void expect_throws(Func&& func, std::source_location loc = std::source_location::current()) {
    try {
        func();
        throw TestFailure("Expected exception not thrown", loc);
    } catch (const Exception&) {
        // Expected
    } catch (...) {
        throw TestFailure("Wrong exception type thrown", loc);
    }
}

template<typename T>
    requires std::floating_point<T>
void expect_near(T expected, T actual, T tolerance,
                std::source_location loc = std::source_location::current()) {
    if (std::abs(expected - actual) > tolerance) {
        throw TestFailure(
            std::format("Expected: {} ± {}, Actual: {}", expected, tolerance, actual), 
            loc
        );
    }
}

// Test case implementation
template<typename Func>
class TestCase : public ITestCase {
public:
    TestCase(std::string_view suite, std::string_view name, Func func)
        : suite_(suite), name_(name), func_(std::move(func)) {}

    TestResult run() override {
        auto start = std::chrono::high_resolution_clock::now();
        TestResult result{std::string(name_), true, "", {}, {}};
        
        try {
            func_();
        } catch (const TestFailure& e) {
            result.passed = false;
            result.error_message = e.what();
            result.location = e.where();
        } catch (const std::exception& e) {
            result.passed = false;
            result.error_message = std::format("Unexpected exception: {}", e.what());
        } catch (...) {
            result.passed = false;
            result.error_message = "Unknown exception thrown";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = end - start;
        return result;
    }

    std::string_view getName() const override { return name_; }
    std::string_view getSuite() const override { return suite_; }

private:
    std::string suite_;
    std::string name_;
    Func func_;
};

// Benchmark case implementation
template<typename Func>
class BenchmarkCase : public IBenchmarkCase {
public:
    BenchmarkCase(std::string_view name, Func func)
        : name_(name), func_(std::move(func)) {}

    BenchmarkResult run(std::size_t iterations) override {
        BenchmarkResult result{std::string(name_), iterations, {}, 
                              std::chrono::nanoseconds::max(), {}, {}};
        
        // Warmup
        for (std::size_t i = 0; i < std::min(iterations / 10, std::size_t(100)); ++i) {
            func_();
        }
        
        // Actual benchmark
        auto total_start = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            auto iter_start = std::chrono::high_resolution_clock::now();
            func_();
            auto iter_end = std::chrono::high_resolution_clock::now();
            auto iter_time = iter_end - iter_start;
            
            result.min_time = std::min(result.min_time, iter_time);
            result.max_time = std::max(result.max_time, iter_time);
        }
        auto total_end = std::chrono::high_resolution_clock::now();
        
        result.total_time = total_end - total_start;
        result.avg_time = result.total_time / iterations;
        return result;
    }

    std::string_view getName() const override { return name_; }

private:
    std::string name_;
    Func func_;
};

// Test registration helpers
template<typename Func>
struct TestRegistrar {
    TestRegistrar(std::string_view suite, std::string_view name, Func func) {
        TestRegistry::instance().registerTest(
            std::make_unique<TestCase<Func>>(suite, name, std::move(func))
        );
    }
};

template<typename Func>
struct BenchmarkRegistrar {
    BenchmarkRegistrar(std::string_view name, Func func) {
        TestRegistry::instance().registerBenchmark(
            std::make_unique<BenchmarkCase<Func>>(name, std::move(func))
        );
    }
};

// Test runner
class TestRunner {
public:
    struct Config {
        bool verbose;
        bool run_benchmarks;
        std::size_t benchmark_iterations;
        std::string filter;
        
        Config() : verbose(false), run_benchmarks(false), benchmark_iterations(1000), filter("") {}
    };

    explicit TestRunner(const Config& config = Config()) : config_(config) {}

    int run() {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
        std::cout << "║           JaiScript Test Suite                        ║\n";
        std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

        auto& registry = TestRegistry::instance();
        
        // Run tests
        std::vector<TestResult> results;
        for (const auto& test : registry.getTests()) {
            if (!config_.filter.empty() && 
                std::string(test->getName()).find(config_.filter) == std::string::npos) {
                continue;
            }
            
            if (config_.verbose) {
                std::cout << "Running " << test->getSuite() << "::" << test->getName() << "..." << std::flush;
            }
            
            auto result = test->run();
            results.push_back(result);
            
            if (config_.verbose) {
                if (result.passed) {
                    std::cout << " ✓ (" << std::chrono::duration_cast<std::chrono::microseconds>(result.duration).count() << "μs)\n";
                } else {
                    std::cout << " ✗\n";
                    std::cout << "  Error: " << result.error_message << "\n";
                    std::cout << "  Location: " << result.location.file_name() << ":" 
                             << result.location.line() << "\n";
                }
            } else {
                std::cout << (result.passed ? "." : "F") << std::flush;
            }
        }
        
        if (!config_.verbose) std::cout << "\n\n";
        
        // Summary
        auto passed = std::count_if(results.begin(), results.end(), 
                                   [](const auto& r) { return r.passed; });
        auto failed = results.size() - passed;
        
        std::cout << "Tests: " << passed << " passed, " << failed << " failed, " 
                  << results.size() << " total\n";
        
        // Run benchmarks if requested
        if (config_.run_benchmarks) {
            runBenchmarks();
        }
        
        return failed > 0 ? 1 : 0;
    }

private:
    void runBenchmarks() {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
        std::cout << "║           JaiScript Benchmarks                        ║\n";
        std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";
        
        auto& registry = TestRegistry::instance();
        
        for (const auto& benchmark : registry.getBenchmarks()) {
            std::cout << "Running " << benchmark->getName() << "...\n";
            auto result = benchmark->run(config_.benchmark_iterations);
            
            std::cout << "  Iterations: " << result.iterations << "\n";
            std::cout << "  Total time: " << std::chrono::duration_cast<std::chrono::milliseconds>(result.total_time).count() << "ms\n";
            std::cout << "  Average:    " << std::chrono::duration_cast<std::chrono::microseconds>(result.avg_time).count() << "μs\n";
            std::cout << "  Min:        " << std::chrono::duration_cast<std::chrono::microseconds>(result.min_time).count() << "μs\n";
            std::cout << "  Max:        " << std::chrono::duration_cast<std::chrono::microseconds>(result.max_time).count() << "μs\n\n";
        }
    }
    
    Config config_;
};

// Test definition macros (minimal, just for registration)
#define JAI_TEST_SUITE(suite) \
    namespace suite##_tests { \
        static constexpr std::string_view suite_name = #suite;

#define JAI_TEST(name) \
    static void test_##name(); \
    static ::JaiScript::Testing::TestRegistrar test_##name##_reg{ \
        suite_name, #name, test_##name \
    }; \
    static void test_##name()

#define JAI_BENCHMARK(name) \
    static void benchmark_##name(); \
    static ::JaiScript::Testing::BenchmarkRegistrar benchmark_##name##_reg{ \
        #name, benchmark_##name \
    }; \
    static void benchmark_##name()

#define JAI_TEST_SUITE_END() }

// Main function helper
#define JAI_TEST_MAIN() \
int main(int argc, char** argv) { \
    JaiScript::Testing::TestRunner::Config config; \
    for (int i = 1; i < argc; ++i) { \
        std::string_view arg = argv[i]; \
        if (arg == "--verbose" || arg == "-v") config.verbose = true; \
        else if (arg == "--benchmark" || arg == "-b") config.run_benchmarks = true; \
        else if (arg == "--filter" && i + 1 < argc) config.filter = argv[++i]; \
        else if (arg == "--iterations" && i + 1 < argc) \
            config.benchmark_iterations = std::stoul(argv[++i]); \
    } \
    JaiScript::Testing::TestRunner runner(config); \
    return runner.run(); \
}

} // namespace JaiScript::Testing