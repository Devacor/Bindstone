#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <memory>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(SharedPtrUnwrap)

class TestClass {
public:
    int value = 42;
    void setValue(int v) { value = v; }
    int getValue() const { return value; }
};

// Function that returns shared_ptr<TestClass>
std::shared_ptr<TestClass> createTestObject() {
    auto obj = std::make_shared<TestClass>();
    obj->setValue(123);
    return obj;
}

// Wrapper that converts shared_ptr to proper class instance
std::function<script_value()> createTestObjectWrapper(engine& eng) {
    return [&eng]() -> script_value {
        auto ptr = createTestObject();
        if (!ptr) {
            return script_value(); // null
        }
        // Use the engine's type converter system to create proper class_instance
        return eng.convert_to_value(typeid(TestClass), ptr.get());
    };
}

// Function that returns shared_ptr<int>
std::shared_ptr<int> createSharedInt() {
    return std::make_shared<int>(456);
}

JAI_TEST(test_shared_ptr_unwrap_class) {
    engine engine;
    
    // Register the TestClass
    class_builder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
    
    // Register function that properly converts shared_ptr<TestClass> to class_instance
    auto wrapper = createTestObjectWrapper(engine);
    engine.add_function("createTestObject", wrapper);
    
    // Test that the returned shared_ptr is auto-unwrapped to TestClass
    auto result = engine.execute("var obj = createTestObject(); obj.getValue();");
    expect_eq(result.as<int>(), 123);
}

JAI_TEST(test_shared_ptr_unwrap_primitive) {
    engine engine;
    
    // Register function that returns shared_ptr<int>
    engine.add_function("createSharedInt", createSharedInt);
    
    // Test that the returned shared_ptr<int> is auto-unwrapped to int
    auto result = engine.execute("createSharedInt();");
    expect_eq(result.as<int>(), 456);
}

JAI_TEST(test_null_shared_ptr) {
    engine engine;
    
    // Register function that returns nullptr shared_ptr
    engine.add_function("createNull", []() -> std::shared_ptr<TestClass> { 
        return nullptr; 
    });
    
    // Test that nullptr shared_ptr becomes script null
    auto result = engine.execute("createNull();");
    expect_true(result.is_null());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()