#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(DebugClass)

class SimpleClass {
public:
    int value = 0;
    void setValue(int v) { value = v; }
    int getValue() const { return value; }
};

JAI_TEST(debug_class_registration) {
    std::cout << "Creating engine\n" << std::flush;
    engine engine;
    
    std::cout << "Creating class builder\n" << std::flush;
    auto builder = class_builder<SimpleClass>(engine, "SimpleClass");
    
    std::cout << "Adding constructor\n" << std::flush;
    builder.constructor<>();
    
    std::cout << "Adding setValue method\n" << std::flush;
    builder.method("setValue", &SimpleClass::setValue);
    
    std::cout << "Adding getValue method\n" << std::flush;
    builder.method("getValue", &SimpleClass::getValue);
    
    std::cout << "Executing script to create instance\n" << std::flush;
    engine.execute("var obj = SimpleClass();");
    
    std::cout << "Executing script to call method\n" << std::flush;
    engine.execute("obj.setValue(42);");
    
    std::cout << "Getting value\n" << std::flush;
    engine.execute("var result = obj.getValue();");
    
    expect_eq(engine.get_variable("result").as<int>(), 42);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()