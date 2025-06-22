#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <chrono>
#include <iomanip>
#include <cmath>

// ChaiScript
#include "../../../../External/ChaiScript-6.1.0/include/chaiscript/chaiscript.hpp"

using namespace JaiScript;
using namespace JaiScript::Testing;
using namespace std::chrono;

JAI_TEST_SUITE(JaiScriptVsChaiScript)

class Benchmark {
    const std::string name;
    high_resolution_clock::time_point start;
    
public:
    Benchmark(const std::string& n) : name(n), start(high_resolution_clock::now()) {}
    
    ~Benchmark() {
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start).count();
        std::cout << std::setw(40) << std::left << name 
                  << std::setw(8) << std::right << duration << " μs" << std::endl;
    }
};

// Apples-to-apples test: Engine Creation
JAI_TEST(engine_creation_comparison) {
    std::cout << "\nEngine Creation Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript engine creation
    {
        Benchmark b("JaiScript Engine Creation");
        JaiScript::Engine engine;
    }
    
    // ChaiScript engine creation  
    {
        Benchmark b("ChaiScript Engine Creation");
        chaiscript::ChaiScript chai;
    }
    
    expect_true(true); // Always pass - this is for comparison
}

// Apples-to-apples test: Simple Arithmetic
JAI_TEST(simple_arithmetic_comparison) {
    std::cout << "\nSimple Arithmetic Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript
    Value jai_result;
    {
        Benchmark b("JaiScript: 1 + 2 * 3");
        JaiScript::Engine engine;
        jai_result = engine.execute("1 + 2 * 3;");
    }
    
    // ChaiScript
    int chai_result;
    {
        Benchmark b("ChaiScript: 1 + 2 * 3");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>("1 + 2 * 3");
    }
    
    expect_eq(jai_result.as<int>(), 7);
    expect_eq(chai_result, 7);
}

// Apples-to-apples test: Variable Assignment
JAI_TEST(variable_assignment_comparison) {
    std::cout << "\nVariable Assignment Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript
    Value jai_result;
    {
        Benchmark b("JaiScript: var x=42; x=x+1; x");
        JaiScript::Engine engine;
        jai_result = engine.execute("var x = 42; x = x + 1; x;");
    }
    
    // ChaiScript
    int chai_result;
    {
        Benchmark b("ChaiScript: var x=42; x=x+1; x");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>("var x = 42; x = x + 1; x");
    }
    
    expect_eq(jai_result.as<int>(), 43);
    expect_eq(chai_result, 43);
}

// Apples-to-apples test: Function Definition and Call
JAI_TEST(function_definition_comparison) {
    std::cout << "\nFunction Definition Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript
    Value jai_result;
    {
        Benchmark b("JaiScript: def add(a,b) + call");
        JaiScript::Engine engine;
        jai_result = engine.execute("auto add(auto a, auto b) -> auto { return a + b; } add(5, 3);");
    }
    
    // ChaiScript
    int chai_result;
    {
        Benchmark b("ChaiScript: def add(a,b) + call");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>("def add(a, b) { return a + b; } add(5, 3)");
    }
    
    expect_eq(jai_result.as<int>(), 8);
    expect_eq(chai_result, 8);
}

// Apples-to-apples test: Loop (100 iterations)
JAI_TEST(loop_100_comparison) {
    std::cout << "\nLoop (100 iterations) Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript
    Value jai_result;
    {
        Benchmark b("JaiScript: 100 iteration loop");
        JaiScript::Engine engine;
        jai_result = engine.execute(R"(
            var sum = 0;
            for (var i = 0; i < 100; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )");
    }
    
    // ChaiScript
    int chai_result;
    {
        Benchmark b("ChaiScript: 100 iteration loop");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>(R"(
            var sum = 0;
            for (var i = 0; i < 100; ++i) {
                sum = sum + i;
            }
            sum
        )");
    }
    
    expect_eq(jai_result.as<int>(), 4950);
    expect_eq(chai_result, 4950);
}

// Apples-to-apples test: Nested Function with Loop
JAI_TEST(nested_function_loop_comparison) {
    std::cout << "\nNested Function with Loop Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript
    Value jai_result;
    {
        Benchmark b("JaiScript: nested func + 50 loop");
        JaiScript::Engine engine;
        jai_result = engine.execute(R"(
            auto sum(auto n) -> auto {
                var total = 0;
                for (var i = 0; i < n; i = i + 1) {
                    total = total + i;
                }
                return total;
            }
            sum(50);
        )");
    }
    
    // ChaiScript
    int chai_result;
    {
        Benchmark b("ChaiScript: nested func + 50 loop");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>(R"(
            def sum(n) {
                var total = 0;
                for (var i = 0; i < n; ++i) {
                    total = total + i;
                }
                return total;
            }
            sum(50)
        )");
    }
    
    expect_eq(jai_result.as<int>(), 1225);
    expect_eq(chai_result, 1225);
}

// Class and method binding tests
class Vec2 {
public:
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
    
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }
    
    Vec2 operator*(float scalar) const {
        return Vec2(x * scalar, y * scalar);
    }
    
    float dot(const Vec2& other) const {
        return x * other.x + y * other.y;
    }
    
    float length() const {
        return std::sqrt(x * x + y * y);
    }
    
    Vec2 normalize() const {
        float len = length();
        return len > 0 ? Vec2(x/len, y/len) : Vec2(0, 0);
    }
};

// Class binding comparison
JAI_TEST(class_binding_comparison) {
    std::cout << "\nClass Binding Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript class binding
    Value jai_result;
    {
        Benchmark b("JaiScript: class creation + method");
        JaiScript::Engine engine;
        
        // Bind Vec2 class
        makeClassBuilder<Vec2>(engine, "Vec2")
            .constructor<float, float>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            .method("dot", &Vec2::dot)
            .method("length", &Vec2::length)
            .build();
        
        jai_result = engine.execute(R"(
            var v1 = Vec2(3.0, 4.0);
            var v2 = Vec2(1.0, 2.0);
            v1.dot(v2);
        )");
    }
    
    // ChaiScript class binding
    float chai_result;
    {
        Benchmark b("ChaiScript: class creation + method");
        chaiscript::ChaiScript chai;
        
        // Bind Vec2 class
        chai.add(chaiscript::constructor<Vec2(float, float)>(), "Vec2");
        chai.add(chaiscript::fun(&Vec2::x), "x");
        chai.add(chaiscript::fun(&Vec2::y), "y");
        chai.add(chaiscript::fun(&Vec2::dot), "dot");
        chai.add(chaiscript::fun(&Vec2::length), "length");
        
        chai_result = chai.eval<float>(R"(
            var v1 = Vec2(3.0, 4.0);
            var v2 = Vec2(1.0, 2.0);
            v1.dot(v2)
        )");
    }
    
    expect_near(jai_result.as<float>(), 11.0f, 0.001f);  // 3*1 + 4*2 = 11
    expect_near(chai_result, 11.0f, 0.001f);
}

// Operator overloading comparison
JAI_TEST(operator_overloading_comparison) {
    std::cout << "\nOperator Overloading Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript operator overloading
    Value jai_result;
    {
        Benchmark b("JaiScript: operator overloads");
        JaiScript::Engine engine;
        
        makeClassBuilder<Vec2>(engine, "Vec2")
            .constructor<float, float>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            .build();
            
        engine.addFunction("+", [](const Vec2& a, const Vec2& b) -> Vec2 {
            return a + b;
        });
        engine.addFunction("*", [](const Vec2& a, float scalar) -> Vec2 {
            return a * scalar;
        });
        
        jai_result = engine.execute(R"(
            var v1 = Vec2(2.0, 3.0);
            var v2 = Vec2(1.0, 1.0);
            var result = (v1 + v2) * 2.0;
            result.x + result.y;
        )");
    }
    
    // ChaiScript operator overloading
    float chai_result;
    {
        Benchmark b("ChaiScript: operator overloads");
        chaiscript::ChaiScript chai;
        
        chai.add(chaiscript::constructor<Vec2(float, float)>(), "Vec2");
        chai.add(chaiscript::fun(&Vec2::x), "x");
        chai.add(chaiscript::fun(&Vec2::y), "y");
        chai.add(chaiscript::fun([](const Vec2& a, const Vec2& b) { return a + b; }), "+");
        chai.add(chaiscript::fun([](const Vec2& a, float s) { return a * s; }), "*");
        
        chai_result = chai.eval<float>(R"(
            var v1 = Vec2(2.0, 3.0);
            var v2 = Vec2(1.0, 1.0);
            var result = (v1 + v2) * 2.0;
            result.x + result.y
        )");
    }
    
    expect_near(jai_result.as<float>(), 14.0f, 0.001f);  // v1=(2,3), v2=(1,1), v1+v2=(3,4), (3,4)*2=(6,8), 6+8=14
    expect_near(chai_result, 14.0f, 0.001f);
}

// Algorithm: Fibonacci sequence
JAI_TEST(fibonacci_algorithm_comparison) {
    std::cout << "\nFibonacci Algorithm Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript fibonacci
    Value jai_result;
    {
        Benchmark b("JaiScript: fibonacci(20)");
        JaiScript::Engine engine;
        jai_result = engine.execute(R"(
            auto fib(auto n) -> auto {
                if (n <= 1) return n;
                return fib(n-1) + fib(n-2);
            }
            fib(20);
        )");
    }
    
    // ChaiScript fibonacci  
    int chai_result;
    {
        Benchmark b("ChaiScript: fibonacci(20)");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>(R"(
            def fib(n) {
                if (n <= 1) return n;
                return fib(n-1) + fib(n-2);
            }
            fib(20)
        )");
    }
    
    expect_eq(jai_result.as<int>(), 6765);
    expect_eq(chai_result, 6765);
}

// Algorithm: Bubble sort 
JAI_TEST(bubble_sort_algorithm_comparison) {
    std::cout << "\nBubble Sort Algorithm Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript bubble sort
    Value jai_result;
    {
        Benchmark b("JaiScript: bubble sort 50 items");
        JaiScript::Engine engine;
        jai_result = engine.execute(R"(
            auto bubbleSort(auto arr, auto n) -> auto {
                for (var i = 0; i < n-1; i = i + 1) {
                    for (var j = 0; j < n-i-1; j = j + 1) {
                        if (arr[j] > arr[j+1]) {
                            var temp = arr[j];
                            arr[j] = arr[j+1];
                            arr[j+1] = temp;
                        }
                    }
                }
                return arr[0];  // Return first element to verify sorting
            }
            
            var arr = [50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1];
            bubbleSort(arr, 50);
        )");
    }
    
    // ChaiScript bubble sort
    int chai_result;
    {
        Benchmark b("ChaiScript: bubble sort 50 items");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>(R"(
            def bubbleSort(arr, n) {
                for (var i = 0; i < n-1; ++i) {
                    for (var j = 0; j < n-i-1; ++j) {
                        if (arr[j] > arr[j+1]) {
                            var temp = arr[j];
                            arr[j] = arr[j+1];
                            arr[j+1] = temp;
                        }
                    }
                }
                return arr[0];
            }
            
            var arr = [50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1];
            bubbleSort(arr, 50)
        )");
    }
    
    expect_eq(jai_result.as<int>(), 1);
    expect_eq(chai_result, 1);
}

// Algorithm: Prime number calculation
JAI_TEST(prime_algorithm_comparison) {
    std::cout << "\nPrime Algorithm Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript prime counting
    Value jai_result;
    {
        Benchmark b("JaiScript: count primes up to 100");
        JaiScript::Engine engine;
        jai_result = engine.execute(R"(
            auto isPrime(auto n) -> auto {
                if (n < 2) return 0;
                for (var i = 2; i * i <= n; i = i + 1) {
                    if (n % i == 0) return 0;
                }
                return 1;
            }
            
            auto countPrimes(auto limit) -> auto {
                var count = 0;
                for (var i = 2; i < limit; i = i + 1) {
                    if (isPrime(i)) count = count + 1;
                }
                return count;
            }
            
            countPrimes(100);
        )");
    }
    
    // ChaiScript prime counting
    int chai_result;
    {
        Benchmark b("ChaiScript: count primes up to 100");
        chaiscript::ChaiScript chai;
        chai_result = chai.eval<int>(R"(
            def isPrime(n) {
                if (n < 2) return false;
                for (var i = 2; i * i <= n; ++i) {
                    if (n % i == 0) return false;
                }
                return true;
            }
            
            def countPrimes(limit) {
                var count = 0;
                for (var i = 2; i < limit; ++i) {
                    if (isPrime(i)) count = count + 1;
                }
                return count;
            }
            
            countPrimes(100)
        )");
    }
    
    expect_eq(jai_result.as<int>(), 25); // There are 25 primes under 100
    expect_eq(chai_result, 25);
}

// Object-oriented performance test
JAI_TEST(object_oriented_comparison) {
    std::cout << "\nObject-Oriented Performance Comparison:\n";
    std::cout << "-------------------------------------------\n";
    
    // JaiScript OO pattern
    Value jai_result;
    {
        Benchmark b("JaiScript: OO with inheritance pattern");
        JaiScript::Engine engine;
        
        makeClassBuilder<Vec2>(engine, "Vec2")
            .constructor<float, float>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            .method("length", &Vec2::length)
            .method("normalize", &Vec2::normalize)
            .build();
        
        jai_result = engine.execute(R"(
            auto processVectors(auto count) -> auto {
                var sum = 0.0;
                for (var i = 0; i < count; i = i + 1) {
                    var v = Vec2(i, i + 1);
                    var normalized = v.normalize();
                    sum = sum + normalized.length();
                }
                return sum;
            }
            
            processVectors(50);
        )");
    }
    
    // ChaiScript OO pattern
    float chai_result;
    {
        Benchmark b("ChaiScript: OO with inheritance pattern");
        chaiscript::ChaiScript chai;
        
        chai.add(chaiscript::constructor<Vec2(float, float)>(), "Vec2");
        chai.add(chaiscript::fun(&Vec2::x), "x");
        chai.add(chaiscript::fun(&Vec2::y), "y");
        chai.add(chaiscript::fun(&Vec2::length), "length");
        chai.add(chaiscript::fun(&Vec2::normalize), "normalize");
        
        chai_result = chai.eval<float>(R"(
            def processVectors(count) {
                var sum = 0.0;
                for (var i = 0; i < count; ++i) {
                    var v = Vec2(i, i + 1);
                    var normalized = v.normalize();
                    sum = sum + normalized.length();
                }
                return sum;
            }
            
            processVectors(50)
        )");
    }
    
    expect_near(jai_result.as<float>(), 50.0f, 0.1f); // Each normalized vector has length ~1.0
    expect_near(chai_result, 50.0f, 0.1f);
}

// Summary test that prints performance ratios
JAI_TEST(performance_summary) {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║              Performance Summary                      ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    std::cout << "Based on the above timing results:\n";
    std::cout << "- Lower times = better performance\n";
    std::cout << "- JaiScript includes move semantics optimizations\n";
    std::cout << "- ChaiScript is a mature, established scripting engine\n";
    std::cout << "- Both engines tested with identical logic\n\n";
    
    expect_true(true); // Always pass
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()