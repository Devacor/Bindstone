#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;
using namespace jai::test;

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
    engine engine;
    
    // Should not throw when registering class with methods
    class_builder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
}

JAI_TEST(create_instance) {
    engine engine;
    
    class_builder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
    
    // Should be able to create instance
    script_value result = engine.execute("auto obj = TestClass{}; obj;");
    expect_true(result.is_object());
}

JAI_TEST(call_methods_on_instance) {
    engine engine;
    
    class_builder<TestClass>(engine, "TestClass")
        .constructor<>()
        .method("setValue", &TestClass::setValue)
        .method("getValue", &TestClass::getValue)
        .build();
    
    // Should be able to call methods and get correct result
    script_value result = engine.execute(R"(
        auto obj = TestClass{};
        obj.setValue(42);
        obj.getValue();
    )");
    
    expect_eq(42, result.as_int());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()