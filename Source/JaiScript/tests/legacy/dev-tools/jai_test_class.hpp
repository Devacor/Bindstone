#pragma once

#include "jai_test.hpp"
#include <memory>
#include <string>
#include <vector>

namespace jai::test {

// Base class for test suites
class test_suite {
public:
    test_suite() = default;
    virtual ~test_suite() = default;
    
    // Override these for custom setup/teardown
    virtual void set_up() {}      // Called before each test
    virtual void tear_down() {}   // Called after each test
    
    // Optional: Suite-wide setup/teardown
    virtual void set_up_suite() {}    // Called once before all tests in suite
    virtual void tear_down_suite() {} // Called once after all tests in suite
    
    // Get the suite name (must be implemented by derived class)
    virtual std::string get_suite_name() const = 0;
    
protected:
    // Test registration happens through macros
    template<typename TestMethod>
    void register_test(const std::string& test_name, TestMethod method) {
        auto wrapper = [this, method](test_suite* suite) {
            suite->set_up();
            try {
                (suite->*method)();
            } catch (...) {
                suite->tear_down();
                throw;
            }
            suite->tear_down();
        };
        
        // Register with the global test registry
        test_registry::instance().register_test(
            std::make_unique<suite_test_wrapper>(
                get_suite_name(), test_name, this, wrapper
            )
        );
    }
    
    template<typename BenchmarkMethod>
    void register_benchmark(const std::string& benchmark_name, BenchmarkMethod method) {
        auto wrapper = [this, method]() {
            (this->*method)();
        };
        
        test_registry::instance().register_benchmark(
            std::make_unique<suite_benchmark_wrapper>(
                benchmark_name, wrapper
            )
        );
    }
    
private:
    // Test wrapper that knows about the suite
    class suite_test_wrapper : public test_case_interface {
    public:
        using TestFunc = std::function<void(test_suite*)>;
        
        suite_test_wrapper(const std::string& suite_name, const std::string& test_name,
                          test_suite* suite, TestFunc func)
            : suite_name_(suite_name), test_name_(test_name), 
              suite_(suite), func_(std::move(func)) {}
        
        test_result run() override {
            auto start = std::chrono::high_resolution_clock::now();
            test_result result{test_name_, true, "", {}, {}};
            
            try {
                func_(suite_);
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
        
        std::string_view get_name() const override { return test_name_; }
        std::string_view get_suite() const override { return suite_name_; }
        
    private:
        std::string suite_name_;
        std::string test_name_;
        test_suite* suite_;
        TestFunc func_;
    };
    
    // Benchmark wrapper
    class suite_benchmark_wrapper : public benchmark_case_interface {
    public:
        suite_benchmark_wrapper(const std::string& name, std::function<void()> func)
            : name_(name), func_(std::move(func)) {}
        
        benchmark_result run(std::size_t iterations) override {
            benchmark_result result{name_, iterations, {}, 
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
        std::function<void()> func_;
    };
};

// Helper to automatically register and manage test suite instances
template<typename SuiteType>
class suite_registrar {
public:
    static SuiteType& get_instance() {
        static SuiteType instance;
        static bool initialized = false;
        if (!initialized) {
            instance.set_up_suite();
            initialized = true;
        }
        return instance;
    }
};

// Macros for defining test suites and tests
#define JAI_TEST_CLASS(class_name) \
    class class_name : public ::jai::test::test_suite { \
    public: \
        std::string get_suite_name() const override { return #class_name; } \
        class_name() { register_all_tests(); } \
    private: \
        void register_all_tests(); \
        static bool suite_registered_; \
        static bool register_suite() { \
            ::jai::test::suite_registrar<class_name>::get_instance(); \
            return true; \
        } \
    public:

#define JAI_TEST_CLASS_END(class_name) \
    }; \
    bool class_name::suite_registered_ = class_name::register_suite();

// Helper macro to get the enclosing class type
#define JAI_TEST_CURRENT_CLASS std::remove_reference_t<decltype(*this)>

// Macro for defining a test method - simpler approach
#define TEST_METHOD(method_name) \
    public: \
        void method_name()

// Macro for defining a benchmark method  
#define BENCHMARK_METHOD(method_name) \
    public: \
        void method_name()

} // namespace jai::test