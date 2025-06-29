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

namespace jai::test {

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
struct test_result {
    std::string name;
    bool passed;
    std::string error_message;
    std::source_location location;
    std::chrono::nanoseconds duration;
};

struct benchmark_result {
    std::string name;
    std::size_t iterations;
    std::chrono::nanoseconds total_time;
    std::chrono::nanoseconds min_time;
    std::chrono::nanoseconds max_time;
    std::chrono::nanoseconds avg_time;
};

// Forward declarations
class test_registry;
class test_reporter;

// Test case interface
class test_case_interface {
public:
    virtual ~test_case_interface() = default;
    virtual test_result run() = 0;
    virtual std::string_view get_name() const = 0;
    virtual std::string_view get_suite() const = 0;
};

// Benchmark case interface
class benchmark_case_interface {
public:
    virtual ~benchmark_case_interface() = default;
    virtual benchmark_result run(std::size_t iterations) = 0;
    virtual std::string_view get_name() const = 0;
};

// Test Registry (Singleton)
class test_registry {
public:
    static test_registry& instance() {
        static test_registry registry;
        return registry;
    }

    void register_test(std::unique_ptr<test_case_interface> test) {
        tests_.push_back(std::move(test));
    }

    void register_benchmark(std::unique_ptr<benchmark_case_interface> benchmark) {
        benchmarks_.push_back(std::move(benchmark));
    }

    const auto& get_tests() const { return tests_; }
    const auto& get_benchmarks() const { return benchmarks_; }

private:
    test_registry() = default;
    std::vector<std::unique_ptr<test_case_interface>> tests_;
    std::vector<std::unique_ptr<benchmark_case_interface>> benchmarks_;
};

// Test failure exception
class test_failure : public std::exception {
public:
    test_failure(std::string message, std::source_location loc = std::source_location::current())
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
        throw test_failure(
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
        throw test_failure(
            std::format("Expected not equal to: {}", detail::stringify(expected)), 
            loc
        );
    }
}

inline void expect_true(bool condition, std::string_view message = "",
                       std::source_location loc = std::source_location::current()) {
    if (!condition) {
        throw test_failure(
            message.empty() ? "Expected true" : std::string(message), 
            loc
        );
    }
}

inline void expect_false(bool condition, std::string_view message = "",
                        std::source_location loc = std::source_location::current()) {
    if (condition) {
        throw test_failure(
            message.empty() ? "Expected false" : std::string(message), 
            loc
        );
    }
}

template<typename exception, typename Func>
    requires std::invocable<Func>
void expect_throws(Func&& func, std::source_location loc = std::source_location::current()) {
    try {
        func();
        throw test_failure("Expected exception not thrown", loc);
    } catch (const exception&) {
        // Expected
    } catch (...) {
        throw test_failure("Wrong exception type thrown", loc);
    }
}

template<typename T>
    requires std::floating_point<T>
void expect_near(T expected, T actual, T tolerance,
                std::source_location loc = std::source_location::current()) {
    if (std::abs(expected - actual) > tolerance) {
        throw test_failure(
            std::format("Expected: {} ± {}, Actual: {}", expected, tolerance, actual), 
            loc
        );
    }
}

// Test case implementation
template<typename Func>
class test_case : public test_case_interface {
public:
    test_case(std::string_view suite, std::string_view name, Func func)
        : suite_(suite), name_(name), func_(std::move(func)) {}

    test_result run() override {
        auto start = std::chrono::high_resolution_clock::now();
        test_result result{std::string(name_), true, "", {}, {}};
        
        try {
            func_();
        } catch (const test_failure& e) {
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

    std::string_view get_name() const override { return name_; }
    std::string_view get_suite() const override { return suite_; }

private:
    std::string suite_;
    std::string name_;
    Func func_;
};

// Benchmark case implementation
template<typename Func>
class benchmark_case : public benchmark_case_interface {
public:
    benchmark_case(std::string_view name, Func func)
        : name_(name), func_(std::move(func)) {}

    benchmark_result run(std::size_t iterations) override {
        benchmark_result result{std::string(name_), iterations, {}, 
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

    std::string_view get_name() const override { return name_; }

private:
    std::string name_;
    Func func_;
};

// Test registration helpers
template<typename Func>
struct test_registrar {
    test_registrar(std::string_view suite, std::string_view name, Func func) {
        test_registry::instance().register_test(
            std::make_unique<test_case<Func>>(suite, name, std::move(func))
        );
    }
};

template<typename Func>
struct benchmark_registrar {
    benchmark_registrar(std::string_view name, Func func) {
        test_registry::instance().register_benchmark(
            std::make_unique<benchmark_case<Func>>(name, std::move(func))
        );
    }
};

// Test runner
class test_runner {
public:
    struct test_config {
        bool verbose;
        bool run_benchmarks;
        std::size_t benchmark_iterations;
        std::string filter;
        
        test_config() : verbose(true), run_benchmarks(false), benchmark_iterations(1000), filter("") {}
    };

    explicit test_runner(const test_config& config = test_config()) : config_(config) {}

    int run() {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
        std::cout << "║           JaiScript Test Suite                        ║\n";
        std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

        auto& registry = test_registry::instance();
        
        // Run tests
        std::vector<test_result> results;
        for (const auto& test : registry.get_tests()) {
            if (!config_.filter.empty() && 
                std::string(test->get_name()).find(config_.filter) == std::string::npos) {
                continue;
            }
            
            if (config_.verbose) {
                std::cout << "Running " << test->get_suite() << "::" << test->get_name() << "..." << std::flush;
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
            run_benchmarks();
        }
        
        return failed > 0 ? 1 : 0;
    }

private:
    void run_benchmarks() {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
        std::cout << "║           JaiScript Benchmarks                        ║\n";
        std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";
        
        auto& registry = test_registry::instance();
        
        for (const auto& benchmark : registry.get_benchmarks()) {
            std::cout << "Running " << benchmark->get_name() << "...\n";
            auto result = benchmark->run(config_.benchmark_iterations);
            
            std::cout << "  Iterations: " << result.iterations << "\n";
            std::cout << "  Total time: " << std::chrono::duration_cast<std::chrono::milliseconds>(result.total_time).count() << "ms\n";
            std::cout << "  Average:    " << std::chrono::duration_cast<std::chrono::microseconds>(result.avg_time).count() << "μs\n";
            std::cout << "  Min:        " << std::chrono::duration_cast<std::chrono::microseconds>(result.min_time).count() << "μs\n";
            std::cout << "  Max:        " << std::chrono::duration_cast<std::chrono::microseconds>(result.max_time).count() << "μs\n\n";
        }
    }
    
    test_config config_;
};

// Test initialization helper
struct test_initializer {
    test_initializer(std::function<void()> init) {
        init();
    }
};

// Macro for suite initialization that runs before any tests
#define JAI_INITIALIZE_TEST(code) \
    static ::jai::test::test_initializer suite_init{[]() { code }};

// Base class for test fixtures
class test_fixture {
public:
    test_fixture() = default;
    virtual ~test_fixture() = default;
    
    // Optional setup/teardown methods
    virtual void SetUp() {}
    virtual void TearDown() {}
    
    // Called once before all tests in the fixture
    virtual void SetUpTestSuite() {}
    // Called once after all tests in the fixture
    virtual void TearDownTestSuite() {}
};

// Simplified fixture test wrapper
template<typename FixtureType>
class fixture_test_wrapper : public test_case_interface {
public:
    using TestMethod = std::function<void(test_fixture*)>;
    
    fixture_test_wrapper(std::string_view suite, std::string_view name, TestMethod method)
        : suite_(suite), name_(name), method_(std::move(method)) {}

    test_result run() override {
        auto start = std::chrono::high_resolution_clock::now();
        test_result result{std::string(name_), true, "", {}, {}};
        
        try {
            // Create fresh fixture instance for each test
            FixtureType fixture;
            fixture.SetUp();
            
            // Run test method
            method_(&fixture);
            
            fixture.TearDown();
        } catch (const test_failure& e) {
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

    std::string_view get_name() const override { return name_; }
    std::string_view get_suite() const override { return suite_; }

private:
    std::string suite_;
    std::string name_;
    TestMethod method_;
};

// Test definition macros (minimal, just for registration)
#define JAI_TEST_SUITE(suite) \
    namespace suite##_tests { \
        static constexpr std::string_view suite_name = #suite;

#define JAI_TEST(name) \
    static void test_##name(); \
    static ::jai::test::test_registrar test_##name##_reg{ \
        suite_name, #name, test_##name \
    }; \
    static void test_##name()

#define JAI_BENCHMARK(name) \
    static void benchmark_##name(); \
    static ::jai::test::benchmark_registrar benchmark_##name##_reg{ \
        #name, benchmark_##name \
    }; \
    static void benchmark_##name()

#define JAI_TEST_SUITE_END() }

// Macro for defining a test fixture class
#define JAI_TEST_FIXTURE(fixture_name) \
    class fixture_name : public ::jai::test::test_fixture

// Macro for defining a test using a fixture
#define JAI_TEST_F(fixture_name, test_name) \
    namespace { \
        void fixture_name##_##test_name##_impl(fixture_name& self); \
        struct fixture_name##_##test_name##_registrar { \
            fixture_name##_##test_name##_registrar() { \
                auto test_func = [](test_fixture* base) { \
                    auto* fixture_ptr = static_cast<fixture_name*>(base); \
                    fixture_name##_##test_name##_impl(*fixture_ptr); \
                }; \
                ::jai::test::test_registry::instance().register_test( \
                    std::make_unique<::jai::test::fixture_test_wrapper<fixture_name>>( \
                        #fixture_name, #test_name, test_func \
                    ) \
                ); \
            } \
        } fixture_name##_##test_name##_reg_instance; \
    } \
    namespace { \
        void fixture_name##_##test_name##_impl(fixture_name& self); \
    } \
    void fixture_name##_##test_name##_impl(fixture_name& self)

// Alternative approach: Define test methods directly in the fixture class
#define JAI_FIXTURE_TEST(test_name) \
    void test_##test_name()

// Main function helper
#define JAI_TEST_MAIN() \
int main(int argc, char** argv) { \
    jai::test::test_runner::test_config config; \
    for (int i = 1; i < argc; ++i) { \
        std::string_view arg = argv[i]; \
        if (arg == "--verbose" || arg == "-v") config.verbose = true; \
        else if (arg == "--benchmark" || arg == "-b") config.run_benchmarks = true; \
        else if (arg == "--filter" && i + 1 < argc) config.filter = argv[++i]; \
        else if (arg == "--iterations" && i + 1 < argc) \
            config.benchmark_iterations = std::stoul(argv[++i]); \
    } \
    jai::test::test_runner runner(config); \
    return runner.run(); \
}

} // namespace jai::test