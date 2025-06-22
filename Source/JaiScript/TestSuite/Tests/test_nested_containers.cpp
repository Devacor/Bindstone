#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

class Vec2 {
public:
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
};

JAI_TEST_SUITE(NestedContainers)

JAI_TEST(array_of_maps) {
    std::cout << "Testing array<map<string, Vec2>>...\n";
    
    try {
        JaiScript::Engine engine;
        
        makeClassBuilder<Vec2>(engine, "Vec2")
            .constructor<>()
            .constructor<float, float>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            .build();
        
        // Test nested container initialization with new syntax
        Value result = engine.execute(R"(
            var locations = [
                {{"player", Vec2(1.0, 1.0)}, {"enemy", Vec2(5.0, 3.0)}},
                {{"boss", Vec2(10.0, 10.0)}, {"treasure", Vec2(2.0, 8.0)}}
            ];
            locations;
        )");
        
        std::cout << "Nested container creation successful!\n";
        std::cout << "Result type: " << (result.isArray() ? "Array" : "Other") << "\n";
        std::cout << "Result string: " << result.toString() << "\n";
        
        if (result.isArray()) {
            auto& array = result.asArray();
            std::cout << "Array size: " << array.size() << "\n";
            expect_eq(array.size(), 2);
            
            // First element should be a map
            if (array.size() > 0) {
                std::cout << "First element type: " << (array[0].isMap() ? "Map" : "Other") << "\n";
                expect_true(array[0].isMap());
                std::cout << "First element is correctly a map\n";
            }
        } else {
            std::cout << "Result is not an array as expected\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "Nested container test exception: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST(map_of_arrays) {
    std::cout << "Testing map<string, array<int>>...\n";
    
    try {
        JaiScript::Engine engine;
        
        Value result = engine.execute(R"(
            var data = {
                {"scores", {95, 87, 92}},
                {"ages", {25, 30, 28}}
            };
            data;
        )");
        
        std::cout << "Map of arrays creation successful!\n";
        expect_true(result.isMap());
        
        auto& map = result.asMap();
        expect_eq(map.size(), 2);
        std::cout << "Map contains " << map.size() << " entries\n";
        
    } catch (const std::exception& e) {
        std::cout << "Map of arrays test exception: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST(deeply_nested) {
    std::cout << "Testing deeply nested structures...\n";
    
    try {
        JaiScript::Engine engine;
        
        makeClassBuilder<Vec2>(engine, "Vec2")
            .constructor<>()
            .constructor<float, float>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            .build();
        
        // Test array<array<Vec2>>
        Value result = engine.execute(R"(
            var grid = [
                [Vec2(0.0, 0.0), Vec2(1.0, 0.0), Vec2(2.0, 0.0)],
                [Vec2(0.0, 1.0), Vec2(1.0, 1.0), Vec2(2.0, 1.0)]
            ];
            grid;
        )");
        
        std::cout << "Deeply nested structure creation successful!\n";
        expect_true(result.isArray());
        
        auto& outerArray = result.asArray();
        expect_eq(outerArray.size(), 2);
        
        // Check that inner elements are also arrays
        expect_true(outerArray[0].isArray());
        auto& innerArray = outerArray[0].asArray();
        expect_eq(innerArray.size(), 3);
        std::cout << "Grid structure is valid\n";
        
    } catch (const std::exception& e) {
        std::cout << "Deeply nested test exception: " << e.what() << "\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()