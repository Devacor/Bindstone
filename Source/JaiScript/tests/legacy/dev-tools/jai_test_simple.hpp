#pragma once

#include "jai_test.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace jai::test {

// Base class for test suites - no macros needed!
class test_suite_base {
public:
    test_suite_base(const std::string& suite_name) : suite_name_(suite_name) {}
    virtual ~test_suite_base() = default;
    
    // Override these for custom setup/teardown
    virtual void set_up() {}      // Called before each test
    virtual void tear_down() {}   // Called after each test
    
    // Register a test method
    template<typename T>
    void add_test(const std::string& test_name, void (T::*method)()) {
        auto* derived = static_cast<T*>(this);
        
        auto test_wrapper = std::make_unique<method_test_case<T>>(
            suite_name_, test_name, derived, method
        );
        
        test_registry::instance().register_test(std::move(test_wrapper));
    }
    
    // Register a benchmark method
    template<typename T>
    void add_benchmark(const std::string& benchmark_name, void (T::*method)()) {
        auto* derived = static_cast<T*>(this);
        
        auto benchmark_wrapper = std::make_unique<method_benchmark_case<T>>(
            benchmark_name, derived, method
        );
        
        test_registry::instance().register_benchmark(std::move(benchmark_wrapper));
    }
    
protected:
    std::string suite_name_;
    
private:
    // Test case wrapper for member functions
    template<typename T>
    class method_test_case : public test_case_interface {
    public:
        method_test_case(const std::string& suite, const std::string& name, 
                        T* instance, void (T::*method)())
            : suite_(suite), name_(name), instance_(instance), method_(method) {}
        
        test_result run() override {
            auto start = std::chrono::high_resolution_clock::now();
            test_result result{name_, true, "", {}, {}};
            
            try {
                instance_->set_up();
                (instance_->*method_)();
                instance_->tear_down();
            } catch (const test_failure& e) {
                result.passed = false;
                result.error_message = e.what();
                result.location = e.where();
                instance_->tear_down();
            } catch (const std::exception& e) {
                result.passed = false;
                result.error_message = std::format("Unexpected exception: {}", e.what());
                instance_->tear_down();
            } catch (...) {
                result.passed = false;
                result.error_message = "Unknown exception thrown";
                instance_->tear_down();
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
        T* instance_;
        void (T::*method_)();
    };
    
    // Benchmark case wrapper for member functions
    template<typename T>
    class method_benchmark_case : public benchmark_case_interface {
    public:
        method_benchmark_case(const std::string& name, T* instance, void (T::*method)())
            : name_(name), instance_(instance), method_(method) {}
        
        benchmark_result run(std::size_t iterations) override {
            benchmark_result result{name_, iterations, {}, 
                                  std::chrono::nanoseconds::max(), {}, {}};
            
            // Warmup
            for (std::size_t i = 0; i < std::min(iterations / 10, std::size_t(100)); ++i) {
                (instance_->*method_)();
            }
            
            // Actual benchmark
            auto total_start = std::chrono::high_resolution_clock::now();
            for (std::size_t i = 0; i < iterations; ++i) {
                auto iter_start = std::chrono::high_resolution_clock::now();
                (instance_->*method_)();
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
        T* instance_;
        void (T::*method_)();
    };
};

// Helper template to make registration easier
template<typename T>
class test_suite : public test_suite_base {
public:
    test_suite(const std::string& suite_name) : test_suite_base(suite_name) {
        instance_ = static_cast<T*>(this);
    }
    
protected:
    // Convenience method for registering tests in derived class constructor
    void register_tests() {
        // Derived class calls this and then adds tests
    }
    
    T* instance_;
};

// Global test suite manager (optional, for automatic registration)
class test_suite_manager {
public:
    static test_suite_manager& instance() {
        static test_suite_manager mgr;
        return mgr;
    }
    
    void register_suite(std::unique_ptr<test_suite_base> suite) {
        suites_.push_back(std::move(suite));
    }
    
    template<typename T, typename... Args>
    T* create_suite(Args&&... args) {
        auto suite = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = suite.get();
        register_suite(std::move(suite));
        return ptr;
    }
    
private:
    std::vector<std::unique_ptr<test_suite_base>> suites_;
};

} // namespace jai::test