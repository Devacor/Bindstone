#include <iostream>
#include <functional>
#include <vector>
#include <memory>
#include <chrono>
#include <string>
#include <stdexcept>

// Simple test framework without macros - just clean C++ classes

class test_exception : public std::exception {
public:
    test_exception(const std::string& msg) : message(msg) {}
    const char* what() const noexcept override { return message.c_str(); }
private:
    std::string message;
};

// Simple assertion functions
template<typename T>
void assert_eq(const T& expected, const T& actual) {
    if (expected != actual) {
        throw test_exception("Expected: " + std::to_string(expected) + 
                           ", Actual: " + std::to_string(actual));
    }
}

void assert_true(bool condition, const std::string& msg = "Expected true") {
    if (!condition) {
        throw test_exception(msg);
    }
}

// Base test suite class
class test_suite {
public:
    test_suite(const std::string& name) : suite_name_(name) {}
    virtual ~test_suite() = default;
    
    // Override this to add your tests
    virtual void register_tests() = 0;
    
    // Called before/after each test
    virtual void set_up() {}
    virtual void tear_down() {}
    
    // Add a test
    void add_test(const std::string& name, std::function<void()> test_func) {
        tests_.push_back({name, test_func});
    }
    
    // Run all tests
    int run() {
        std::cout << "\n=== " << suite_name_ << " ===\n";
        
        register_tests();  // Let derived class register its tests
        
        int passed = 0;
        int failed = 0;
        
        for (const auto& [name, func] : tests_) {
            std::cout << "  " << name << "... " << std::flush;
            
            auto start = std::chrono::steady_clock::now();
            
            try {
                set_up();
                func();
                tear_down();
                
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                
                std::cout << "PASS (" << duration.count() << "μs)\n";
                passed++;
            } catch (const std::exception& e) {
                std::cout << "FAIL: " << e.what() << "\n";
                failed++;
                tear_down();  // Ensure cleanup even on failure
            }
        }
        
        std::cout << "\nSummary: " << passed << " passed, " << failed << " failed\n";
        return failed;
    }
    
private:
    std::string suite_name_;
    std::vector<std::pair<std::string, std::function<void()>>> tests_;
};

// Example calculator class to test
class Calculator {
public:
    int add(int a, int b) { return a + b; }
    int multiply(int a, int b) { return a * b; }
    double divide(double a, double b) { 
        if (b == 0) throw std::runtime_error("Division by zero");
        return a / b; 
    }
    
    void set_memory(int value) { memory_ = value; }
    int get_memory() const { return memory_; }
    
private:
    int memory_ = 0;
};

// Test suite for Calculator - no macros!
class calculator_tests : public test_suite {
public:
    calculator_tests() : test_suite("Calculator Tests") {}
    
    void set_up() override {
        // Fresh calculator for each test
        calc_ = std::make_unique<Calculator>();
    }
    
    void register_tests() override {
        // Register all test methods
        add_test("addition", [this]() { test_addition(); });
        add_test("multiplication", [this]() { test_multiplication(); });
        add_test("division", [this]() { test_division(); });
        add_test("memory", [this]() { test_memory(); });
        add_test("state_isolation", [this]() { test_state_isolation(); });
    }
    
private:
    std::unique_ptr<Calculator> calc_;
    
    void test_addition() {
        assert_eq(5, calc_->add(2, 3));
        assert_eq(0, calc_->add(-5, 5));
        assert_eq(-10, calc_->add(-5, -5));
    }
    
    void test_multiplication() {
        assert_eq(6, calc_->multiply(2, 3));
        assert_eq(0, calc_->multiply(5, 0));
        assert_eq(-15, calc_->multiply(3, -5));
    }
    
    void test_division() {
        assert_eq(2.0, calc_->divide(6.0, 3.0));
        assert_eq(0.5, calc_->divide(1.0, 2.0));
        
        // Test division by zero
        try {
            calc_->divide(1.0, 0.0);
            throw test_exception("Expected exception for division by zero");
        } catch (const std::runtime_error&) {
            // Expected
        }
    }
    
    void test_memory() {
        assert_eq(0, calc_->get_memory());
        
        calc_->set_memory(42);
        assert_eq(42, calc_->get_memory());
        
        calc_->set_memory(-10);
        assert_eq(-10, calc_->get_memory());
    }
    
    void test_state_isolation() {
        // This test verifies that set_up() gives us a fresh calculator
        assert_eq(0, calc_->get_memory());  // Should be 0, not affected by previous test
    }
};

// Another test suite to demonstrate shared state
class shared_state_tests : public test_suite {
public:
    shared_state_tests() : test_suite("Shared State Tests") {
        // Initialize shared resource once
        shared_data_ = std::make_shared<std::vector<int>>();
    }
    
    void register_tests() override {
        add_test("initial_state", [this]() {
            assert_true(shared_data_->empty());
            shared_data_->push_back(1);
        });
        
        add_test("persistent_state", [this]() {
            // This sees the data from previous test
            assert_eq(1, static_cast<int>(shared_data_->size()));
            assert_eq(1, (*shared_data_)[0]);
            shared_data_->push_back(2);
        });
        
        add_test("accumulated_state", [this]() {
            // This sees all previous modifications
            assert_eq(2, static_cast<int>(shared_data_->size()));
            assert_eq(1, (*shared_data_)[0]);
            assert_eq(2, (*shared_data_)[1]);
        });
    }
    
private:
    std::shared_ptr<std::vector<int>> shared_data_;
};

// Main function - clean and simple
int main() {
    std::cout << "Running Test Suite Demo (No Macros!)\n";
    std::cout << "====================================\n";
    
    // Create test suites
    std::vector<std::unique_ptr<test_suite>> suites;
    suites.push_back(std::make_unique<calculator_tests>());
    suites.push_back(std::make_unique<shared_state_tests>());
    
    // Run all suites
    int total_failures = 0;
    for (auto& suite : suites) {
        total_failures += suite->run();
    }
    
    std::cout << "\n====================================\n";
    std::cout << "Total failures: " << total_failures << "\n";
    
    return total_failures > 0 ? 1 : 0;
}