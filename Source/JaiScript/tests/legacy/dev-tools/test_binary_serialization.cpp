#include "jai_test.hpp"
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

JAI_TEST(binary_cpp_objects) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register TestPoint class
    make_class_builder<TestPoint>(engine, "TestPoint")
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
    script_value restored = engine.get_variable("restored_point");
    
    // Should be reconstructed as TestPoint instance
    auto instance = restored.as<std::shared_ptr<class_instance>>();
    expect_true(instance != nullptr);
    expect_eq(instance->get_class_name(), "TestPoint");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()