#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

class FloatTest {
public:
    FloatTest(float value) : value_(value) {}
    
    float getValue() const { return value_; }
    
private:
    float value_;
};

JAI_TEST_SUITE(FloatConversion)

JAI_TEST(direct_float_value) {
    Engine engine;
    
    Value floatVal = engine.execute("5.0;");
    expect_true(floatVal.isFloat());
    expect_eq(5.0f, floatVal.as<float>());
}

JAI_TEST(register_class_with_float_constructor) {
    Engine engine;
    
    // Should not throw when registering class with float constructor
    makeClassBuilder<FloatTest>(engine, "FloatTest")
        .constructor<float>()
        .method("getValue", &FloatTest::getValue)
        .build();
}

JAI_TEST(create_instance_with_float) {
    Engine engine;
    
    makeClassBuilder<FloatTest>(engine, "FloatTest")
        .constructor<float>()
        .method("getValue", &FloatTest::getValue)
        .build();
    
    // Should be able to create instance with float parameter
    Value result = engine.execute("FloatTest{5.0};");
    expect_true(result.isObject());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()