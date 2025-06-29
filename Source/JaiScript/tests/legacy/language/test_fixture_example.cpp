#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

// Example of a test fixture with member variables and constructor initialization
JAI_TEST_FIXTURE(EngineFixture) {
public:
    // Constructor-based initialization
    EngineFixture() : test_value_(42), engine_ready_(false) {
        // Initialize engine with some standard functions
        engine_.add_function("add", [](script_int a, script_int b) { return a + b; });
        engine_.add_function("mul", [](script_int a, script_int b) { return a * b; });
        
        // Set up test data
        test_data_ = {"hello", "world", "test"};
        engine_ready_ = true;
    }
    
    // Optional: Per-test setup
    void SetUp() override {
        // Reset any state that needs to be fresh for each test
        counter_ = 0;
    }
    
    // Member variables accessible to all tests
    engine engine_;
    int test_value_;
    bool engine_ready_;
    std::vector<std::string> test_data_;
    mutable int counter_ = 0;
    
    // Helper methods can also be defined
    script_value execute_and_count(const std::string& code) {
        counter_++;
        return engine_.execute(code);
    }
};

// Tests using the fixture - 'self' parameter provides access to fixture members
JAI_TEST_F(EngineFixture, basic_arithmetic) {
    // Access member variables through 'self' parameter
    expect_true(self.engine_ready_);
    expect_eq(self.test_value_, 42);
    
    // Use the engine member
    auto result = self.engine_.execute("2 + 3");
    expect_eq(result.as<script_int>(), 5);
}

JAI_TEST_F(EngineFixture, use_registered_functions) {
    // Test the functions we registered in constructor
    auto result1 = self.engine_.execute("add(10, 20)");
    expect_eq(result1.as<script_int>(), 30);
    
    auto result2 = self.engine_.execute("mul(5, 6)");
    expect_eq(result2.as<script_int>(), 30);
}

JAI_TEST_F(EngineFixture, test_helper_method) {
    // Use helper method that modifies state
    auto result = self.execute_and_count("100 - 50");
    expect_eq(result.as<script_int>(), 50);
    expect_eq(self.counter_, 1);
    
    self.execute_and_count("1 + 1");
    expect_eq(self.counter_, 2);
}

JAI_TEST_F(EngineFixture, access_test_data) {
    // Use the test data vector
    expect_eq(self.test_data_.size(), 3);
    expect_eq(self.test_data_[0], "hello");
    
    // Modify it for this test (each test gets fresh fixture)
    self.test_data_.push_back("new");
    expect_eq(self.test_data_.size(), 4);
}

// Shared state fixture example (more advanced)
JAI_TEST_FIXTURE(SharedStateFixture) {
private:
    // Static members for suite-wide shared state
    static inline engine* shared_engine_ = nullptr;
    static inline int suite_counter_ = 0;
    
public:
    SharedStateFixture() {
        // Lazy initialize shared state
        if (!shared_engine_) {
            shared_engine_ = new engine();
            shared_engine_->add_function("sqrt", [](script_float x) { 
                return std::sqrt(x); 
            });
        }
        suite_counter_++;
    }
    
    ~SharedStateFixture() {
        // Note: Don't delete shared_engine_ here as other tests might use it
    }
    
    engine& get_shared_engine() { return *shared_engine_; }
    int get_suite_counter() const { return suite_counter_; }
};

JAI_TEST_F(SharedStateFixture, shared_engine_test1) {
    auto result = self.get_shared_engine().execute("sqrt(16.0)");
    expect_near(result.as<script_float>(), 4.0, 0.001);
    
    // This counter increases for each test
    expect_true(self.get_suite_counter() > 0);
}

JAI_TEST_F(SharedStateFixture, shared_engine_test2) {
    // Same engine instance as test1
    auto result = self.get_shared_engine().execute("sqrt(25.0)");
    expect_near(result.as<script_float>(), 5.0, 0.001);
    
    // Counter has increased
    expect_true(self.get_suite_counter() > 1);
}

// Example showing how both styles can coexist
JAI_TEST_SUITE(MixedStyleTests)

// Traditional function-based test
JAI_TEST(traditional_test) {
    engine engine;
    auto result = engine.execute("10 * 10");
    expect_eq(result.as<script_int>(), 100);
}

// Another fixture in the same file
JAI_TEST_FIXTURE(SimpleFixture) {
public:
    int value_ = 123;
};

JAI_TEST_F(SimpleFixture, check_value) {
    expect_eq(self.value_, 123);
    self.value_ = 456;
    expect_eq(self.value_, 456);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()