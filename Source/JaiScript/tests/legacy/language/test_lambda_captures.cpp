#include <jaiscript/jaiscript.hpp>
#include "../jai_test.hpp"

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(LambdaCaptureTests)

JAI_TEST(capture_all_by_value) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto y = 20;
        auto z = 30;
        
        auto lambda = [=](auto bonus) -> auto {
            return x + y + z + bonus;
        };
        
        x = 100;  // Change variables after lambda creation
        y = 200;
        z = 300;
        
        lambda(5);  // Should use original values: 10 + 20 + 30 + 5 = 65
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 65);
}

JAI_TEST(capture_all_by_reference) {
    engine engine;
    
    std::string script = R"(
        auto a = 1;
        auto b = 2;
        
        auto lambda = [&](auto multiplier) -> auto {
            a = a * multiplier;
            b = b * multiplier;
            return a + b;
        };
        
        auto result1 = lambda(10);  // a=10, b=20, result=30
        auto result2 = lambda(2);   // a=20, b=40, result=60
        
        result1 * 100 + result2;   // Should be 3060
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 3060);
}

JAI_TEST(mixed_capture_value_default_with_ref) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto y = 20;
        auto counter = 0;
        
        auto lambda = [=, &counter](auto bonus) -> auto {
            counter = counter + 1;
            return x + y + bonus + counter;
        };
        
        x = 100;  // x should still be 10 in lambda
        y = 200;  // y should still be 20 in lambda
        
        auto result1 = lambda(1);  // 10 + 20 + 1 + 1 = 32
        auto result2 = lambda(2);  // 10 + 20 + 2 + 2 = 34
        
        result1 * 100 + result2;   // Should be 3234
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 3234);
}

JAI_TEST(mixed_capture_ref_default_with_value) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto y = 20;
        auto z = 30;
        
        auto lambda = [&, z](auto bonus) -> auto {
            x = x * 2;  // Should modify original x
            y = y * 2;  // Should modify original y
            return x + y + z + bonus;  // z should be captured by value (30)
        };
        
        auto result = lambda(5);
        auto final_x = x;  // Should be 20
        auto final_y = y;  // Should be 40
        
        result * 10000 + final_x * 100 + final_y;  // Should be 950420
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 950420);
}

JAI_TEST(explicit_value_capture_vs_lexical) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto lambda_explicit = [x](auto y) -> auto {
            return x + y;
        };
        auto lambda_lexical = [](auto y) -> auto {
            return x + y;
        };
        
        x = 99;  // Change x after lambda creation
        
        auto result1 = lambda_explicit(1);  // Should be 11 (captured 10)
        auto result2 = lambda_lexical(1);   // Should be 100 (current x=99)
        
        result1 * 1000 + result2;  // Should be 11100
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 11100);
}

JAI_TEST(reference_capture_modifies_original) {
    engine engine;
    
    std::string script = R"(
        auto counter = 0;
        auto increment = [&counter]() -> auto {
            counter = counter + 1;
            return counter;
        };
        
        auto val1 = increment();  // Should be 1
        auto val2 = increment();  // Should be 2
        counter;                  // Check final counter value
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 2);
}

JAI_TEST(mixed_capture_types) {
    engine engine;
    
    std::string script = R"(
        auto a = 10;
        auto b = 20;
        auto mixed = [a, &b](auto x) -> auto {
            b = b + 5;  // Modify reference-captured b
            return a + b + x;  // a should be 10, b should be 25 after increment
        };
        
        auto result = mixed(1);  // Should be 10 + 25 + 1 = 36
        auto final_b = b;        // b should now be 25
        
        result * 100 + final_b;  // Should be 3625
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 3625);
}

JAI_TEST(nested_function_captures) {
    engine engine;
    
    std::string script = R"(
        auto createCounter = [](auto start) -> auto {
            auto count = start;
            return [&count]() -> auto {
                count = count + 1;
                return count;
            };
        };
        
        auto counter1 = createCounter(10);
        auto counter2 = createCounter(100);
        
        auto c1_val1 = counter1();  // Should be 11
        auto c1_val2 = counter1();  // Should be 12
        auto c2_val1 = counter2();  // Should be 101
        
        c1_val1 * 10000 + c1_val2 * 100 + c2_val1;  // Should be 111201
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<script_int>(), 111201);
}

JAI_TEST(capture_undefined_variable_error) {
    engine engine;
    
    std::string script = R"(
        auto lambda = [undefined_var]() -> auto {
            return undefined_var;
        };
        lambda();
    )";
    
    // TODO: Enable when proper error checking is implemented
    // expect_throws([&]() { engine.execute(script); });
}

JAI_TEST(this_capture_explicit) {
    engine engine;
    
    std::string script = R"(
        class MyClass {
            auto value = 42;
            
            auto createLambda() -> auto {
                return [this]() -> auto {
                    return this.value;
                };
            }
        };
        
        auto obj = MyClass();
        auto lambda = obj.createLambda();
        lambda();
    )";
    
    // Expected to fail until classes are implemented
    // script_value result = engine.execute(script);
    // expect_eq(result.as<script_int>(), 42);
}

JAI_TEST(this_capture_implicit_by_value) {
    engine engine;
    
    std::string script = R"(
        class Calculator {
            auto base = 10;
            auto multiplier = 5;
            
            auto createMultiplierLambda() -> auto {
                return [=](auto input) -> auto {
                    return base * multiplier * input;
                };
            }
        };
        
        auto calc = Calculator();
        auto multiply = calc.createMultiplierLambda();
        
        calc.base = 100;        // Should not affect lambda (captured by value)
        calc.multiplier = 50;   // Should not affect lambda (captured by value)
        
        multiply(2);            // Should be 10 * 5 * 2 = 100
    )";
    
    // Expected to fail until classes are implemented
    // script_value result = engine.execute(script);
    // expect_eq(result.as<script_int>(), 100);
}

JAI_TEST(this_capture_implicit_by_reference) {
    engine engine;
    
    std::string script = R"(
        class Counter {
            auto count = 0;
            
            auto createIncrementer() -> auto {
                return [&]() -> auto {
                    count = count + 1;
                    return count;
                };
            }
        };
        
        auto counter = Counter();
        auto increment = counter.createIncrementer();
        
        auto val1 = increment();  // Should be 1
        auto val2 = increment();  // Should be 2
        auto val3 = increment();  // Should be 3
        
        val1 + val2 + val3;      // Should be 6
    )";
    
    // Expected to fail until classes are implemented
    // script_value result = engine.execute(script);
    // expect_eq(result.as<script_int>(), 6);
}

JAI_TEST(mixed_this_and_local_capture) {
    engine engine;
    
    std::string script = R"(
        class MixedCapture {
            auto memberVar = 100;
            
            auto createMixedLambda() -> auto {
                auto localVar = 200;
                return [this, localVar](auto input) -> auto {
                    return this.memberVar + localVar + input;
                };
            }
        };
        
        auto obj = MixedCapture();
        auto lambda = obj.createMixedLambda();
        
        obj.memberVar = 999;  // Should affect lambda (this captured by reference)
        
        lambda(5);  // Should be 999 + 200 + 5 = 1204
    )";
    
    // Expected to fail until classes are implemented
    // script_value result = engine.execute(script);
    // expect_eq(result.as<script_int>(), 1204);
}

JAI_TEST(this_parsing_in_capture_list) {
    engine engine;
    
    std::string script = R"(
        auto lambda = [this]() -> auto {
            return 42;
        };
        lambda();
    )";
    
    // Test that 'this' at least parses in capture list
    // Even if it doesn't work correctly without classes
    // TODO: Enable when parsing is implemented
    // script_value result = engine.execute(script);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()