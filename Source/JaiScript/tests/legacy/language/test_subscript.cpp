#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(Subscript)

JAI_TEST(array_subscript_read) {
    std::cout << "Testing array subscript read...\n";
    
    engine engine;
    
    script_value result = engine.execute(R"(
        var numbers = [10, 20, 30, 40, 50];
        numbers[2];
    )");
    
    expect_eq(result.as<int>(), 30);
    std::cout << "Array subscript read successful: " << result.as<int>() << "\n";
}

JAI_TEST(array_subscript_loop) {
    std::cout << "Testing array subscript in loop...\n";
    
    engine engine;
    
    script_value result = engine.execute(R"(
        var numbers = [1, 2, 3, 4, 5];
        var sum = 0;
        for (var i = 0; i < 5; i = i + 1) {
            sum = sum + numbers[i];
        }
        sum;
    )");
    
    expect_eq(result.as<int>(), 15);
    std::cout << "Array loop sum successful: " << result.as<int>() << "\n";
}

JAI_TEST(map_subscript_read) {
    std::cout << "Testing map subscript read...\n";
    
    engine engine;
    
    script_value result = engine.execute(R"(
        var scores = {{"Alice", 95}, {"Bob", 87}, {"Charlie", 92}};
        scores["Bob"];
    )");
    
    expect_eq(result.as<int>(), 87);
    std::cout << "Map subscript read successful: " << result.as<int>() << "\n";
}

JAI_TEST(map_subscript_missing_key) {
    std::cout << "Testing map subscript with missing key...\n";
    
    engine engine;
    
    script_value result = engine.execute(R"(
        var scores = {{"Alice", 95}, {"Bob", 87}};
        scores["Charlie"];
    )");
    
    expect_true(result.is_null());
    std::cout << "Missing key returns null as expected\n";
}

JAI_TEST(nested_subscript) {
    std::cout << "Testing nested subscript...\n";
    
    engine engine;
    
    script_value result = engine.execute(R"(
        var matrix = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
        matrix[1][2];
    )");
    
    expect_eq(result.as<int>(), 6);
    std::cout << "Nested subscript successful: " << result.as<int>() << "\n";
}

JAI_TEST(array_bounds_check) {
    std::cout << "Testing array bounds checking...\n";
    
    engine engine;
    
    try {
        engine.execute(R"(
            var arr = [1, 2, 3];
            arr[5];
        )");
        expect_true(false); // Should not reach here
    } catch (const runtime_error& e) {
        std::cout << "Bounds check correctly threw: " << e.what() << "\n";
        expect_true(true);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()