#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>
#include <typeinfo>

using namespace jai;
using namespace jai::test;

class SimpleClass {
public:
    int value;
    SimpleClass(int v = 0) : value(v) {}
};

JAI_TEST_SUITE(CustomTypeDebug)

JAI_TEST(type_name_matching) {
    engine engine;
    
    // Register the class
    class_builder<SimpleClass>(engine, "SimpleClass")
        .constructor<int>()
        .property("value", &SimpleClass::value)
        .build();
    
    // Check what typeid gives us
    std::cout << "typeid(SimpleClass).name() = " << typeid(SimpleClass).name() << std::endl;
    
    // Try returning a custom type from a function
    engine.add_function("makeSimple", [className = std::string("SimpleClass")](int val) -> script_value {
        auto obj = std::make_shared<SimpleClass>(val);
        return script_value::make_object(className, std::static_pointer_cast<void>(obj));
    });
    
    try {
        script_value result = engine.execute("makeSimple(42);");
        std::cout << "Successfully created SimpleClass" << std::endl;
        
        auto obj = result.as<std::shared_ptr<SimpleClass>>();
        expect_eq(obj->value, 42);
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        expect_true(false);  // Force test failure
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()