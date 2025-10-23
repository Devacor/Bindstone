#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    auto eng = jai::engine::make();
    jai::stdlib::register_all(*eng);

    // Test multi-argument array concatenation
    auto result1 = eng->execute(R"(
        var arr1 = [1, 2];
        var arr2 = [3, 4];
        var arr3 = [5, 6];
        concatenate(arr1, arr2, arr3);
    )");

    std::cout << "3-array concat: " << result1.to_string() << std::endl;

    // Test multi-argument string concatenation
    auto result2 = eng->execute(R"(
        concatenate("Hello", " ", "World", "!");
    )");

    std::cout << "4-string concat: " << result2.to_string() << std::endl;

    // Test append with multiple args
    auto result3 = eng->execute(R"(
        append("A", "B", "C", "D", "E");
    )");

    std::cout << "5-string append: " << result3.to_string() << std::endl;

    return 0;
}
