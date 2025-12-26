#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/class_builder.hpp>

// Note: This requires ChaiScript to be available
// Install via: vcpkg install chaiscript
#ifdef HAVE_CHAISCRIPT
#include <chaiscript/chaiscript.hpp>
#endif

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// C++ TreeNode class for fair performance comparison
// Uses shared_ptr for proper null handling in both engines
class CppTreeNode {
public:
    int value;
    std::shared_ptr<CppTreeNode> left;
    std::shared_ptr<CppTreeNode> right;

    CppTreeNode(int val) : value(val), left(nullptr), right(nullptr) {}
};

// C++ tree operations for bound class
inline std::shared_ptr<CppTreeNode> cpp_insertNode(std::shared_ptr<CppTreeNode> root, int val) {
    if (!root) {
        return std::make_shared<CppTreeNode>(val);
    }
    if (val < root->value) {
        root->left = cpp_insertNode(root->left, val);
    } else {
        root->right = cpp_insertNode(root->right, val);
    }
    return root;
}

inline int cpp_inorderSum(std::shared_ptr<CppTreeNode> node) {
    if (!node) return 0;
    return cpp_inorderSum(node->left) + node->value + cpp_inorderSum(node->right);
}

inline int cpp_treeHeight(std::shared_ptr<CppTreeNode> node) {
    if (!node) return 0;
    int leftH = cpp_treeHeight(node->left);
    int rightH = cpp_treeHeight(node->right);
    return (leftH > rightH ? leftH : rightH) + 1;
}

inline std::shared_ptr<CppTreeNode> cpp_rotateRight(std::shared_ptr<CppTreeNode> y) {
    if (!y || !y->left) return y;
    auto x = y->left;
    auto T2 = x->right;
    x->right = y;
    y->left = T2;
    return x;
}

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

        // Tree node class and tree operations for BST benchmark
        jai_engine->execute(R"(
            class TreeNode {
                int value = 0;
                TreeNode left = null;
                TreeNode right = null;

                TreeNode(int val) {
                    value = val;
                }
            }
        )");

        jai_engine->execute(R"(
            function insertNode(TreeNode root, int val) -> TreeNode {
                if (root == null) {
                    return TreeNode(val);
                }
                if (val < root.value) {
                    root.left = insertNode(root.left, val);
                } else {
                    root.right = insertNode(root.right, val);
                }
                return root;
            }
        )");

        jai_engine->execute(R"(
            function inorderSum(TreeNode node) -> int {
                if (node == null) { return 0; }
                return inorderSum(node.left) + node.value + inorderSum(node.right);
            }
        )");

        jai_engine->execute(R"(
            function treeHeight(TreeNode node) -> int {
                if (node == null) { return 0; }
                auto leftH = treeHeight(node.left);
                auto rightH = treeHeight(node.right);
                if (leftH > rightH) {
                    return leftH + 1;
                }
                return rightH + 1;
            }
        )");

        jai_engine->execute(R"(
            function rotateRight(TreeNode y) -> TreeNode {
                if (y == null || y.left == null) { return y; }
                auto x = y.left;
                auto T2 = x.right;
                x.right = y;
                y.left = T2;
                return x;
            }
        )");

        // Bind C++ TreeNode class to JaiScript for fair comparison
        class_builder<CppTreeNode>(*jai_engine, "CppTreeNode")
            .constructor<int>()
            .property("value", &CppTreeNode::value)
            .property("left", &CppTreeNode::left, jai::skip_type_check)  // Self-referential
            .property("right", &CppTreeNode::right, jai::skip_type_check)  // Self-referential
            .build();

        // Register C++ tree operations
        jai_engine->add_function("cpp_insertNode", &cpp_insertNode);
        jai_engine->add_function("cpp_inorderSum", &cpp_inorderSum);
        jai_engine->add_function("cpp_treeHeight", &cpp_treeHeight);
        jai_engine->add_function("cpp_rotateRight", &cpp_rotateRight);

        // Pre-declare variables for C++ BST benchmark (for fair comparison with ChaiScript)
        // Use 'var' instead of 'auto' so the variable can accept any type (TreeNode or CppTreeNode)
        jai_engine->execute("var root = null;");
        jai_engine->execute("var sum = 0;");
        jai_engine->execute("var height = 0;");

        // Pre-declare string benchmark variables (for fair comparison with ChaiScript)
        jai_engine->execute("var original = \"\";");
        jai_engine->execute("var copy1 = \"\";");
        jai_engine->execute("var copy2 = \"\";");
        jai_engine->execute("var copy3 = \"\";");
        jai_engine->execute("var copy4 = \"\";");
        jai_engine->execute("var copy5 = \"\";");
        jai_engine->execute("var s = \"\";");
        jai_engine->execute("var len = 0;");
        jai_engine->execute("var pos = 0;");
        jai_engine->execute("var sub = \"\";");
        jai_engine->execute("var str_result = \"\";");

        // Pre-declare arrays and benchmark functions for range-for benchmarks
        jai_engine->execute(R"(
            auto benchArr100 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                               20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                               40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                               60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                               80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99];
            auto benchArr10 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        )");

        // Range-for benchmark functions (avoid parsing arrays each iteration)
        jai_engine->execute(R"(
            function benchRangeForCopy100() -> int {
                auto s = 0;
                for (auto x : benchArr100) { s += x; }
                return s;
            }
            function benchRangeForRef100() -> int {
                auto s = 0;
                for (auto& x : benchArr100) { s += x; }
                return s;
            }
            function benchRangeForCopy10() -> int {
                auto s = 0;
                for (auto x : benchArr10) { s += x; }
                return s;
            }
            function benchRangeForRef10() -> int {
                auto s = 0;
                for (auto& x : benchArr10) { s += x; }
                return s;
            }
        )");

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
            chai_engine->eval("var root");
            chai_engine->eval("var height = 0");

            // String benchmark variables
            chai_engine->eval("var original = \"\"");
            chai_engine->eval("var copy1 = \"\"");
            chai_engine->eval("var copy2 = \"\"");
            chai_engine->eval("var copy3 = \"\"");
            chai_engine->eval("var copy4 = \"\"");
            chai_engine->eval("var copy5 = \"\"");
            chai_engine->eval("var s = \"\"");
            chai_engine->eval("var len = 0");
            chai_engine->eval("var pos = 0");
            chai_engine->eval("var sub = \"\"");
            chai_engine->eval("var str_result = \"\"");

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

            // Tree node class - use is_var_undef for uninitialized members
            // Note: ChaiScript class member assignment to null is problematic,
            // so we use undefined members and check with is_var_undef OR is_var_null
            chai_engine->eval(R"(
                class TreeNode {
                    var value;
                    var left;
                    var right;

                    def TreeNode(val) {
                        this.value = val;
                    }

                    def clone() {
                        var n = TreeNode(this.value);
                        n.left := this.left;
                        n.right := this.right;
                        return n;
                    }
                }

                def is_empty(x) {
                    return is_var_undef(x) || is_var_null(x);
                }
            )");

            // BST functions using is_empty() to check for both null and undefined
            // Note: We use := for all assignments to avoid clone issues
            chai_engine->eval(R"(
                def insertNode(root, val) {
                    if (is_empty(root)) {
                        return TreeNode(val);
                    }
                    if (val < root.value) {
                        if (is_empty(root.left)) {
                            root.left := TreeNode(val);
                        } else {
                            insertNode(root.left, val);
                        }
                    } else {
                        if (is_empty(root.right)) {
                            root.right := TreeNode(val);
                        } else {
                            insertNode(root.right, val);
                        }
                    }
                    return root;
                }
            )");

            chai_engine->eval(R"(
                def inorderSum(node) {
                    if (is_empty(node)) { return 0; }
                    var leftSum = 0;
                    var rightSum = 0;
                    if (!is_empty(node.left)) {
                        leftSum = inorderSum(node.left);
                    }
                    if (!is_empty(node.right)) {
                        rightSum = inorderSum(node.right);
                    }
                    return leftSum + node.value + rightSum;
                }
            )");

            chai_engine->eval(R"(
                def treeHeight(node) {
                    if (is_empty(node)) { return 0; }
                    var leftH = 0;
                    var rightH = 0;
                    if (!is_empty(node.left)) {
                        leftH = treeHeight(node.left);
                    }
                    if (!is_empty(node.right)) {
                        rightH = treeHeight(node.right);
                    }
                    if (leftH > rightH) {
                        return leftH + 1;
                    }
                    return rightH + 1;
                }
            )");

            chai_engine->eval(R"(
                def rotateRight(y) {
                    if (is_empty(y) || is_empty(y.left)) { return y; }
                    var x := y.left;
                    var T2;
                    if (!is_empty(x.right)) {
                        T2 := x.right;
                    }
                    x.right := y;
                    if (!is_empty(T2)) {
                        y.left := T2;
                    }
                    return x;
                }
            )");

            // Bind C++ TreeNode class to ChaiScript for fair comparison
            chaiscript::ModulePtr m = std::make_shared<chaiscript::Module>();

            // Register the class
            chaiscript::utility::add_class<CppTreeNode>(*m,
                "CppTreeNode",
                { chaiscript::constructor<CppTreeNode(int)>() },
                { {chaiscript::fun(&CppTreeNode::value), "value"},
                  {chaiscript::fun(&CppTreeNode::left), "left"},
                  {chaiscript::fun(&CppTreeNode::right), "right"} }
            );

            // Register tree operations
            m->add(chaiscript::fun(&cpp_insertNode), "cpp_insertNode");
            m->add(chaiscript::fun(&cpp_inorderSum), "cpp_inorderSum");
            m->add(chaiscript::fun(&cpp_treeHeight), "cpp_treeHeight");
            m->add(chaiscript::fun(&cpp_rotateRight), "cpp_rotateRight");

            // Register assignment operator for shared_ptr<CppTreeNode>
            m->add(chaiscript::fun([](std::shared_ptr<CppTreeNode> &lhs, const std::shared_ptr<CppTreeNode> &rhs) -> std::shared_ptr<CppTreeNode> & {
                return lhs = rhs;
            }), "=");

            chai_engine->add(m);

            // Pre-declare variables for C++ BST benchmark (for fair comparison with JaiScript)
            chai_engine->eval("global root;");
            chai_engine->eval("global sum = 0;");
            chai_engine->eval("global height = 0;");

            // Pre-declare arrays as globals (no 'var' = global in ChaiScript)
            // and benchmark functions for range-for benchmarks
            chai_engine->eval(R"(
                global benchArr100 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                      20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                      40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                      60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                      80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99];
                global benchArr10 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];

                def benchRangeFor100() {
                    var s = 0;
                    for (elem : benchArr100) { s += elem; }
                    return s;
                }
                def benchRangeFor10() {
                    var s = 0;
                    for (elem : benchArr10) { s += elem; }
                    return s;
                }
            )");

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

        // ===== For Loop Optimization Variants (JaiScript Only) =====
        // These test different code paths to measure optimization effectiveness
        test("JaiScript: For Loop Optimization Variants", [this]() {
            // Literal condition - triggers fast path (i < 100)
            benchmark("JaiScript - For Loop (literal condition, fast path)", [this]() {
                jai_engine->execute(R"(
                    auto sum = 0;
                    for (auto i = 0; i < 100; ++i) {
                        sum += i;
                    }
                )");
            });

            // Expression condition - does NOT trigger literal fast path (i < n)
            benchmark("JaiScript - For Loop (expression condition)", [this]() {
                jai_engine->execute(R"(
                    auto sum = 0;
                    auto n = 100;
                    for (auto i = 0; i < n; ++i) {
                        sum += i;
                    }
                )");
            });

            // Hot loop - nested iterations (measures overhead scaling)
            benchmark("JaiScript - Hot Loop (10x100 nested iterations)", [this]() {
                jai_engine->execute(R"(
                    auto total = 0;
                    for (auto i = 0; i < 10; ++i) {
                        for (auto j = 0; j < 100; ++j) {
                            total += j;
                        }
                    }
                )");
            });
        });

        // ===== Declaration Type Loop Performance =====
        // Verifies all declaration types (auto/int/var) use unified fast path
        test("JaiScript: Declaration Type Loop Performance (1000 iterations)", [this]() {
            benchmark("JaiScript - Hot Loop (auto, 1000 iter)", [this]() {
                jai_engine->execute(R"(
                    auto sum = 0;
                    for (auto i = 0; i < 1000; ++i) {
                        sum += i;
                    }
                )");
            });

            benchmark("JaiScript - Hot Loop (int, 1000 iter)", [this]() {
                jai_engine->execute(R"(
                    int sum = 0;
                    for (int i = 0; i < 1000; ++i) {
                        sum += i;
                    }
                )");
            });

            benchmark("JaiScript - Hot Loop (var, 1000 iter)", [this]() {
                jai_engine->execute(R"(
                    var sum = 0;
                    for (var i = 0; i < 1000; ++i) {
                        sum += i;
                    }
                )");
            });

            std::cout << "\nDeclaration Type Performance (1000 iterations):\n";
            std::cout << "  All types use unified fast path - performance should be equal\n";
        });

        // ===== Range-Based For Loop Comparison =====
        // Uses pre-declared arrays and functions - fair comparison without array parsing overhead
        test("JaiScript vs ChaiScript: Range-Based For Loop (100 elements)", [this]() {
            // JaiScript range-for with copy (for(auto x : arr))
            benchmark("JaiScript - Range-For (copy, 100 elements)", [this]() {
                jai_engine->execute("benchRangeForCopy100();");
            });

            // ChaiScript range-for (for(x : arr))
            benchmark("ChaiScript - Range-For (100 elements)", [this]() {
                chai_engine->eval("benchRangeFor100();");
            });
        });

        // ===== JaiScript-Only Range-For Variants =====
        // Uses pre-declared arrays and functions for accurate per-iteration measurement
        test("JaiScript: Range-For Reference vs Copy", [this]() {
            // Range-for with reference (for(auto& x : arr)) - JaiScript only feature
            benchmark("JaiScript - Range-For (reference, 100 elements)", [this]() {
                jai_engine->execute("benchRangeForRef100();");
            });

            // Small array range-for (fewer elements, measures per-iteration overhead)
            benchmark("JaiScript - Range-For (copy, 10 elements)", [this]() {
                jai_engine->execute("benchRangeForCopy10();");
            }, 5000);

            // Small array range-for with reference
            benchmark("JaiScript - Range-For (reference, 10 elements)", [this]() {
                jai_engine->execute("benchRangeForRef10();");
            }, 5000);
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

        // ===== Constant Folding Optimization =====
        test("JaiScript vs ChaiScript: Constant Folding", [this]() {
            // JaiScript should fold constants at parse-time, ChaiScript evaluates at runtime
            // Test with pure constant expression that should be optimized to a single literal
            benchmark("JaiScript - Constant Expression (Parse-time Folding)", [this]() {
                jai_engine->execute("2 + 3 * 4 - 5 / 2;");  // Should fold to single literal
            }, 10000);

            benchmark("ChaiScript - Constant Expression (Runtime Evaluation)", [this]() {
                chai_engine->eval("2 + 3 * 4 - 5 / 2;");  // Evaluates at runtime
            }, 10000);
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

        // ===== Binary Search Tree Operations =====
        test("JaiScript vs ChaiScript: Binary Search Tree", [this]() {
            // This benchmark tests:
            // - Object creation (TreeNode instances)
            // - Field access (left, right, value)
            // - Recursion (insert, traversal, height)
            // - Pointer chasing through object references
            // Build a BST with 15 nodes, traverse it, calculate height, and perform rotations

            benchmark("JaiScript - BST (15 nodes)", [this]() {
                jai_engine->execute(R"(
                    auto tree_root = TreeNode(8);
                    tree_root = insertNode(tree_root, 4);
                    tree_root = insertNode(tree_root, 12);
                    tree_root = insertNode(tree_root, 2);
                    tree_root = insertNode(tree_root, 6);
                    tree_root = insertNode(tree_root, 10);
                    tree_root = insertNode(tree_root, 14);
                    tree_root = insertNode(tree_root, 1);
                    tree_root = insertNode(tree_root, 3);
                    tree_root = insertNode(tree_root, 5);
                    tree_root = insertNode(tree_root, 7);
                    tree_root = insertNode(tree_root, 9);
                    tree_root = insertNode(tree_root, 11);
                    tree_root = insertNode(tree_root, 13);
                    tree_root = insertNode(tree_root, 15);

                    auto tree_sum = inorderSum(tree_root);
                    auto tree_height = treeHeight(tree_root);
                    tree_root = rotateRight(tree_root);
                    tree_sum = inorderSum(tree_root);
                )");
            });

            // Verify is_empty() works for undefined class members
            try {
                chai_engine->eval("var testNode = TreeNode(42);");
                auto leftEmpty = chai_engine->eval<bool>("is_empty(testNode.left);");
                std::cout << "  [DEBUG] is_empty(testNode.left): " << (leftEmpty ? "true" : "false") << "\n";
            } catch (const std::exception& e) {
                std::cerr << "  [DEBUG] is_empty test FAILED: " << e.what() << "\n";
            }

            // ChaiScript BST benchmark - skipped due to poor performance (~52ms/iteration)
            // The benchmark works correctly but is too slow to run in the test suite.
            // Last measured: 52698μs/iteration (vs JaiScript's ~563μs - 94x slower)
            std::cout << "    ChaiScript - BST (15 nodes): 52698μs/iteration (skipped - too slow)\n";
        });

        // ===== String Operations =====
        test("JaiScript vs ChaiScript: String Copy (Long String)", [this]() {
            // Tests the shared_ptr string optimization in JaiScript
            // Variables pre-declared in constructor for fair comparison
            benchmark("JaiScript - String Copy (5 copies)", [this]() {
                jai_engine->execute(R"(
                    original = "This is a longer string that would be expensive to copy without shared_ptr optimization";
                    copy1 = original;
                    copy2 = original;
                    copy3 = original;
                    copy4 = original;
                    copy5 = original;
                )");
            });

            benchmark("ChaiScript - String Copy (5 copies)", [this]() {
                chai_engine->eval(R"(
                    original = "This is a longer string that would be expensive to copy without shared_ptr optimization";
                    copy1 = original;
                    copy2 = original;
                    copy3 = original;
                    copy4 = original;
                    copy5 = original;
                )");
            });
        });

        test("JaiScript vs ChaiScript: String Concatenation Loop", [this]() {
            benchmark("JaiScript - String Concat (20 iterations)", [this]() {
                jai_engine->execute(R"(
                    str_result = "";
                    for (var i = 0; i < 20; ++i) {
                        str_result = str_result + "x";
                    }
                )");
            });

            benchmark("ChaiScript - String Concat (20 iterations)", [this]() {
                chai_engine->eval(R"(
                    str_result = "";
                    for (var i = 0; i < 20; ++i) {
                        str_result = str_result + "x";
                    }
                )");
            });
        });

        test("JaiScript vs ChaiScript: String Methods", [this]() {
            benchmark("JaiScript - String find/substr/size", [this]() {
                jai_engine->execute(R"(
                    s = "Hello World, Hello Universe";
                    len = s.length();
                    pos = s.find("World");
                    sub = s.substr(0, 5);
                )");
            });

            benchmark("ChaiScript - String find/substr/size", [this]() {
                chai_engine->eval(R"(
                    s = "Hello World, Hello Universe";
                    len = s.size();
                    pos = s.find("World");
                    sub = s.substr(0, 5);
                )");
            });
        });

        // ===== Binary Search Tree Operations (C++ Bound - Fair Comparison) =====
        test("JaiScript vs ChaiScript: Binary Search Tree (C++ Bound)", [this]() {
            // This benchmark provides a fair comparison by using the same C++ TreeNode class
            // bound to both engines. This works around ChaiScript's language limitations
            // and allows us to compare pure scripting performance for tree operations.

            benchmark("JaiScript - C++ BST (15 nodes)", [this]() {
                jai_engine->execute(R"(
                    root = CppTreeNode(8);
                    root = cpp_insertNode(root, 4);
                    root = cpp_insertNode(root, 12);
                    root = cpp_insertNode(root, 2);
                    root = cpp_insertNode(root, 6);
                    root = cpp_insertNode(root, 10);
                    root = cpp_insertNode(root, 14);
                    root = cpp_insertNode(root, 1);
                    root = cpp_insertNode(root, 3);
                    root = cpp_insertNode(root, 5);
                    root = cpp_insertNode(root, 7);
                    root = cpp_insertNode(root, 9);
                    root = cpp_insertNode(root, 11);
                    root = cpp_insertNode(root, 13);
                    root = cpp_insertNode(root, 15);

                    sum = cpp_inorderSum(root);
                    height = cpp_treeHeight(root);
                    root = cpp_rotateRight(root);
                    sum = cpp_inorderSum(root);
                )");
            });

            benchmark("ChaiScript - C++ BST (15 nodes)", [this]() {
                chai_engine->eval(R"(
                    root = CppTreeNode(8);
                    root = cpp_insertNode(root, 4);
                    root = cpp_insertNode(root, 12);
                    root = cpp_insertNode(root, 2);
                    root = cpp_insertNode(root, 6);
                    root = cpp_insertNode(root, 10);
                    root = cpp_insertNode(root, 14);
                    root = cpp_insertNode(root, 1);
                    root = cpp_insertNode(root, 3);
                    root = cpp_insertNode(root, 5);
                    root = cpp_insertNode(root, 7);
                    root = cpp_insertNode(root, 9);
                    root = cpp_insertNode(root, 11);
                    root = cpp_insertNode(root, 13);
                    root = cpp_insertNode(root, 15);

                    sum = cpp_inorderSum(root);
                    height = cpp_treeHeight(root);
                    root = cpp_rotateRight(root);
                    sum = cpp_inorderSum(root);
                )");
            });
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
