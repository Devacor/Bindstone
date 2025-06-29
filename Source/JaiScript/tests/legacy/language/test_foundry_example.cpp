#include "../jai_foundry_test.hpp"
#include <vector>
#include <string>
#include <memory>

using namespace jai::foundry;

// Example 1: Simple tests (no per-test setup needed)
class basic_math_tests : public suite {
public:
    basic_math_tests() : suite("Basic Math") {}
    
    void forge_tests() override {
        test("addition", [this]() {
            check(2 + 2, 4);
            check(10 + (-5), 5);
            check(0 + 0, 0);
        });
        
        test("multiplication", [this]() {
            check(3 * 4, 12);
            check(5 * 0, 0);
            check(-3 * 2, -6);
        });
        
        test("division", [this]() {
            check(10.0 / 2.0, 5.0);
            check_near(7.0 / 3.0, 2.333, 0.001);
            
            check_throws([]() {
                if (0 == 0) throw std::runtime_error("Division by zero");
            });
        });
        
        benchmark("arithmetic_speed", [this]() {
            volatile int result = 1 + 2 + 3 + 4 + 5;
            (void)result;  // Suppress unused warning
        });
    }
};

// Example 2: With per-test setup/teardown
class vector_tests : public suite {
public:
    vector_tests() : suite("Vector Operations") {}
    
    void pre_test(const std::string& test_name) override {
        // Fresh vector for each test
        vec_ = std::make_unique<std::vector<int>>();
        counter_ = 0;
        
        // Could customize based on test name
        if (test_name == "large_vector_test") {
            vec_->reserve(10000);  // Pre-allocate for performance test
        }
    }
    
    void post_test(const std::string& test_name) override {
        // Cleanup after each test
        vec_.reset();
        
        // Could log or validate based on test
        // std::cout << "  [" << test_name << " used " << counter_ << " operations]\n";
    }
    
    void forge_tests() override {
        test("empty_vector", [this]() {
            check(vec_->empty());
            check_eq(vec_->size(), 0u);
        });
        
        test("push_elements", [this]() {
            vec_->push_back(10);
            vec_->push_back(20);
            vec_->push_back(30);
            
            check_eq(vec_->size(), 3u);  // Use check_eq with unsigned literal
            check((*vec_)[0], 10);
            check((*vec_)[2], 30);
        });
        
        test("state_isolation", [this]() {
            // This test verifies pre_test() gives us clean state
            check(vec_->empty());  // Should be empty despite previous test
            check(counter_, 0);    // Should be reset
            
            counter_ = 99;
            vec_->push_back(123);
        });
        
        test("verify_isolation", [this]() {
            // Verify the previous test didn't affect this one
            check_eq(counter_, 0);
            check(vec_->empty());
        });
        
        benchmark("vector_pushback", [this]() {
            vec_->push_back(42);
        });
    }
    
private:
    std::unique_ptr<std::vector<int>> vec_;
    int counter_ = 0;
};

// Example 3: Shared state across tests (no pre_test/post_test)
class accumulator_tests : public suite {
public:
    accumulator_tests() : suite("Accumulator") {
        // Initialize shared state in constructor
        shared_data_ = std::make_shared<std::vector<std::string>>();
    }
    
    void forge_tests() override {
        test("initial_state", [this]() {
            check(shared_data_->empty());
            shared_data_->push_back("first");
        });
        
        test("accumulate_data", [this]() {
            // Sees data from previous test
            check_eq(shared_data_->size(), 1u);
            check((*shared_data_)[0], std::string("first"));
            shared_data_->push_back("second");
        });
        
        test("final_state", [this]() {
            // Sees all accumulated data
            check_eq(shared_data_->size(), 2u);
            check((*shared_data_)[1], std::string("second"));
        });
    }
    
private:
    std::shared_ptr<std::vector<std::string>> shared_data_;
};

// Example 4: Exception handling
class error_tests : public suite {
public:
    error_tests() : suite("Error Handling") {}
    
    void forge_tests() override {
        test("exception_handling", [this]() {
            check_throws([]() {
                throw std::runtime_error("Test exception");
            });
        });
        
        test("condition_checks", [this]() {
            check(true, "This should pass");
            check(1 + 1, 2);  // Use numeric check instead
        });
        
        test("floating_point", [this]() {
            check_near(0.1 + 0.2, 0.3, 0.0001);
        });
    }
};

// Simple runner - no macros needed!
int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║        JaiScript Foundry Tests         ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    // Create test suites
    std::vector<std::unique_ptr<suite>> test_suites;
    test_suites.push_back(std::make_unique<basic_math_tests>());
    test_suites.push_back(std::make_unique<vector_tests>());
    test_suites.push_back(std::make_unique<accumulator_tests>());
    test_suites.push_back(std::make_unique<error_tests>());
    
    // Run all suites
    int total_failures = 0;
    for (auto& test_suite : test_suites) {
        total_failures += test_suite->forge();
    }
    
    std::cout << "\n╔════════════════════════════════════════╗\n";
    if (total_failures == 0) {
        std::cout << "║            All Tests Passed!          ║\n";
    } else {
        std::cout << "║       " << total_failures << " Test(s) Failed!            ║\n";
    }
    std::cout << "╚════════════════════════════════════════╝\n";
    
    return total_failures > 0 ? 1 : 0;
}