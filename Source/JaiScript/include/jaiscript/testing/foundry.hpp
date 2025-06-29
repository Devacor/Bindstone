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
    void benchmark(const std::string& name, std::function<void()> bench_func) {
        benchmarks_.emplace_back(name, bench_func);
    }
    
    // Get the name of this test suite
    const std::string& get_name() const { return suite_name_; }
    
    // Run all tests and benchmarks
    int quench() {
        std::cout << "\n╔══ " << suite_name_ << " ══╗\n";
        
        // Let derived class register its tests
        forge_tests();
        
        int passed = 0;
        int failed = 0;
        
        // Run tests
        for (const auto& [name, func] : tests_) {
            std::cout << "  " << name << " ... " << std::flush;
            
            auto start = std::chrono::steady_clock::now();
            
            try {
                func();
                
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                
                std::cout << "✓ (" << duration.count() << "μs)\n";
                passed++;
            } catch (const std::exception& e) {
                std::cout << "✗\n";
                std::cout << "    Error: " << e.what() << "\n";
                failed++;
            }
        }
        
        // Run benchmarks if any
        if (!benchmarks_.empty()) {
            std::cout << "\n  Benchmarks:\n";
            for (const auto& [name, func] : benchmarks_) {
                run_benchmark(name, func);
            }
        }
        
        std::cout << "\n  Summary: " << passed << " passed";
        if (failed > 0) {
            std::cout << ", " << failed << " failed";
        }
        std::cout << "\n";
        
        return failed;
    }
    
private:
    void run_benchmark(const std::string& name, std::function<void()> func) {
        constexpr int iterations = 1000;
        
        // Setup for benchmark
        pre_test();
        
        try {
            // Warmup
            for (int i = 0; i < 100; ++i) {
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
        } catch (...) {
            post_test();
            throw;
        }
    }
    
    std::string suite_name_;
    std::vector<std::pair<std::string, std::function<void()>>> tests_;
    std::vector<std::pair<std::string, std::function<void()>>> benchmarks_;
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
        throw test_failure(std::format("Expected: {} ± {}, Actual: {}", expected, tolerance, actual));
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