#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <chrono>
#include <iostream>

using namespace jai;
using namespace jai::test;
using namespace std::chrono;

JAI_TEST_SUITE(ArrayMethodsPerformance)

JAI_TEST(array_method_call_performance) {
    engine engine;
    
    // Setup
    engine.execute(R"(
        var arr = [1, 2, 3, 4, 5];
    )");
    
    const int iterations = 100000;
    
    // Measure size() method calls
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        engine.execute("arr.size();");
    }
    auto end = high_resolution_clock::now();
    
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << "\nArray size() method calls:\n";
    std::cout << "  Total time for " << iterations << " calls: " << duration << " µs\n";
    std::cout << "  Average time per call: " << (double)duration / iterations << " µs\n";
    
    // Measure push() method calls
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations / 10; ++i) {  // Less iterations since push grows the array
        engine.execute("arr.push(42);");
    }
    end = high_resolution_clock::now();
    
    duration = duration_cast<microseconds>(end - start).count();
    std::cout << "\nArray push() method calls:\n";
    std::cout << "  Total time for " << iterations/10 << " calls: " << duration << " µs\n";
    std::cout << "  Average time per call: " << (double)duration / (iterations/10) << " µs\n";
    
    // Measure empty() method calls
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        engine.execute("arr.empty();");
    }
    end = high_resolution_clock::now();
    
    duration = duration_cast<microseconds>(end - start).count();
    std::cout << "\nArray empty() method calls:\n";
    std::cout << "  Total time for " << iterations << " calls: " << duration << " µs\n";
    std::cout << "  Average time per call: " << (double)duration / iterations << " µs\n";
    
    // Always pass the test
    expect_true(true);
}

JAI_TEST(array_method_lookup_overhead) {
    engine engine;
    
    // Compare method lookup vs direct subscript access
    engine.execute(R"(
        var arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    )");
    
    const int iterations = 50000;
    
    // Measure method calls that access elements
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        engine.execute("arr.front();");
    }
    auto end = high_resolution_clock::now();
    
    auto methodDuration = duration_cast<microseconds>(end - start).count();
    
    // Measure direct subscript access
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        engine.execute("arr[0];");
    }
    end = high_resolution_clock::now();
    
    auto subscriptDuration = duration_cast<microseconds>(end - start).count();
    
    std::cout << "\nMethod lookup overhead comparison:\n";
    std::cout << "  front() method: " << methodDuration << " µs for " << iterations << " calls\n";
    std::cout << "  arr[0] access:  " << subscriptDuration << " µs for " << iterations << " calls\n";
    std::cout << "  Method overhead: " << ((double)methodDuration / subscriptDuration - 1) * 100 << "%\n";
    
    expect_true(true);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()