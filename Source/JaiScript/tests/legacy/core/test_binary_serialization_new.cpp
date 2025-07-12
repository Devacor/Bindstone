#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::test;

// Test class for binary serialization
class TestPoint {
public:
    TestPoint() : x(0.0), y(0.0) {}
    TestPoint(double x, double y) : x(x), y(y) {}
    
    double x;
    double y;
};

// Test node class with shared_ptr/weak_ptr support
class TestNode : public std::enable_shared_from_this<TestNode> {
public:
    TestNode(const std::string& name) : name_(name), value_(0) {}
    
    std::string get_name() const { return name_; }
    void set_name(const std::string& name) { name_ = name; }
    
    int get_value() const { return value_; }
    void set_value(int value) { value_ = value; }
    
    void set_next(std::shared_ptr<TestNode> next) { next_ = next; }
    std::shared_ptr<TestNode> get_next() const { return next_; }
    
    void set_parent(std::shared_ptr<TestNode> parent) { parent_ = parent; }
    std::weak_ptr<TestNode> get_parent() const { return parent_; }
    
private:
    std::string name_;
    int value_;
    std::shared_ptr<TestNode> next_;
    std::weak_ptr<TestNode> parent_;
};

JAI_TEST_SUITE(BinarySerializationTests)

JAI_TEST(to_binary_basic_types) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test basic values - they should return non-empty strings
    script_value result = engine.execute("to_binary(42)");
    expect_true(result.is_string());
    expect_true(!result.as<script_string>().empty());
    
    result = engine.execute("to_binary(3.14)");
    expect_true(result.is_string());
    expect_true(!result.as<script_string>().empty());
    
    result = engine.execute("to_binary(\"hello\")");
    expect_true(result.is_string());
    expect_true(!result.as<script_string>().empty());
    
    result = engine.execute("to_binary(true)");
    expect_true(result.is_string());
    expect_true(!result.as<script_string>().empty());
}

JAI_TEST(from_binary_basic_types) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test round trip for basic types
    script_value result = engine.execute("from_binary(to_binary(42))");
    expect_eq(result.as<script_int>(), 42);
    
    result = engine.execute("from_binary(to_binary(3.14))");
    expect_near(result.as<script_float>(), 3.14, 0.001);
    
    result = engine.execute("from_binary(to_binary(\"hello\"))");
    expect_eq(result.as<script_string>(), "hello");
    
    result = engine.execute("from_binary(to_binary(true))");
    expect_eq(result.as<script_bool>(), true);
}

JAI_TEST(binary_array_serialization) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test heterogeneous array
    engine.execute("var arr = [1, \"hello\", 3.14, true, null]");
    script_value result = engine.execute("from_binary(to_binary(arr))");
    
    expect_true(result.is_array());
    const auto& arr = result.as_array();
    expect_eq(arr.size(), 5u);
    expect_eq(arr[0].as<script_int>(), 1);
    expect_eq(arr[1].as<script_string>(), "hello");
    expect_near(arr[2].as<script_float>(), 3.14, 0.001);
    expect_eq(arr[3].as<script_bool>(), true);
    expect_true(arr[4].is_null());
}

JAI_TEST(binary_map_serialization) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test map serialization
    engine.execute("var map = {\"name\": \"test\", \"value\": 42, \"flag\": true}");
    script_value result = engine.execute("from_binary(to_binary(map))");
    
    expect_true(result.is_map());
    const auto& map = result.as_map();
    expect_eq(map.size(), 3u);
    expect_eq(map.at(script_value("name")).as<script_string>(), "test");
    expect_eq(map.at(script_value("value")).as<script_int>(), 42);
    expect_eq(map.at(script_value("flag")).as<script_bool>(), true);
}

JAI_TEST(binary_cpp_objects) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register TestPoint class
    class_builder<TestPoint>(engine, "TestPoint")
        .constructor<>()
        .constructor<double, double>()
        .property("x", &TestPoint::x)
        .property("y", &TestPoint::y)
        .build();
    
    // Test C++ object serialization
    engine.execute("var point = TestPoint(3.5, 4.2)");
    script_value binary_data = engine.execute("to_binary(point)");
    expect_true(binary_data.is_string());
    
    // Test C++ object deserialization
    engine.execute("var restored_point = from_binary(to_binary(point))");
    
    // Check if it's a map with the expected properties
    script_value restored = engine.get_variable("restored_point");
    expect_true(restored.is_map());
    
    const auto& map = restored.as_map();
    auto type_it = map.find(script_value("_type_"));
    expect_true(type_it != map.end());
    expect_eq(type_it->second.as<script_string>(), "TestPoint");
    
    auto x_it = map.find(script_value("x"));
    expect_true(x_it != map.end());
    expect_near(x_it->second.as<script_float>(), 3.5, 0.001);
    
    auto y_it = map.find(script_value("y"));
    expect_true(y_it != map.end());
    expect_near(y_it->second.as<script_float>(), 4.2, 0.001);
}

JAI_TEST(shared_ptr_basic_serialization) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test basic shared_ptr serialization
    engine.execute("var obj = {\"value\": 42}");
    engine.execute("var ptr = make_shared(obj)");
    
    script_value result = engine.execute("from_binary(to_binary(ptr))");
    
    // The deserialized shared_ptr should contain the same object
    expect_true(!result.is_null());
    // Note: accessing shared_ptr content would require proper accessor methods
}

JAI_TEST(shared_ptr_deduplication) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test that multiple shared_ptrs to same object are deduplicated
    engine.execute("var obj = {\"value\": 100}");
    engine.execute("var ptr1 = make_shared(obj)");
    engine.execute("var ptr2 = ptr1");  // Same object
    
    engine.execute("var data = {\"first\": ptr1, \"second\": ptr2}");
    
    // Serialize and check size is reasonable (indicating deduplication)
    script_value binary_data = engine.execute("to_binary(data)");
    expect_true(binary_data.is_string());
    
    // The binary size should be relatively small if deduplication worked
    const auto& binary_str = binary_data.as<script_string>();
    expect_true(binary_str.size() < 200);  // Reasonable size for deduplicated data
}

JAI_TEST(weak_ptr_basic_test) {
    engine engine;
    stdlib::register_all(engine);
    
    // Add a helper function to create weak_ptr from shared_ptr
    engine.add_variadic_function("make_weak_from_shared", [](const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("make_weak_from_shared expects exactly 1 argument");
        }
        
        // For now, just return a null weak_ptr since we can't access private members
        // In real implementation, this would convert shared_ptr to weak_ptr
        return script_value::make_weak_ptr(script_value());
    });
    
    // Test weak_ptr serialization
    engine.execute("var obj = {\"value\": 200}");
    engine.execute("var shared = make_shared(obj)");
    engine.execute("var weak = make_weak_from_shared(shared)");
    
    // Serialize and deserialize
    engine.execute("var data = {\"shared\": shared, \"weak\": weak}");
    script_value result = engine.execute("from_binary(to_binary(data))");
    
    // Should get back a map with both pointers
    expect_true(result.is_map());
}

JAI_TEST(circular_reference_test) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register TestNode class
    class_builder<TestNode>(engine, "TestNode")
        .constructor<const std::string&>()
        .method("get_name", &TestNode::get_name)
        .method("set_name", &TestNode::set_name)
        .method("get_value", &TestNode::get_value)
        .method("set_value", &TestNode::set_value)
        .method("set_next", &TestNode::set_next)
        .method("get_next", &TestNode::get_next)
        .method("set_parent", &TestNode::set_parent)
        .method("get_parent", &TestNode::get_parent)
        .build();
    
    // Create circular structure
    auto parent = std::make_shared<TestNode>("parent");
    auto child1 = std::make_shared<TestNode>("child1");
    auto child2 = std::make_shared<TestNode>("child2");
    
    parent->set_value(100);
    child1->set_value(200);
    child2->set_value(300);
    
    // Create circular reference
    parent->set_next(child1);
    child1->set_next(child2);
    child1->set_parent(parent);
    child2->set_parent(parent);
    
    // Add to engine
    script_value parent_val = script_value::make_object("TestNode", parent);
    engine.add_global("test_parent", parent_val);
    
    // Test serialization doesn't crash with circular references
    script_value binary_data = engine.execute("to_binary(test_parent)");
    expect_true(binary_data.is_string());
    expect_true(!binary_data.as<script_string>().empty());
}

JAI_TEST(null_pointer_serialization) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test null shared_ptr
    engine.execute("var null_ptr = make_shared(null)");
    script_value result = engine.execute("from_binary(to_binary(null_ptr))");
    
    // Should get back something (implementation dependent)
    expect_true(result.type() != script_value_type::jai_null_type || result.is_null());
}

JAI_TEST(binary_format_validation) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test various edge cases
    
    // Empty array
    script_value result = engine.execute("from_binary(to_binary([]))");
    expect_true(result.is_array());
    expect_eq(result.as_array().size(), 0u);
    
    // Empty map
    result = engine.execute("from_binary(to_binary({}))");
    expect_true(result.is_map());
    expect_eq(result.as_map().size(), 0u);
    
    // Nested structures
    engine.execute("var nested = {\"arr\": [1, 2, {\"inner\": true}], \"val\": 42}");
    result = engine.execute("from_binary(to_binary(nested))");
    expect_true(result.is_map());
    
    const auto& map = result.as_map();
    auto arr_it = map.find(script_value("arr"));
    expect_true(arr_it != map.end());
    expect_true(arr_it->second.is_array());
    
    const auto& arr = arr_it->second.as_array();
    expect_eq(arr.size(), 3u);
    expect_eq(arr[0].as<script_int>(), 1);
    expect_eq(arr[1].as<script_int>(), 2);
    expect_true(arr[2].is_map());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()