#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <iostream>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(ArrayAssignment)

JAI_TEST(array_subscript_write) {
    std::cout << "Testing array subscript write...\n";
    
    Engine engine;
    
    Value result = engine.execute(R"(
        var arr = [1, 2, 3];
        arr[1] = 99;
        arr[1];
    )");
    
    expect_eq(result.as<int>(), 99);
    std::cout << "Array subscript write successful: " << result.as<int>() << "\n";
}

JAI_TEST(array_swap_elements) {
    std::cout << "Testing array element swap...\n";
    
    Engine engine;
    
    Value result = engine.execute(R"(
        var arr = [10, 20];
        var temp = arr[0];
        arr[0] = arr[1];
        arr[1] = temp;
        arr[0];
    )");
    
    expect_eq(result.as<int>(), 20);
    std::cout << "Array swap successful: arr[0] = " << result.as<int>() << "\n";
}

JAI_TEST(array_reversal_algorithm) {
    std::cout << "Testing array reversal algorithm...\n";
    
    Engine engine;
    
    // Test the exact code from performance test
    Value result = engine.execute(R"(
        var arr = [1, 2, 3, 4, 5];
        var n = 5;
        for (var i = 0; i < n / 2; i = i + 1) {
            var temp = arr[i];
            arr[i] = arr[n - 1 - i];
            arr[n - 1 - i] = temp;
        }
        arr[0];  // Should be 5
    )");
    
    std::cout << "First element after reversal: " << result.as<int>() << " (expected: 5)\n";
    expect_eq(result.as<int>(), 5);
    
    // Check all elements
    std::cout << "Full reversed array: ";
    for (int i = 0; i < 5; i++) {
        Value elem = engine.execute("arr[" + std::to_string(i) + "];");
        std::cout << elem.as<int>() << " ";
        expect_eq(elem.as<int>(), 5 - i);
    }
    std::cout << "\n";
}

JAI_TEST(nested_array_assignment) {
    std::cout << "Testing nested array assignment...\n";
    
    Engine engine;
    
    Value result = engine.execute(R"(
        var matrix = [[1, 2], [3, 4]];
        matrix[0][1] = 99;
        matrix[0][1];
    )");
    
    expect_eq(result.as<int>(), 99);
    std::cout << "Nested array assignment successful: " << result.as<int>() << "\n";
}

JAI_TEST(array_modification_in_loop) {
    std::cout << "Testing array modification in loop...\n";
    
    Engine engine;
    
    Value result = engine.execute(R"(
        var arr = [1, 2, 3, 4, 5];
        for (var i = 0; i < 5; i = i + 1) {
            arr[i] = arr[i] * 2;
        }
        var sum = 0;
        for (var i = 0; i < 5; i = i + 1) {
            sum = sum + arr[i];
        }
        sum;
    )");
    
    expect_eq(result.as<int>(), 30); // 2+4+6+8+10 = 30
    std::cout << "Array modification in loop successful: sum = " << result.as<int>() << "\n";
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()