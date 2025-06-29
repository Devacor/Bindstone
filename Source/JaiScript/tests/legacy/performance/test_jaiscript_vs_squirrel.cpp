#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <squirrel.h>
#include <sqstdaux.h>
#include <sqstdmath.h>
#include <sqstdstring.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstring>

using namespace jai;
using namespace std::chrono;

// Squirrel helper functions
void sq_compile_error_handler(HSQUIRRELVM v, const SQChar* desc, const SQChar* source, 
                              SQInteger line, SQInteger column) {
    std::cerr << "Squirrel compile error: " << desc << " at " << source 
              << ":" << line << ":" << column << std::endl;
}

bool sq_compile_and_run(HSQUIRRELVM v, const char* script) {
    if (SQ_FAILED(sq_compilebuffer(v, script, strlen(script), "test", SQTrue))) {
        return false;
    }
    
    sq_pushroottable(v);
    if (SQ_FAILED(sq_call(v, 1, SQFalse, SQTrue))) {
        sq_pop(v, 1); // remove closure
        return false;
    }
    sq_pop(v, 1); // remove closure
    return true;
}

// Benchmark function
template<typename Func>
double benchmark(const std::string& name, Func f, int iterations = 1) {
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        f();
    }
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    return static_cast<double>(duration) / iterations;
}

// Print comparison results
void print_comparison(const std::string& test_name, double jai_time, double sq_time) {
    double speedup = sq_time / jai_time;
    std::cout << std::left << std::setw(30) << test_name << ": "
              << "JaiScript: " << std::setw(10) << std::fixed << std::setprecision(2) << jai_time << " µs, "
              << "Squirrel: " << std::setw(10) << std::fixed << std::setprecision(2) << sq_time << " µs, "
              << "Speedup: " << std::setw(6) << std::fixed << std::setprecision(2) << speedup << "x"
              << std::endl;
}

int main() {
    std::cout << "=== JaiScript vs Squirrel Performance Comparison ===" << std::endl;
    std::cout << std::endl;

    // Test 1: engine/VM Creation
    {
        double jai_time = benchmark("JaiScript engine Creation", []() {
            engine eng;
        }, 100);

        double sq_time = benchmark("Squirrel VM Creation", []() {
            HSQUIRRELVM v = sq_open(1024);
            sq_setcompilererrorhandler(v, sq_compile_error_handler);
            sq_close(v);
        }, 100);

        print_comparison("engine/VM Creation", jai_time, sq_time);
    }

    // Test 2: Simple Arithmetic
    {
        engine jai_engine;
        HSQUIRRELVM sq_vm = sq_open(1024);
        sq_setcompilererrorhandler(sq_vm, sq_compile_error_handler);
        sqstd_register_mathlib(sq_vm);

        const char* sq_script = "local result = 0; for(local i = 0; i < 1000; i++) { result = result + i * 2; }";
        const char* jai_script = "auto result = 0; for (int i = 0; i < 1000; ++i) { result = result + i * 2; }";

        double jai_time = benchmark("JaiScript Arithmetic", [&]() {
            jai_engine.execute(jai_script);
        }, 100);

        double sq_time = benchmark("Squirrel Arithmetic", [&]() {
            sq_compile_and_run(sq_vm, sq_script);
        }, 100);

        print_comparison("Simple Arithmetic", jai_time, sq_time);
        sq_close(sq_vm);
    }

    // Test 3: Variable Assignment
    {
        engine jai_engine;
        HSQUIRRELVM sq_vm = sq_open(1024);
        sq_setcompilererrorhandler(sq_vm, sq_compile_error_handler);

        const char* sq_script = R"(
            local a = 10;
            local b = 20;
            local c = a + b;
            local d = c * 2;
            local e = d - a;
        )";

        const char* jai_script = R"(
            auto a = 10;
            auto b = 20;
            auto c = a + b;
            auto d = c * 2;
            auto e = d - a;
        )";

        double jai_time = benchmark("JaiScript Variables", [&]() {
            jai_engine.execute(jai_script);
        }, 1000);

        double sq_time = benchmark("Squirrel Variables", [&]() {
            sq_compile_and_run(sq_vm, sq_script);
        }, 1000);

        print_comparison("Variable Assignment", jai_time, sq_time);
        sq_close(sq_vm);
    }

    // Test 4: Function Definition and Call
    {
        engine jai_engine;
        HSQUIRRELVM sq_vm = sq_open(1024);
        sq_setcompilererrorhandler(sq_vm, sq_compile_error_handler);

        const char* sq_script = R"(
            function add(a, b) {
                return a + b;
            }
            local result = add(5, 3);
        )";

        const char* jai_script = R"(
            function add(int a, int b) {
                return a + b;
            }
            auto result = add(5, 3);
        )";

        double jai_time = benchmark("JaiScript Function", [&]() {
            jai_engine.execute(jai_script);
        }, 1000);

        double sq_time = benchmark("Squirrel Function", [&]() {
            sq_compile_and_run(sq_vm, sq_script);
        }, 1000);

        print_comparison("Function Call", jai_time, sq_time);
        sq_close(sq_vm);
    }

    // Test 5: Loop Performance
    {
        engine jai_engine;
        HSQUIRRELVM sq_vm = sq_open(1024);
        sq_setcompilererrorhandler(sq_vm, sq_compile_error_handler);

        const char* sq_script = R"(
            local sum = 0;
            for (local i = 0; i < 100; i++) {
                for (local j = 0; j < 100; j++) {
                    sum = sum + 1;
                }
            }
        )";

        const char* jai_script = R"(
            auto sum = 0;
            for (int i = 0; i < 100; ++i) {
                for (int j = 0; j < 100; ++j) {
                    sum = sum + 1;
                }
            }
        )";

        double jai_time = benchmark("JaiScript Loops", [&]() {
            jai_engine.execute(jai_script);
        }, 10);

        double sq_time = benchmark("Squirrel Loops", [&]() {
            sq_compile_and_run(sq_vm, sq_script);
        }, 10);

        print_comparison("Nested Loops", jai_time, sq_time);
        sq_close(sq_vm);
    }

    // Test 6: Array Operations
    {
        engine jai_engine;
        HSQUIRRELVM sq_vm = sq_open(1024);
        sq_setcompilererrorhandler(sq_vm, sq_compile_error_handler);

        const char* sq_script = R"(
            local arr = [];
            for (local i = 0; i < 100; i++) {
                arr.append(i);
            }
            local sum = 0;
            foreach (val in arr) {
                sum = sum + val;
            }
        )";

        const char* jai_script = R"(
            auto arr = [];
            for (int i = 0; i < 100; ++i) {
                arr.push(i);
            }
            auto sum = 0;
            for (int i = 0; i < arr.size(); ++i) {
                sum = sum + arr[i];
            }
        )";

        double jai_time = benchmark("JaiScript Arrays", [&]() {
            jai_engine.execute(jai_script);
        }, 100);

        double sq_time = benchmark("Squirrel Arrays", [&]() {
            sq_compile_and_run(sq_vm, sq_script);
        }, 100);

        print_comparison("Array Operations", jai_time, sq_time);
        sq_close(sq_vm);
    }

    // Test 7: Object/Table Creation
    {
        engine jai_engine;
        HSQUIRRELVM sq_vm = sq_open(1024);
        sq_setcompilererrorhandler(sq_vm, sq_compile_error_handler);

        const char* sq_script = R"(
            local obj = {
                x = 10,
                y = 20,
                sum = function() {
                    return this.x + this.y;
                }
            };
            local result = obj.sum();
        )";

        const char* jai_script = R"(
            auto obj = {
                {"x", 10},
                {"y", 20}
            };
            auto result = obj["x"] + obj["y"];
        )";

        double jai_time = benchmark("JaiScript Objects", [&]() {
            jai_engine.execute(jai_script);
        }, 1000);

        double sq_time = benchmark("Squirrel Objects", [&]() {
            sq_compile_and_run(sq_vm, sq_script);
        }, 1000);

        print_comparison("Object/Table Creation", jai_time, sq_time);
        sq_close(sq_vm);
    }

    std::cout << std::endl;
    std::cout << "Note: Lower times are better. Speedup > 1.0 means JaiScript is faster." << std::endl;

    return 0;
}