#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

class TestClass {
public:
    int value = 0;
    
    void setValue(int v) { 
        value = v; 
    }
    
    int getValue() const { 
        return value; 
    }
};

JAI_TEST_SUITE(ClassMethods)

JAI_TEST(register_class_with_methods) {
    Engine engine;
    
    // Should not throw when registering class with methods
    makeClassBuilder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
}

JAI_TEST(create_instance) {
    Engine engine;
    
    makeClassBuilder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
    
    // Should be able to create instance
    Value result = engine.execute("auto obj = TestClass{}; obj;");
    expect_true(result.isObject());
}

JAI_TEST(call_methods_on_instance) {
    Engine engine;
    
    makeClassBuilder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
    
    // Should be able to call methods and get correct result
    Value result = engine.execute(R"(
        auto obj = TestClass{};
        obj.setValue(42);
        obj.getValue();
    )");
    
    expect_eq(42, result.asInt());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()