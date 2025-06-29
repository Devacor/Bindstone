#include "../jai_test_class.hpp"
#include <jaiscript/core/engine.hpp>
#include <cmath>

using namespace jai;
using namespace jai::test;

// Example test class with shared data and initialization
JAI_TEST_CLASS(EngineTestSuite)
private:
    // Member variables accessible to all test methods
    engine engine_;
    int test_counter_;
    std::vector<std::string> test_data_;
    
public:
    // Constructor for one-time initialization
    EngineTestSuite() : test_counter_(0) {
        // Set up the engine with some functions
        engine_.add_function("add", [](script_int a, script_int b) { return a + b; });
        engine_.add_function("mul", [](script_int a, script_int b) { return a * b; });
        engine_.add_function("sqrt", [](script_float x) { return std::sqrt(x); });
        
        // Initialize test data
        test_data_ = {"hello", "world", "test"};
    }
    
    // Called before each test
    void set_up() override {
        // Reset counter for each test
        test_counter_ = 0;
        
        // Could also reset engine state if needed
        engine_.execute("var test_var = 0;");
    }
    
    // Called after each test
    void tear_down() override {
        // Cleanup if needed
    }
    
    // Test methods
    TEST_METHOD(basic_arithmetic) {
        auto result = engine_.execute("2 + 3");
        expect_eq(result.as<script_int>(), 5);
        
        // Can access member variables directly
        test_counter_++;
        expect_eq(test_counter_, 1);
    }
    
    TEST_METHOD(use_registered_functions) {
        auto result1 = engine_.execute("add(10, 20)");
        expect_eq(result1.as<script_int>(), 30);
        
        auto result2 = engine_.execute("mul(5, 6)");
        expect_eq(result2.as<script_int>(), 30);
        
        auto result3 = engine_.execute("sqrt(16.0)");
        expect_near(result3.as<script_float>(), 4.0, 0.001);
    }
    
    TEST_METHOD(access_member_data) {
        expect_eq(test_data_.size(), 3);
        expect_eq(test_data_[0], "hello");
        
        // Modify for this test (gets reset in set_up)
        test_data_.push_back("new");
        expect_eq(test_data_.size(), 4);
    }
    
    TEST_METHOD(state_isolation) {
        // test_counter was reset in set_up()
        expect_eq(test_counter_, 0);
        
        // Modify it
        test_counter_ = 42;
        expect_eq(test_counter_, 42);
        
        // Next test will have it reset to 0 again
    }
    
    // Benchmark method
    BENCHMARK_METHOD(arithmetic_performance) {
        engine_.execute("1 + 2 + 3 + 4 + 5");
    }
    
JAI_TEST_CLASS_END(EngineTestSuite)

// Another test class to show multiple suites
JAI_TEST_CLASS(ArrayTestSuite)
private:
    engine engine_;
    
public:
    ArrayTestSuite() {
        // Nothing special needed
    }
    
    TEST_METHOD(array_creation) {
        auto result = engine_.execute("[1, 2, 3, 4, 5]");
        expect_true(result.is_array());
        expect_eq(result.as_array().size(), 5);
    }
    
    TEST_METHOD(array_methods) {
        engine_.execute("var arr = [10, 20, 30];");
        
        auto size_result = engine_.execute("arr.size()");
        expect_eq(size_result.as<script_int>(), 3);
        
        engine_.execute("arr.push(40);");
        auto new_size = engine_.execute("arr.size()");
        expect_eq(new_size.as<script_int>(), 4);
        
        auto back_result = engine_.execute("arr.back()");
        expect_eq(back_result.as<script_int>(), 40);
    }
    
    TEST_METHOD(array_subscript) {
        engine_.execute("var arr = [100, 200, 300];");
        
        auto first = engine_.execute("arr[0]");
        expect_eq(first.as<script_int>(), 100);
        
        auto second = engine_.execute("arr[1]");
        expect_eq(second.as<script_int>(), 200);
        
        // Modify through subscript
        engine_.execute("arr[1] = 250;");
        auto modified = engine_.execute("arr[1]");
        expect_eq(modified.as<script_int>(), 250);
    }
    
JAI_TEST_CLASS_END(ArrayTestSuite)

// Example of a test class with suite-wide setup/teardown
JAI_TEST_CLASS(SharedResourceTestSuite)
private:
    static inline std::shared_ptr<engine> shared_engine_;
    static inline int suite_run_count_ = 0;
    
public:
    // Called once before all tests in this suite
    void set_up_suite() override {
        shared_engine_ = std::make_shared<engine>();
        shared_engine_->add_function("shared_func", [](script_int x) { return x * 2; });
    }
    
    // Called once after all tests in this suite
    void tear_down_suite() override {
        shared_engine_.reset();
    }
    
    void set_up() override {
        suite_run_count_++;
    }
    
    TEST_METHOD(use_shared_resource) {
        auto result = shared_engine_->execute("shared_func(21)");
        expect_eq(result.as<script_int>(), 42);
        
        // This counter increases with each test
        expect_true(suite_run_count_ > 0);
    }
    
    TEST_METHOD(shared_state_persists) {
        // Define a variable in the shared engine
        shared_engine_->execute("var persistent_var = 123;");
        
        auto result = shared_engine_->execute("persistent_var");
        expect_eq(result.as<script_int>(), 123);
        
        // This will be visible to other tests using shared_engine_
    }
    
JAI_TEST_CLASS_END(SharedResourceTestSuite)

// Traditional function-based tests can still coexist
JAI_TEST_SUITE(TraditionalTests)

JAI_TEST(simple_function_test) {
    engine engine;
    auto result = engine.execute("10 * 10");
    expect_eq(result.as<script_int>(), 100);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()