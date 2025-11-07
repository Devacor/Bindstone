#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

// Note: This requires ChaiScript to be available
// Install via: vcpkg install chaiscript
#ifdef HAVE_CHAISCRIPT
#include <chaiscript/chaiscript.hpp>
#endif

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class chaiscript_comparison : public suite {
public:
    chaiscript_comparison() : suite("ChaiScript Performance Comparison") {
        // Create engines once during construction, not for every test
        std::cout << "\n==============================================\n";
        std::cout << "Initializing engines for comparison benchmarks...\n";
        std::cout << "==============================================\n";

        // Create JaiScript engine
        std::cout << "Creating JaiScript engine...\n";
        jai_engine = engine::make();
        jai::stdlib::register_all(jai_engine);
        std::cout << "JaiScript engine ready.\n";

#ifdef HAVE_CHAISCRIPT
        // Create ChaiScript engine with standard library
        std::cout << "Creating ChaiScript engine...\n";
        try {
            // Create ChaiScript engine (default constructor includes stdlib)
            chai_engine = std::make_shared<chaiscript::ChaiScript>();

            // Test basic execution to verify it works
            auto result = chai_engine->eval<int>("42");
            if (result != 42) {
                throw std::runtime_error("ChaiScript basic eval test failed");
            }
            std::cout << "ChaiScript engine ready.\n";
        } catch (const std::exception& e) {
            std::cerr << "ERROR: ChaiScript initialization failed: " << e.what() << "\n";
            std::cerr << "ChaiScript comparison benchmarks will be skipped.\n";
            chai_engine.reset();
        } catch (...) {
            std::cerr << "ERROR: ChaiScript initialization failed with unknown exception\n";
            std::cerr << "ChaiScript comparison benchmarks will be skipped.\n";
            chai_engine.reset();
        }
#else
        std::cout << "ChaiScript not available - comparison benchmarks disabled.\n";
#endif

        std::cout << "==============================================\n\n";

        // Pre-declare functions and classes in BOTH engines ONCE during construction
        // This way we only measure execution overhead, not parsing/declaration overhead

        // JaiScript declarations - pre-declare functions and classes for fair comparison
        jai_engine->execute("function add(auto a, auto b) -> auto { return a + b; }");
        jai_engine->execute(R"(
            class Point {
                float x = 0.0;
                float y = 0.0;
                Point(float px, float py) {
                    x = px;
                    y = py;
                }
            }
        )");
        jai_engine->execute(R"(
            class Calculator {
                int add(int a, int b) {
                    return a + b;
                }
            }
        )");

        // Algorithm function declarations for JaiScript
        jai_engine->execute("function factorial(auto n) -> auto { if (n <= 1) { return 1; } return n * factorial(n - 1); }");
        jai_engine->execute(R"(
            function binarySearch(auto arr, auto target, auto left, auto right) -> auto {
                if (left > right) { return -1; }
                auto mid = (left + right) / 2;
                auto val = arr[mid];
                if (val == target) { return mid; }
                if (val > target) { return binarySearch(arr, target, left, mid - 1); }
                return binarySearch(arr, target, mid + 1, right);
            }
        )");
        jai_engine->execute(R"(
            function bubbleSort(auto arr) -> auto {
                auto n = arr.size();
                for (auto i = 0; i < n; i = i + 1) {
                    for (auto j = 0; j < n - i - 1; j = j + 1) {
                        if (arr[j] > arr[j + 1]) {
                            auto temp = arr[j];
                            arr[j] = arr[j + 1];
                            arr[j + 1] = temp;
                        }
                    }
                }
                return arr;
            }
        )");
        jai_engine->execute("function fib(auto n) -> auto { if (n <= 1) { return n; } return fib(n - 1) + fib(n - 2); }");

#ifdef HAVE_CHAISCRIPT
        // ChaiScript declarations - pre-declare functions, classes, and variables ONCE in constructor
        try {
            // Pre-declare all variables that benchmarks will use
            chai_engine->eval("var x = 0");
            chai_engine->eval("var y = 0");
            chai_engine->eval("var z = 0");
            chai_engine->eval("var a = 0");
            chai_engine->eval("var b = 0");
            chai_engine->eval("var c = 0");
            chai_engine->eval("var result = 0");
            chai_engine->eval("var sum = 0");
            chai_engine->eval("var arr = []");
            chai_engine->eval("var m = Map()");
            chai_engine->eval("var val = 0");
            chai_engine->eval("var p");
            chai_engine->eval("var calc");
            chai_engine->eval("var testArr = []");
            chai_engine->eval("var unsorted = []");

            // Declare add function for Function Calls benchmark
            chai_engine->eval("def add(a, b) { return a + b; }");

            // Declare Point class for Class Creation benchmark
            chai_engine->eval(R"(
                class Point {
                    var x;
                    var y;
                    def Point(px, py) {
                        this.x = px;
                        this.y = py;
                    }
                }
            )");

            // Declare Calculator class for Method Invocation benchmark
            chai_engine->eval(R"(
                class Calculator {
                    def Calculator() {
                        // Default constructor
                    }
                    def add(a, b) {
                        return a + b;
                    }
                }
            )");

            // Algorithm function declarations for ChaiScript
            chai_engine->eval("def factorial(n) { if (n <= 1) { return 1; } return n * factorial(n - 1); }");
            chai_engine->eval(R"(
                def binarySearch(arr, target, left, right) {
                    if (left > right) { return -1; }
                    var mid = (left + right) / 2;
                    var val = arr[mid];
                    if (val == target) { return mid; }
                    if (val > target) { return binarySearch(arr, target, left, mid - 1); }
                    return binarySearch(arr, target, mid + 1, right);
                }
            )");
            chai_engine->eval(R"(
                def bubbleSort(arr) {
                    var n = arr.size();
                    for (var i = 0; i < n; ++i) {
                        for (var j = 0; j < n - i - 1; ++j) {
                            if (arr[j] > arr[j + 1]) {
                                var temp = arr[j];
                                arr[j] = arr[j + 1];
                                arr[j + 1] = temp;
                            }
                        }
                    }
                    return arr;
                }
            )");
            chai_engine->eval("def fib(n) { if (n <= 1) { return n; } return fib(n - 1) + fib(n - 2); }");

        } catch (const std::exception& e) {
            std::cerr << "ChaiScript constructor error: " << e.what() << std::endl;
        }
#endif
    }

    std::shared_ptr<jai::engine> jai_engine;
#ifdef HAVE_CHAISCRIPT
    std::shared_ptr<chaiscript::ChaiScript> chai_engine;
#endif

    void forge_tests() override {
#ifdef HAVE_CHAISCRIPT
        // Check if ChaiScript initialized successfully
        if (!chai_engine) {
            test("ChaiScript Initialization Failed", [this]() {
                std::cout << "ChaiScript engine failed to initialize. Skipping comparison benchmarks.\n";
            });
            return;
        }

        // ===== Integer Addition =====
        test("JaiScript vs ChaiScript: Integer Addition", [this]() {
            // Verify both engines work correctly first
            auto jai_result = jai_engine->execute("42 + 58").as<int>();
            auto chai_result = chai_engine->eval<int>("42 + 58");
            if (jai_result != 100 || chai_result != 100) {
                std::cerr << "WARNING: Integer addition test failed! JaiScript=" << jai_result
                         << " ChaiScript=" << chai_result << "\n";
            }

            // Increased iterations to get better metrics (from ~2μs to ~10μs)
            benchmark("JaiScript - Integer Addition", [this]() {
                jai_engine->execute("42 + 58;");
            }, 5000);

            benchmark("ChaiScript - Integer Addition", [this]() {
                chai_engine->eval("42 + 58;");
            }, 5000);
        });

        // ===== Float Multiplication =====
        test("JaiScript vs ChaiScript: Float Multiplication", [this]() {
            // Increased iterations to get better metrics (from ~2μs to ~10μs)
            benchmark("JaiScript - Float Multiplication", [this]() {
                jai_engine->execute("3.14 * 2.71;");
            }, 5000);

            benchmark("ChaiScript - Float Multiplication", [this]() {
                chai_engine->eval("3.14 * 2.71;");
            }, 5000);
        });

        // ===== Variable Operations =====
        test("JaiScript vs ChaiScript: Variable Operations", [this]() {
            benchmark("JaiScript - Variable Operations", [this]() {
                try {
                    jai_engine->execute("auto x = 10; auto y = 20; auto z = x + y;");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** JaiScript Variable Operations ERROR: " << e.what() << std::endl;
                    throw;
                }
            });

            benchmark("ChaiScript - Variable Operations", [this]() {
                try {
                    chai_engine->eval("x = 10; y = 20; z = x + y;");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** ChaiScript Variable Operations ERROR: " << e.what() << std::endl;
                    throw;
                }
            });
        });

        // ===== Function Calls =====
        test("JaiScript vs ChaiScript: Function Calls", [this]() {
            benchmark("JaiScript - Function Calls", [this]() {
                try {
                    jai_engine->execute("add(10, 20);");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** JaiScript Function Calls ERROR: " << e.what() << std::endl;
                    throw;
                }
            });

            benchmark("ChaiScript - Function Calls", [this]() {
                try {
                    chai_engine->eval("add(10, 20);");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** ChaiScript Function Calls ERROR: " << e.what() << std::endl;
                    throw;
                }
            });
        });

        // ===== Array Push/Pop =====
        test("JaiScript vs ChaiScript: Array Push/Pop", [this]() {
            benchmark("JaiScript - Array Push/Pop", [this]() {
                try {
                    jai_engine->execute(R"(
                        auto arr = [];
                        arr.push(1);
                        arr.push(2);
                        arr.push(3);
                        arr.pop();
                        arr.size();
                    )");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** JaiScript Array Push/Pop ERROR: " << e.what() << std::endl;
                    throw;
                }
            });

            benchmark("ChaiScript - Array Push/Pop", [this]() {
                try {
                    chai_engine->eval("arr = []; arr.push_back(1); arr.push_back(2); arr.push_back(3); arr.pop_back(); arr.size();");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** ChaiScript Array Push/Pop ERROR: " << e.what() << std::endl;
                    throw;
                }
            });
        });

        // ===== Map Insert/Lookup =====
        test("JaiScript vs ChaiScript: Map Insert/Lookup", [this]() {
            benchmark("JaiScript - Map Insert/Lookup", [this]() {
                // JaiScript allows redeclaration freely
                jai_engine->execute(R"(
                    auto m = {};
                    m["key1"] = 100;
                    m["key2"] = 200;
                    auto val = m["key1"];
                )");
            });

            benchmark("ChaiScript - Map Insert/Lookup", [this]() {
                try {
                    chai_engine->eval("m = Map(); m[\"key1\"] = 100; m[\"key2\"] = 200; val = m[\"key1\"];");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** ChaiScript Map Insert/Lookup ERROR: " << e.what() << std::endl;
                    throw;
                }
            });
        });

        // ===== Class Creation =====
        test("JaiScript vs ChaiScript: Class Creation", [this]() {
            benchmark("JaiScript - Class Creation", [this]() {
                try {
                    // Point class pre-declared in pre_test(), just instantiate it
                    jai_engine->execute("auto p = Point(3.0, 4.0);");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** JaiScript Class Creation ERROR: " << e.what() << std::endl;
                    throw;
                }
            });

            benchmark("ChaiScript - Class Creation", [this]() {
                try {
                    chai_engine->eval("p = Point(3.0, 4.0);");
                } catch (const std::exception& e) {
                    std::cerr << "\n*** ChaiScript Class Creation ERROR: " << e.what() << std::endl;
                    throw;
                }
            });
        });

        // ===== Method Invocation =====
        test("JaiScript vs ChaiScript: Method Invocation", [this]() {
            benchmark("JaiScript - Method Invocation", [this]() {
                // Calculator class pre-declared in pre_test(), instantiate and call method
                jai_engine->execute("auto calc = Calculator(); calc.add(5, 3);");
            });

            benchmark("ChaiScript - Method Invocation", [this]() {
                // Use 'new' to instantiate ChaiScript classes
                chai_engine->eval("calc = Calculator(); calc.add(5, 3);");
            });
        });

        // ===== For Loop =====
        test("JaiScript vs ChaiScript: For Loop (100 iterations)", [this]() {
            benchmark("JaiScript - For Loop", [this]() {
                // Use optimized ++ and += operators for fair comparison
                jai_engine->execute(R"(
                    auto sum = 0;
                    for (auto i = 0; i < 100; ++i) {
                        sum += i;
                    }
                )");
            });

            benchmark("ChaiScript - For Loop", [this]() {
                // Assign to global - ChaiScript auto-creates on first use
                chai_engine->eval("sum = 0; for (var i = 0; i < 100; ++i) { sum += i; }");
            });
        });

        // ===== Variable Lookup Heavy =====
        test("JaiScript vs ChaiScript: Variable Lookup Heavy", [this]() {
            benchmark("JaiScript - Variable Lookup Heavy", [this]() {
                // JaiScript allows redeclaration freely
                jai_engine->execute(R"(
                    auto a = 1;
                    auto b = 2;
                    auto c = 3;
                    auto result = a + b + c + a + b + c + a + b + c + a;
                )");
            });

            benchmark("ChaiScript - Variable Lookup Heavy", [this]() {
                // Assign to globals - ChaiScript auto-creates on first use
                chai_engine->eval("a = 1; b = 2; c = 3; result = a + b + c + a + b + c + a + b + c + a");
            });
        });

        // ===== Complex Expression =====
        test("JaiScript vs ChaiScript: Complex Expression", [this]() {
            // Increased iterations to get better metrics (from ~4μs to ~20μs)
            benchmark("JaiScript - Complex Expression", [this]() {
                jai_engine->execute("(10 + 20) * (30 - 15) / 5;");
            }, 5000);

            benchmark("ChaiScript - Complex Expression", [this]() {
                chai_engine->eval("(10 + 20) * (30 - 15) / 5;");
            }, 5000);
        });

        // ===== Factorial (Recursion) =====
        test("JaiScript vs ChaiScript: Factorial (Recursion)", [this]() {
            benchmark("JaiScript - Factorial(10)", [this]() {
                jai_engine->execute("factorial(10);");
            });

            benchmark("ChaiScript - Factorial(10)", [this]() {
                chai_engine->eval("factorial(10);");
            });
        });

        // ===== Fibonacci (Deep Recursion) =====
        test("JaiScript vs ChaiScript: Fibonacci (Deep Recursion)", [this]() {
            // Reduced from fib(10) to fib(6) - ChaiScript is extremely slow at high recursion
            benchmark("JaiScript - Fibonacci(6)", [this]() {
                jai_engine->execute("fib(6);");
            });

            benchmark("ChaiScript - Fibonacci(6)", [this]() {
                chai_engine->eval("fib(6);");
            });
        });

        // ===== Binary Search (Recursion + Array Access) =====
        test("JaiScript vs ChaiScript: Binary Search", [this]() {
            benchmark("JaiScript - Binary Search", [this]() {
                jai_engine->execute("auto testArr = [1, 3, 5, 7, 9, 11, 13, 15]; binarySearch(testArr, 11, 0, 7);");
            });

            benchmark("ChaiScript - Binary Search", [this]() {
                // Assign to global - ChaiScript auto-creates on first use
                chai_engine->eval("testArr = [1, 3, 5, 7, 9, 11, 13, 15]; binarySearch(testArr, 11, 0, 7);");
            });
        });

        // ===== Bubble Sort (Nested Loops + Array Manipulation) =====
        test("JaiScript vs ChaiScript: Bubble Sort", [this]() {
            benchmark("JaiScript - Bubble Sort (10 elements)", [this]() {
                jai_engine->execute("auto unsorted = [9, 3, 7, 1, 5, 8, 2, 6, 4, 10]; bubbleSort(unsorted);");
            });

            benchmark("ChaiScript - Bubble Sort (10 elements)", [this]() {
                // Assign to global - ChaiScript auto-creates on first use
                chai_engine->eval("unsorted = [9, 3, 7, 1, 5, 8, 2, 6, 4, 10]; bubbleSort(unsorted);");
            });
        });

        // ===== BoxedValue vs script_value: Integer Construction =====
        test("BoxedValue vs script_value: Integer Construction", [this]() {
            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value Integer Construction", [this]() {
                jai_engine->execute("42;");
            }, 10000);

            benchmark("ChaiScript - BoxedValue Integer Construction", [this]() {
                chai_engine->eval("42;");
            }, 10000);
        });

        // ===== BoxedValue vs script_value: String Construction =====
        test("BoxedValue vs script_value: String Construction", [this]() {
            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value String Construction", [this]() {
                jai_engine->execute("\"Hello, World!\";");
            }, 10000);

            benchmark("ChaiScript - BoxedValue String Construction", [this]() {
                chai_engine->eval("\"Hello, World!\";");
            }, 10000);
        });

        // ===== BoxedValue vs script_value: Boolean Construction =====
        test("BoxedValue vs script_value: Boolean Construction", [this]() {
            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value Boolean Construction", [this]() {
                jai_engine->execute("true;");
            }, 10000);

            benchmark("ChaiScript - BoxedValue Boolean Construction", [this]() {
                chai_engine->eval("true;");
            }, 10000);
        });

        // ===== BoxedValue vs script_value: Float Construction =====
        test("BoxedValue vs script_value: Float Construction", [this]() {
            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value Float Construction", [this]() {
                jai_engine->execute("3.14159;");
            }, 10000);

            benchmark("ChaiScript - BoxedValue Float Construction", [this]() {
                chai_engine->eval("3.14159;");
            }, 10000);
        });

        // ===== BoxedValue vs script_value: Type Checking =====
        test("BoxedValue vs script_value: Type Checking", [this]() {
            // Pre-declare and initialize variables for type checking tests
            jai_engine->execute("auto testInt = 42; auto testStr = \"hello\"; auto testBool = true;");
            chai_engine->eval("var testInt = 42; var testStr = \"hello\"; var testBool = true;");

            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value Type Checking", [this]() {
                jai_engine->execute("testInt; testStr; testBool;");
            }, 5000);

            benchmark("ChaiScript - BoxedValue Type Checking", [this]() {
                chai_engine->eval("testInt; testStr; testBool;");
            }, 5000);
        });

        // ===== BoxedValue vs script_value: Array Construction =====
        test("BoxedValue vs script_value: Array Construction", [this]() {
            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value Array Construction", [this]() {
                jai_engine->execute("[1, 2, 3, 4, 5];");
            }, 5000);

            benchmark("ChaiScript - BoxedValue Array Construction", [this]() {
                chai_engine->eval("[1, 2, 3, 4, 5];");
            }, 5000);
        });

        // ===== BoxedValue vs script_value: Mixed Type Operations =====
        test("BoxedValue vs script_value: Mixed Type Operations", [this]() {
            // Increased iterations to get better metrics
            benchmark("JaiScript - script_value Mixed Types", [this]() {
                jai_engine->execute("auto x = 42; auto y = 3.14; auto z = x + y; auto s = \"Result: \";");
            }, 5000);

            benchmark("ChaiScript - BoxedValue Mixed Types", [this]() {
                // Use assignment to pre-declared variables only (ChaiScript doesn't allow redefinition)
                chai_engine->eval("x = 42; y = 3.14; z = x + y; result = 45;");
            }, 5000);
        });

#else
        test("ChaiScript Not Available", [this]() {
            std::cout << "\n==============================================\n";
            std::cout << "ChaiScript comparison skipped - ChaiScript not found\n";
            std::cout << "To enable: Install ChaiScript via vcpkg and rebuild with -DHAVE_CHAISCRIPT=ON\n";
            std::cout << "  vcpkg install chaiscript\n";
            std::cout << "  cmake -DHAVE_CHAISCRIPT=ON ..\n";
            std::cout << "==============================================\n\n";
        });
#endif
    }
};

} // namespace jai::foundry::tests

// Auto-register this test suite with Foundry
FOUNDRY_REGISTER(jai::foundry::tests::chaiscript_comparison)
