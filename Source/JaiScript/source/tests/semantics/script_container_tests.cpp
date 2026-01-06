#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <jaiscript/core/bound_map.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/engine_make_object.hpp>
#include <chrono>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test widget class to verify deep copy behavior
struct TestWidget {
    std::string name;
    int value;
    static int instance_count;
    static int copy_count;
    
    TestWidget(const std::string& n, int v) : name(n), value(v) {
        instance_count++;
    }
    
    TestWidget(const TestWidget& other) : name(other.name), value(other.value) {
        instance_count++;
        copy_count++;
    }
    
    ~TestWidget() {
        instance_count--;
    }
    
    static void reset_counts() {
        instance_count = 0;
        copy_count = 0;
    }
};

int TestWidget::instance_count = 0;
int TestWidget::copy_count = 0;

class script_container_tests : public suite {
public:
    script_container_tests() : suite("script_container_tests") {}
    
    void pre_test() override {
        // Reset static counters before each test
        TestWidget::reset_counts();
    }
    
    void forge_tests() override {
        // ========== SCRIPT ARRAY TESTS ==========
        
        test("bound_array_reference_semantics", [this]() {
            auto eng = engine::make();
            
            // Create a script array
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_value(10));
            arr.push_back(eng->make_value(20));
            arr.push_back(eng->make_value(30));
            
            // Create reference wrapper (no copy)
            bound_array<int> arr_ref(arr_val.as_array(), eng.get());
            check_eq(arr_ref.size(), 3u);
            check(!arr_ref.is_owned());
            
            // Modifications affect original
            arr_ref.push_back(40);
            check_eq(arr_val.as_array().size(), 4u);
            check_eq(arr_ref.size(), 4u);
        });
        
        test("bound_array_value_semantics", [this]() {
            auto eng = engine::make();
            
            // Create a script array
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_value(10));
            arr.push_back(eng->make_value(20));
            
            // Create by value (deep copy)
            bound_array<int> arr_copy(arr_val);
            check_eq(arr_copy.size(), 2u);
            check(arr_copy.is_owned());
            
            // Modifications don't affect original
            arr_copy.push_back(30);
            check_eq(arr_val.as_array().size(), 2u);
            check_eq(arr_copy.size(), 3u);
        });
        
        test("bound_array_copy_constructor", [this]() {
            auto eng = engine::make();
            
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_value(100));
            
            // Reference wrapper
            bound_array<int> arr_ref(arr_val.as_array(), eng.get());
            
            // Copy from reference creates owned copy
            bound_array<int> arr_copy(arr_ref);
            check(arr_copy.is_owned());
            
            arr_copy.push_back(200);
            check_eq(arr_ref.size(), 1u);  // Original unchanged
            check_eq(arr_copy.size(), 2u);
        });
        
        test("bound_array_function_parameters", [this]() {
            auto eng = engine::make();
            
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_value(1));
            arr.push_back(eng->make_value(2));
            arr.push_back(eng->make_value(3));
            
            bound_array<int> arr_ref(arr_val.as_array(), eng.get());
            
            // By value - should copy
            auto by_value = [](bound_array<int> arr) {
                arr.push_back(999);
                return arr.size();
            };
            
            size_t new_size = by_value(arr_ref);
            check_eq(new_size, 4u);
            check_eq(arr_ref.size(), 3u);  // Original unchanged
            
            // By reference - no copy
            auto by_ref = [](bound_array<int>& arr) {
                arr.push_back(777);
                return arr.size();
            };
            
            new_size = by_ref(arr_ref);
            check_eq(new_size, 4u);
            check_eq(arr_ref.size(), 4u);  // Original changed
            
            // By const reference - no copy
            auto by_const_ref = [](const bound_array<int>& arr) {
                int sum = 0;
                for (int val : arr) {
                    sum += val;
                }
                return sum;
            };
            
            int sum = by_const_ref(arr_ref);
            check_eq(sum, 1 + 2 + 3 + 777);
        });
        
        test("bound_array_element_access", [this]() {
            auto eng = engine::make();
            
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_value(10));
            arr.push_back(eng->make_value(20));
            
            bound_array<int> barr(arr_val.as_array(), eng.get());
            
            // Read access
            check_eq(barr[0], 10);
            check_eq(barr[1], 20);
            
            // Write access
            barr[0] = 100;
            check_eq(barr[0], 100);
            check_eq(arr_val.as_array()[0].as<int>(), 100);  // Original modified
        });
        
        test("bound_array_iteration", [this]() {
            auto eng = engine::make();
            
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_value(1));
            arr.push_back(eng->make_value(2));
            arr.push_back(eng->make_value(3));
            
            bound_array<int> barr(arr_val.as_array(), eng.get());
            
            // Range-based for
            int sum = 0;
            for (int val : barr) {
                sum += val;
            }
            check_eq(sum, 6);
            
            // Iterator access
            auto it = barr.begin();
            check_eq(*it, 1);
            ++it;
            check_eq(*it, 2);
        });
        
        test("bound_array_custom_objects", [this]() {
            auto eng = engine::make();
            
            // Register TestWidget
            dynamic_binder<TestWidget>(*eng, "TestWidget")
                .constructor<std::string, int>()
                .property("name", &TestWidget::name)
                .property("value", &TestWidget::value)
                .build();
            
            TestWidget::reset_counts();
            
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            arr.push_back(eng->make_object<TestWidget>("Button", 100));
            arr.push_back(eng->make_object<TestWidget>("Label", 200));
            
            check_eq(TestWidget::instance_count, 2);
            check_eq(TestWidget::copy_count, 0);
            
            // Reference wrapper - no copies
            bound_array<TestWidget> widgets_ref(arr_val.as_array(), eng.get());
            check_eq(TestWidget::copy_count, 0);
            
            // By value parameter - should trigger copies
            auto process = [](bound_array<TestWidget> widgets) {
                return widgets.size();
            };
            
            size_t count = process(widgets_ref);
            check_eq(count, 2u);
            check_eq(TestWidget::copy_count, 2);  // Deep copy of elements
        });
        
        // ========== SCRIPT MAP TESTS ==========
        
        test("bound_map_reference_semantics", [this]() {
            auto eng = engine::make();
            
            script_value map_val = eng->make_map();
            auto& map = map_val.as_map();
            map.insert_or_assign(eng->make_value("one"), eng->make_value(1));
            map.insert_or_assign(eng->make_value("two"), eng->make_value(2));
            
            // Reference wrapper
            bound_map<std::string, int> map_ref(map_val.as_map(), eng.get());
            check_eq(map_ref.size(), 2u);
            check(!map_ref.is_owned());
            
            // Modifications affect original
            map_ref.insert("three", 3);
            check_eq(map_val.as_map().size(), 3u);
            check_eq(map_ref.size(), 3u);
        });
        
        test("bound_map_value_semantics", [this]() {
            auto eng = engine::make();
            
            script_value map_val = eng->make_map();
            auto& jmap = map_val.as_map();
            jmap.insert_or_assign(eng->make_value("alpha"), eng->make_value(10));
            jmap.insert_or_assign(eng->make_value("beta"), eng->make_value(20));
            
            // By value (deep copy)
            bound_map<std::string, int> map_copy(map_val);
            check_eq(map_copy.size(), 2u);
            check(map_copy.is_owned());
            
            // Modifications don't affect original
            map_copy.insert("gamma", 30);
            check_eq(map_val.as_map().size(), 2u);
            check_eq(map_copy.size(), 3u);
        });
        
        test("bound_map_element_access", [this]() {
            auto eng = engine::make();
            
            script_value map_val = eng->make_map();
            auto& jmap = map_val.as_map();
            jmap.insert_or_assign(eng->make_value("x"), eng->make_value(100));
            jmap.insert_or_assign(eng->make_value("y"), eng->make_value(200));
            
            bound_map<std::string, int> map(map_val.as_map(), eng.get());
            
            // Read access
            check_eq(map.at("x"), 100);
            check_eq(map.at("y"), 200);
            check(map.contains("x"));
            check(!map.contains("z"));
            
            // Write access
            map["x"] = 1000;
            check_eq(map.at("x"), 1000);
            
            // Insert via operator[]
            map["z"] = 300;
            check_eq(map.size(), 3u);
            check_eq(map.at("z"), 300);
        });
        
        test("bound_map_iteration", [this]() {
            auto eng = engine::make();
            
            script_value map_val = eng->make_map();
            auto& jmap = map_val.as_map();
            jmap.insert_or_assign(eng->make_value("a"), eng->make_value(1));
            jmap.insert_or_assign(eng->make_value("b"), eng->make_value(2));
            jmap.insert_or_assign(eng->make_value("c"), eng->make_value(3));
            
            bound_map<std::string, int> map(map_val.as_map(), eng.get());
            
            // Range-based for
            int sum = 0;
            for (const auto& [key, value] : map) {
                sum += value;
            }
            check_eq(sum, 6);
        });
        
        // ========== BINDING REJECTION TESTS ==========
        // Commented out for now as they test compile-time failures
        
        /*
        test("reject_vector_reference_bindings", [this]() {
            auto eng = engine::make();
            
            // These should compile and work
            eng->add_function("take_vector_by_value", 
                [](std::vector<int> vec) { return vec.size(); });
            
            eng->add_function("take_bound_array_ref", 
                [](bound_array<int>& arr) { arr.push_back(42); });
            
            eng->add_function("take_bound_array_const_ref", 
                [](const bound_array<int>& arr) { return arr.size(); });
            
            // These should fail to compile if uncommented:
            // eng->add_function("bad_const_vector_ref", 
            //     [](const std::vector<int>& vec) { return vec.size(); });
            
            // eng->add_function("bad_vector_ref", 
            //     [](std::vector<int>& vec) { vec.push_back(42); });
            
            check(true);  // If we get here, rejection is working
        });
        
        test("reject_map_reference_bindings", [this]() {
            auto eng = engine::make();
            
            // These should compile and work
            eng->add_function("take_map_by_value", 
                [](std::map<std::string, int> m) { return m.size(); });
            
            eng->add_function("take_bound_map_ref", 
                [](bound_map<std::string, int>& m) { m.insert("key", 42); });
            
            // These should fail to compile if uncommented:
            // eng->add_function("bad_const_map_ref", 
            //     [](const std::map<std::string, int>& m) { return m.size(); });
            
            // eng->add_function("bad_map_ref", 
            //     [](std::map<std::string, int>& m) { m["key"] = 42; });
            
            check(true);  // If we get here, rejection is working
        });
        */
        
        // ========== PERFORMANCE TEST ==========
        
        test("bound_array_zero_copy_performance", [this]() {
            auto eng = engine::make();
            
            // Create large array
            script_value arr_val = eng->make_array();
            auto& arr = arr_val.as_array();
            for (int i = 0; i < 10000; ++i) {
                arr.push_back(eng->make_value(i));
            }
            
            // Reference wrapper should be instant
            auto start = std::chrono::high_resolution_clock::now();
            bound_array<int> arr_ref(arr_val.as_array(), eng.get());
            auto ref_time = std::chrono::high_resolution_clock::now() - start;
            
            // Iteration should be fast (no copying)
            start = std::chrono::high_resolution_clock::now();
            int sum = 0;
            for (int val : arr_ref) {
                sum += val;
            }
            auto iter_time = std::chrono::high_resolution_clock::now() - start;
            
            // Value copy should take time
            start = std::chrono::high_resolution_clock::now();
            bound_array<int> arr_copy(arr_val);
            auto copy_time = std::chrono::high_resolution_clock::now() - start;
            
            // Reference creation should be much faster than copying
            check(ref_time < copy_time / 10);
            
            // Verify sum is correct
            check_eq(sum, (10000 * 9999) / 2);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::script_container_tests)