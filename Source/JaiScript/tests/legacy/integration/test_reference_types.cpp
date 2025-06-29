#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

class SimpleClass {
public:
    int value;
    SimpleClass(int v = 0) : value(v) {}
};

JAI_TEST_SUITE(ReferenceTypes)

JAI_TEST(non_const_reference_works) {
    engine engine;
    
    make_class_builder<SimpleClass>(engine, "SimpleClass")
        .constructor<int>()
        .property("value", &SimpleClass::value)
        .build();
    
    // Test SimpleClass& (non-const reference)
    engine.add_function("modifyValue", [](SimpleClass& obj, int newValue) -> void {
        std::cout << "modifyValue: old=" << obj.value << ", new=" << newValue << "\n";
        obj.value = newValue;
    });
    
    engine.execute(R"(
        var obj = SimpleClass(10);
        modifyscript_value(obj, 42);
    )");
    
    std::cout << "Non-const reference test completed\n";
}

JAI_TEST(shared_ptr_reference_works) {
    engine engine;
    
    make_class_builder<SimpleClass>(engine, "SimpleClass")
        .constructor<int>()
        .property("value", &SimpleClass::value)
        .build();
    
    // Test std::shared_ptr<SimpleClass>& 
    engine.add_function("printPtr", [](std::shared_ptr<SimpleClass>& obj) -> void {
        std::cout << "printPtr: value=" << (obj ? std::to_string(obj->value) : "null") << "\n";
    });
    
    engine.execute(R"(
        var obj = SimpleClass(99);
        printPtr(obj);
    )");
    
    std::cout << "Shared_ptr reference test completed\n";
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()