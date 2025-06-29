#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <cmath>
#include <memory>

using namespace jai;
using namespace jai::test;

// Clean approach: Just inherit from a base test class and override run_tests()
class base_test_suite {
public:
    base_test_suite(const std::string& suite_name) : suite_name_(suite_name) {}
    virtual ~base_test_suite() = default;
    
    // Override this to define your tests
    virtual void run_tests() = 0;
    
    // Helper to run a single test
    void test(const std::string& test_name, std::function<void()> test_func) {
        std::cout << "Running " << suite_name_ << "::" << test_name << "..." << std::flush;
        
        auto start = std::chrono::high_resolution_clock::now();
        bool passed = true;
        std::string error_message;
        
        try {
            set_up();
            test_func();
            tear_down();
        } catch (const test_failure& e) {
            passed = false;
            error_message = e.what();
        } catch (const std::exception& e) {
            passed = false;
            error_message = std::format("Unexpected exception: {}", e.what());
        } catch (...) {
            passed = false;
            error_message = "Unknown exception thrown";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (passed) {
            std::cout << " ✓ (" << duration.count() << "μs)\n";
            passed_++;
        } else {
            std::cout << " ✗\n";
            std::cout << "  Error: " << error_message << "\n";
            failed_++;
        }
        
        total_++;
    }
    
    // Override for custom setup/teardown
    virtual void set_up() {}
    virtual void tear_down() {}
    
    // Run the suite and return number of failures
    int run() {
        std::cout << "\n--- " << suite_name_ << " ---\n";
        passed_ = failed_ = total_ = 0;
        
        run_tests();
        
        std::cout << "\nSummary: " << passed_ << " passed, " << failed_ << " failed, " 
                  << total_ << " total\n";
        
        return failed_;
    }
    
protected:
    std::string suite_name_;
    int passed_ = 0;
    int failed_ = 0;
    int total_ = 0;
};

// Example test suite - clean and simple!
class engine_tests : public base_test_suite {
public:
    engine_tests() : base_test_suite("EngineTests") {
        // Initialize engine with functions
        engine_.add_function("add", [](script_int a, script_int b) { return a + b; });
        engine_.add_function("mul", [](script_int a, script_int b) { return a * b; });
        engine_.add_function("sqrt", [](script_float x) { return std::sqrt(x); });
    }
    
    void set_up() override {
        // Reset state before each test
        test_counter_ = 0;
    }
    
    void run_tests() override {
        test("basic_arithmetic", [this]() {
            auto result = engine_.execute("2 + 3");
            expect_eq(result.as<script_int>(), 5);
            
            test_counter_++;
            expect_eq(test_counter_, 1);
        });
        
        test("registered_functions", [this]() {
            auto result1 = engine_.execute("add(10, 20)");
            expect_eq(result1.as<script_int>(), 30);
            
            auto result2 = engine_.execute("mul(5, 6)");
            expect_eq(result2.as<script_int>(), 30);
            
            auto result3 = engine_.execute("sqrt(16.0)");
            expect_near(result3.as<script_float>(), 4.0, 0.001);
        });
        
        test("variables", [this]() {
            engine_.execute("var x = 42;");
            auto result = engine_.execute("x");
            expect_eq(result.as<script_int>(), 42);
            
            engine_.execute("x = x + 8;");
            result = engine_.execute("x");
            expect_eq(result.as<script_int>(), 50);
        });
        
        test("arrays", [this]() {
            auto result = engine_.execute("[1, 2, 3, 4, 5]");
            expect_true(result.is_array());
            expect_eq(result.as_array().size(), 5);
            
            engine_.execute("var arr = [10, 20, 30];");
            auto size_result = engine_.execute("arr.size()");
            expect_eq(size_result.as<script_int>(), 3);
        });
    }
    
private:
    engine engine_;
    int test_counter_ = 0;
};

// Another test suite
class control_flow_tests : public base_test_suite {
public:
    control_flow_tests() : base_test_suite("ControlFlowTests") {}
    
    void run_tests() override {
        test("if_statement", [this]() {
            engine engine;
            auto result = engine.execute(R"(
                var x = 10;
                if (x > 5) {
                    x = x * 2;
                }
                x
            )");
            expect_eq(result.as<script_int>(), 20);
        });
        
        test("while_loop", [this]() {
            engine engine;
            auto result = engine.execute(R"(
                var sum = 0;
                var i = 1;
                while (i <= 5) {
                    sum = sum + i;
                    i = i + 1;
                }
                sum
            )");
            expect_eq(result.as<script_int>(), 15);
        });
        
        test("for_loop", [this]() {
            engine engine;
            auto result = engine.execute(R"(
                var sum = 0;
                for (var i = 1; i <= 5; i = i + 1) {
                    sum = sum + i;
                }
                sum
            )");
            expect_eq(result.as<script_int>(), 15);
        });
    }
};

// Simple main function - no macros needed!
int main(int argc, char** argv) {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║           JaiScript Test Suite (No Macros!)           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    
    // Create and run test suites
    std::vector<std::unique_ptr<base_test_suite>> suites;
    suites.push_back(std::make_unique<engine_tests>());
    suites.push_back(std::make_unique<control_flow_tests>());
    
    int total_failures = 0;
    
    for (auto& suite : suites) {
        total_failures += suite->run();
    }
    
    std::cout << "\n══════════════════════════════════════════════════════════\n";
    std::cout << "Total failures: " << total_failures << "\n";
    
    return total_failures > 0 ? 1 : 0;
}