#pragma once

#include <iostream>
#include <functional>
#include <vector>
#include <memory>
#include <chrono>
#include <string>
#include <format>
#include <exception>
#include <type_traits>
#include <cmath>

namespace jai::foundry {

// Test failure exception
class test_failure : public std::exception {
public:
    test_failure(const std::string& msg) : message_(msg) {}
    const char* what() const noexcept override { return message_.c_str(); }
private:
    std::string message_;
};

// Base test suite class
class suite {
public:
    suite(const std::string& name) : suite_name_(name) {}
    virtual ~suite() = default;
    
    // Override to define your tests
    virtual void forge_tests() = 0;
    
    // Optional: Override for per-test setup/teardown
    virtual void pre_test() {}
    virtual void post_test() {}
    
    // Add a test
    void test(const std::string& name, std::function<void()> test_func) {
        tests_.emplace_back(name, [this, test_func]() {
            pre_test();
            try {
                test_func();
                post_test();
            } catch (...) {
                post_test();  // Ensure cleanup on failure
                throw;
            }
        });
    }
    
    // Add a benchmark
    void benchmark(const std::string& name, std::function<void()> bench_func, int32_t iterations = 1000) {
        benchmarks_.emplace_back(name, bench_func, iterations);
    }
    
    // Get the name of this test suite
    const std::string& get_name() const { return suite_name_; }
    
    // Simple wildcard matching (* matches any sequence, no * does substring)
    static bool matches_filter(const std::string& pattern, const std::string& text) {
        if (pattern.empty() || pattern == "*") return true;

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

    // Run all tests and benchmarks (with optional test name filter)
    int quench(const std::string& test_filter = "") {
        std::cout << "\n╔══ " << suite_name_ << " ══╗\n";

        // Let derived class register its tests
        forge_tests();

        int passed = 0;
        int failed = 0;
        int skipped = 0;

        // Run tests
        for (const auto& [name, func] : tests_) {
            // Skip if filter doesn't match
            if (!test_filter.empty() && !matches_filter(test_filter, name)) {
                skipped++;
                continue;
            }

            std::cout << "  " << name << " ... " << std::flush;

            auto start = std::chrono::steady_clock::now();

            try {
                // Call pre_test before each test
                pre_test();

                func();

                // Call post_test after each test
                post_test();

                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

                std::cout << "<3 (" << duration.count() << "us)\n";
                passed++;
            } catch (const std::exception& e) {
                post_test();  // Clean up even on error
                std::cout << "x\n";
                std::cout << "    Error: " << e.what() << "\n";
                failed++;
            }
        }

        // Run benchmarks if any (and no filter is active or filter matches)
        if (!benchmarks_.empty() && (test_filter.empty() || test_filter == "*")) {
            std::cout << "\n  Benchmarks:\n";
            for (const auto& [name, func, iterations] : benchmarks_) {
                run_benchmark(name, func, iterations);
            }
        }

        std::cout << "\n  Summary: " << passed << " passed";
        if (failed > 0) {
            std::cout << ", " << failed << " failed";
        }
        if (skipped > 0) {
            std::cout << " (" << skipped << " skipped)";
        }
        std::cout << "\n";

        return failed;
    }
    
private:
    void run_benchmark(const std::string& name, std::function<void()> func, int32_t iterations = 1000) {
        
        // Setup for benchmark
        pre_test();
        
        try {
            // Warmup
            for (int i = 0; i < 10; ++i) {
                func();
            }
            
            // Measure
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < iterations; ++i) {
                func();
            }
            auto end = std::chrono::steady_clock::now();
            
            auto total = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            auto per_iteration = total.count() / iterations;
            
            std::cout << "    " << name << ": " << per_iteration << "μs/iteration\n";

            post_test();
        } catch (const std::exception& e) {
            post_test();
            std::cerr << "\n*** BENCHMARK ERROR in \"" << name << "\": " << e.what() << std::endl;
            throw;
        } catch (...) {
            post_test();
            std::cerr << "\n*** BENCHMARK ERROR in \"" << name << "\": Unknown exception" << std::endl;
            throw;
        }
    }
    
    std::string suite_name_;
    std::vector<std::pair<std::string, std::function<void()>>> tests_;
    std::vector<std::tuple<std::string, std::function<void()>, int32_t>> benchmarks_;
};

// Simple assertion helpers
template<typename T, typename U>
void check_eq(const T& expected, const U& actual, const std::string& message = "") {
    if (!(expected == actual)) {
        if (message.empty()) {
            throw test_failure("Values do not match");
        } else {
            throw test_failure(message);
        }
    }
}

inline void check(bool condition, const std::string& message = "Check failed") {
    if (!condition) {
        throw test_failure(message);
    }
}

// Convenience aliases for common patterns
template<typename T>
void check(const T& expected, const T& actual) {
    check_eq(expected, actual);
}

template<typename T>
void check_near(T expected, T actual, T tolerance) {
    if (std::abs(expected - actual) > tolerance) {
        std::stringstream ss;
        ss << "Expected: " << expected << " ± " << tolerance << ", Actual: " << actual;
        throw test_failure(ss.str());
    }
}

template<typename Func>
void check_throws(Func&& func, const std::string& message = "Expected exception") {
    try {
        func();
        throw test_failure(message);
    } catch (const test_failure&) {
        throw;  // Re-throw test failures
    } catch (...) {
        // Expected exception caught
    }
}

template<typename Exception, typename Func>
void check_throws(Func&& func, const std::string& message = "Expected specific exception") {
    try {
        func();
        throw test_failure(message);
    } catch (const test_failure&) {
        throw;  // Re-throw test failures
    } catch (const Exception&) {
        // Expected exception caught
    } catch (...) {
        throw test_failure("Caught unexpected exception type");
    }
}

// Additional check functions
inline void check_true(bool condition, const std::string& message = "Expected true") {
    if (!condition) {
        throw test_failure(message);
    }
}

inline void check_false(bool condition, const std::string& message = "Expected false") {
    if (condition) {
        throw test_failure(message);
    }
}

template<typename T>
void check_null(const T& ptr, const std::string& message = "Expected null") {
    if (ptr != nullptr) {
        throw test_failure(message);
    }
}

template<typename T>
void check_not_null(const T& ptr, const std::string& message = "Expected non-null") {
    if (ptr == nullptr) {
        throw test_failure(message);
    }
}

template<typename T, typename U>
void check_ne(const T& expected, const U& actual, const std::string& message = "") {
    if (expected == actual) {
        if (message.empty()) {
            throw test_failure("Values should not match");
        } else {
            throw test_failure(message);
        }
    }
}

template<typename T, typename U>
void check_gt(const T& value, const U& threshold, const std::string& message = "") {
    if (!(value > threshold)) {
        if (message.empty()) {
            throw test_failure("Value not greater than threshold");
        } else {
            throw test_failure(message);
        }
    }
}

template<typename T, typename U>
void check_lt(const T& value, const U& threshold, const std::string& message = "") {
    if (!(value < threshold)) {
        if (message.empty()) {
            throw test_failure("Value not less than threshold");
        } else {
            throw test_failure(message);
        }
    }
}

template<typename T, typename U>
void check_ge(const T& value, const U& threshold, const std::string& message = "") {
    if (!(value >= threshold)) {
        if (message.empty()) {
            throw test_failure("Value not greater than or equal to threshold");
        } else {
            throw test_failure(message);
        }
    }
}

template<typename T, typename U>
void check_le(const T& value, const U& threshold, const std::string& message = "") {
    if (!(value <= threshold)) {
        if (message.empty()) {
            throw test_failure("Value not less than or equal to threshold");
        } else {
            throw test_failure(message);
        }
    }
}

// Conditional isolated test support
// When JAI_ISOLATED_TEST is defined, this creates a main() function
// Otherwise, it does nothing, allowing the test to be part of a suite
#ifdef JAI_ISOLATED_TEST
    #define CONDITIONAL_ISOLATED_TEST(test_class) \
        int main(int argc, char** argv) { \
            std::cout << "Running isolated test: " #test_class "\n"; \
            std::cout << "=====================================\n"; \
            test_class test_instance; \
            int result = test_instance.quench(); \
            std::cout << "=====================================\n"; \
            return result; \
        }
#else
    #define CONDITIONAL_ISOLATED_TEST(test_class) \
        /* No-op when not building in isolation */
#endif

// Convenience macro to register a test suite
#define REGISTER_FOUNDRY_TESTS(suite_class) \
    std::unique_ptr<::jai::foundry::suite> create_##suite_class() { \
        return std::make_unique<suite_class>(); \
    }

} // namespace jai::foundry

// Include auto-registration functionality
#include "auto_register.hpp"