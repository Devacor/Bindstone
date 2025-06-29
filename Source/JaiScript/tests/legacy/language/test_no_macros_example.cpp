#include "../jai_test_simple.hpp"
#include <jaiscript/core/engine.hpp>
#include <cmath>

using namespace jai;
using namespace jai::test;

// Example test suite - no macros!
class engine_test_suite : public test_suite<engine_test_suite> {
public:
    // Constructor where we register all our tests
    engine_test_suite() : test_suite("EngineTests") {
        // Register test methods
        add_test("basic_arithmetic", &engine_test_suite::test_basic_arithmetic);
        add_test("registered_functions", &engine_test_suite::test_registered_functions);
        add_test("member_data_access", &engine_test_suite::test_member_data_access);
        add_test("state_isolation", &engine_test_suite::test_state_isolation);
        
        // Register benchmarks
        add_benchmark("arithmetic_performance", &engine_test_suite::benchmark_arithmetic);
        
        // Initialize member data
        test_data_ = {"hello", "world", "test"};
        
        // Set up engine with functions
        engine_.add_function("add", [](script_int a, script_int b) { return a + b; });
        engine_.add_function("mul", [](script_int a, script_int b) { return a * b; });
        engine_.add_function("sqrt", [](script_float x) { return std::sqrt(x); });
    }
    
    // Called before each test
    void set_up() override {
        test_counter_ = 0;
        engine_.execute("var test_var = 0;");
    }
    
    // Called after each test
    void tear_down() override {
        // Cleanup if needed
    }
    
    // Test methods
    void test_basic_arithmetic() {
        auto result = engine_.execute("2 + 3");
        expect_eq(result.as<script_int>(), 5);
        
        test_counter_++;
        expect_eq(test_counter_, 1);
    }
    
    void test_registered_functions() {
        auto result1 = engine_.execute("add(10, 20)");
        expect_eq(result1.as<script_int>(), 30);
        
        auto result2 = engine_.execute("mul(5, 6)");
        expect_eq(result2.as<script_int>(), 30);
        
        auto result3 = engine_.execute("sqrt(16.0)");
        expect_near(result3.as<script_float>(), 4.0, 0.001);
    }
    
    void test_member_data_access() {
        expect_eq(test_data_.size(), 3);
        expect_eq(test_data_[0], "hello");
        
        test_data_.push_back("new");
        expect_eq(test_data_.size(), 4);
    }
    
    void test_state_isolation() {
        expect_eq(test_counter_, 0);
        test_counter_ = 42;
        expect_eq(test_counter_, 42);
    }
    
    void benchmark_arithmetic() {
        engine_.execute("1 + 2 + 3 + 4 + 5");
    }
    
private:
    engine engine_;
    int test_counter_ = 0;
    std::vector<std::string> test_data_;
};

// Another test suite
class array_test_suite : public test_suite<array_test_suite> {
public:
    array_test_suite() : test_suite("ArrayTests") {
        add_test("array_creation", &array_test_suite::test_array_creation);
        add_test("array_methods", &array_test_suite::test_array_methods);
        add_test("array_subscript", &array_test_suite::test_array_subscript);
    }
    
    void test_array_creation() {
        auto result = engine_.execute("[1, 2, 3, 4, 5]");
        expect_true(result.is_array());
        expect_eq(result.as_array().size(), 5);
    }
    
    void test_array_methods() {
        engine_.execute("var arr = [10, 20, 30];");
        
        auto size_result = engine_.execute("arr.size()");
        expect_eq(size_result.as<script_int>(), 3);
        
        engine_.execute("arr.push(40);");
        auto new_size = engine_.execute("arr.size()");
        expect_eq(new_size.as<script_int>(), 4);
    }
    
    void test_array_subscript() {
        engine_.execute("var arr = [100, 200, 300];");
        
        auto first = engine_.execute("arr[0]");
        expect_eq(first.as<script_int>(), 100);
        
        engine_.execute("arr[1] = 250;");
        auto modified = engine_.execute("arr[1]");
        expect_eq(modified.as<script_int>(), 250);
    }
    
private:
    engine engine_;
};

// Alternative: Using lambdas for even more flexibility
class lambda_style_tests : public test_suite<lambda_style_tests> {
public:
    lambda_style_tests() : test_suite("LambdaStyleTests") {
        // You can also use lambdas if you prefer
        auto basic_test = [this]() {
            engine engine;
            auto result = engine.execute("100 / 10");
            expect_eq(result.as<script_int>(), 10);
        };
        
        // Would need to update add_test to accept std::function<void()> too
        // For now, we'll use regular methods
        add_test("lambda_test", &lambda_style_tests::test_with_lambda);
    }
    
    void test_with_lambda() {
        engine engine;
        auto result = engine.execute("100 / 10");
        expect_eq(result.as<script_int>(), 10);
    }
};

// Manual registration (no macros!)
namespace {
    // Create suite instances - they auto-register with test_suite_manager
    auto* engine_tests = test_suite_manager::instance().create_suite<engine_test_suite>();
    auto* array_tests = test_suite_manager::instance().create_suite<array_test_suite>();
    auto* lambda_tests = test_suite_manager::instance().create_suite<lambda_style_tests>();
}

// You can still mix with traditional function-based tests
JAI_TEST_SUITE(TraditionalTests)

JAI_TEST(simple_function_test) {
    engine engine;
    auto result = engine.execute("10 * 10");
    expect_eq(result.as<script_int>(), 100);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()