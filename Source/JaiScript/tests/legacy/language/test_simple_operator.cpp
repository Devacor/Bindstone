#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

class Vec {
public:
    float x, y;
    Vec(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
};

JAI_TEST_SUITE(SimpleOperator)

JAI_TEST(basic_operator_test) {
    engine engine;
    
    class_builder<Vec>(engine, "Vec")
        .constructor<float, float>()
        .property("x", &Vec::x)
        .property("y", &Vec::y)
        .build();
    
    // Simple operator
    engine.add_function("+", [](const Vec& a, const Vec& b) -> Vec {
        std::cout << "Operator called!\n";
        return Vec(a.x + b.x, a.y + b.y);
    });
    
    try {
        std::cout << "Creating vectors...\n";
        engine.execute("var v1 = Vec(1.0, 2.0);");
        engine.execute("var v2 = Vec(3.0, 4.0);");
        
        // Check if + operator is available
        std::cout << "Checking if + operator is available...\n";
        if (engine.has_variable("+")) {
            std::cout << "+ operator is registered\n";
            auto plusOp = engine.get_variable("+");
            if (plusOp.is_function()) {
                std::cout << "+ is a function\n";
            } else {
                std::cout << "+ is not a function\n";
            }
        } else {
            std::cout << "+ operator is NOT registered\n";
        }
        
        std::cout << "Testing addition...\n";
        script_value result = engine.execute("v1 + v2;");
        
        std::cout << "Extracting result...\n";
        auto v = result.as<std::shared_ptr<Vec>>();
        std::cout << "Result: (" << v->x << ", " << v->y << ")\n";
        
        expect_near(v->x, 4.0f, 0.001f);
        expect_near(v->y, 6.0f, 0.001f);
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()