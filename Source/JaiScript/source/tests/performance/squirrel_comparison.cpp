#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/class_builder.hpp>

// Note: This requires Squirrel to be available
// The squirrel source is at: Source/JaiScript/squirrel
#ifdef HAVE_SQUIRREL
#include <squirrel.h>
#include <sqstdio.h>
#include <sqstdaux.h>
#include <sqstdstring.h>
#include <sqstdmath.h>
#include <cstdarg>
#endif

using namespace jai;
using namespace jai::foundry;

// C++ TreeNode class for fair performance comparison (same as ChaiScript comparison)
class CppTreeNode {
public:
    int value;
    std::shared_ptr<CppTreeNode> left;
    std::shared_ptr<CppTreeNode> right;

    CppTreeNode(int val) : value(val), left(nullptr), right(nullptr) {}
};

// C++ BST functions for fair comparison
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
    return 1 + (leftH > rightH ? leftH : rightH);
}

inline std::shared_ptr<CppTreeNode> cpp_rotateRight(std::shared_ptr<CppTreeNode> y) {
    if (!y || !y->left) return y;
    auto x = y->left;
    y->left = x->right;
    x->right = y;
    return x;
}

namespace jai::foundry::tests {

#ifdef HAVE_SQUIRREL
// Helper class to manage Squirrel VM lifecycle
class SquirrelVM {
public:
    HSQUIRRELVM vm;
    std::string last_error;

    SquirrelVM(int stack_size = 1024) {
        vm = sq_open(stack_size);
        sqstd_seterrorhandlers(vm);
        sq_setprintfunc(vm, &SquirrelVM::print_func, &SquirrelVM::error_func);

        // Register standard libraries
        sq_pushroottable(vm);
        sqstd_register_stringlib(vm);
        sqstd_register_mathlib(vm);
        sq_pop(vm, 1);
    }

    ~SquirrelVM() {
        sq_close(vm);
    }

    bool execute(const char* script) {
        sq_pushroottable(vm);
        if (SQ_FAILED(sq_compilebuffer(vm, script, strlen(script), "script", SQTrue))) {
            sq_pop(vm, 1);
            return false;
        }
        sq_push(vm, -2); // Push root table as 'this'
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_pop(vm, 2);
            return false;
        }
        sq_pop(vm, 2);
        return true;
    }

    template<typename T>
    T eval(const char* script);

    static void print_func(HSQUIRRELVM v, const SQChar* s, ...) {
        va_list args;
        va_start(args, s);
        vprintf(s, args);
        va_end(args);
    }

    static void error_func(HSQUIRRELVM v, const SQChar* s, ...) {
        va_list args;
        va_start(args, s);
        vfprintf(stderr, s, args);
        va_end(args);
    }
};

template<>
int SquirrelVM::eval<int>(const char* script) {
    std::string wrapped = std::string("return ") + script + ";";
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, wrapped.c_str(), wrapped.length(), "eval", SQTrue))) {
        sq_pop(vm, 1);
        return 0;
    }
    sq_push(vm, -2);
    if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
        sq_pop(vm, 2);
        return 0;
    }
    SQInteger result = 0;
    sq_getinteger(vm, -1, &result);
    sq_pop(vm, 3);
    return static_cast<int>(result);
}

template<>
float SquirrelVM::eval<float>(const char* script) {
    std::string wrapped = std::string("return ") + script + ";";
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, wrapped.c_str(), wrapped.length(), "eval", SQTrue))) {
        sq_pop(vm, 1);
        return 0.0f;
    }
    sq_push(vm, -2);
    if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
        sq_pop(vm, 2);
        return 0.0f;
    }
    SQFloat result = 0.0f;
    sq_getfloat(vm, -1, &result);
    sq_pop(vm, 3);
    return static_cast<float>(result);
}

// ===== Squirrel C++ Binding for CppTreeNode =====
// Type tag for CppTreeNode (unique identifier)
static SQUserPointer CppTreeNodeTag = (SQUserPointer)"CppTreeNode";

// Release hook - called when Squirrel garbage collects the object
static SQInteger sq_CppTreeNode_release(SQUserPointer ptr, SQInteger size) {
    auto* sp = reinterpret_cast<std::shared_ptr<CppTreeNode>*>(ptr);
    if (sp) {
        delete sp;
    }
    return 0;
}

// Helper to get the shared_ptr from a Squirrel instance
static std::shared_ptr<CppTreeNode> sq_getCppTreeNode(HSQUIRRELVM vm, SQInteger idx) {
    SQUserPointer ptr = nullptr;
    if (SQ_FAILED(sq_getinstanceup(vm, idx, &ptr, nullptr, SQFalse))) {
        return nullptr;
    }
    if (!ptr) return nullptr;
    auto* sp = reinterpret_cast<std::shared_ptr<CppTreeNode>*>(ptr);
    return *sp;
}

// Helper to push a shared_ptr<CppTreeNode> onto the Squirrel stack
static void sq_pushCppTreeNode(HSQUIRRELVM vm, std::shared_ptr<CppTreeNode> node) {
    if (!node) {
        sq_pushnull(vm);
        return;
    }
    // Get the class from the root table
    sq_pushroottable(vm);
    sq_pushstring(vm, "CppTreeNode", -1);
    sq_get(vm, -2);
    sq_createinstance(vm, -1);

    // Store the shared_ptr (heap-allocated)
    auto* sp = new std::shared_ptr<CppTreeNode>(node);
    sq_setinstanceup(vm, -1, sp);
    sq_setreleasehook(vm, -1, sq_CppTreeNode_release);

    // Clean up stack: remove class and root table, keep instance
    sq_remove(vm, -2); // remove class
    sq_remove(vm, -2); // remove root table
}

// Constructor: CppTreeNode(value)
static SQInteger sq_CppTreeNode_constructor(HSQUIRRELVM vm) {
    SQInteger val;
    sq_getinteger(vm, 2, &val);

    auto* sp = new std::shared_ptr<CppTreeNode>(std::make_shared<CppTreeNode>(static_cast<int>(val)));
    sq_setinstanceup(vm, 1, sp);
    sq_setreleasehook(vm, 1, sq_CppTreeNode_release);

    return 0;
}

// Global function: sq_cpp_insertNode(root, val)
static SQInteger sq_cpp_insertNode(HSQUIRRELVM vm) {
    auto root = sq_getCppTreeNode(vm, 2);
    SQInteger val;
    sq_getinteger(vm, 3, &val);

    auto result = cpp_insertNode(root, static_cast<int>(val));
    sq_pushCppTreeNode(vm, result);
    return 1;
}

// Global function: sq_cpp_inorderSum(node)
static SQInteger sq_cpp_inorderSum(HSQUIRRELVM vm) {
    auto node = sq_getCppTreeNode(vm, 2);
    SQInteger sum = cpp_inorderSum(node);
    sq_pushinteger(vm, sum);
    return 1;
}

// Global function: sq_cpp_treeHeight(node)
static SQInteger sq_cpp_treeHeight(HSQUIRRELVM vm) {
    auto node = sq_getCppTreeNode(vm, 2);
    SQInteger height = cpp_treeHeight(node);
    sq_pushinteger(vm, height);
    return 1;
}

// Global function: sq_cpp_rotateRight(node)
static SQInteger sq_cpp_rotateRight(HSQUIRRELVM vm) {
    auto node = sq_getCppTreeNode(vm, 2);
    auto result = cpp_rotateRight(node);
    sq_pushCppTreeNode(vm, result);
    return 1;
}

// Register all CppTreeNode bindings with a Squirrel VM
static void sq_registerCppTreeNode(HSQUIRRELVM vm) {
    sq_pushroottable(vm);

    // Create the CppTreeNode class
    sq_pushstring(vm, "CppTreeNode", -1);
    sq_newclass(vm, SQFalse); // no base class

    // Set the type tag
    sq_settypetag(vm, -1, CppTreeNodeTag);

    // Register constructor
    sq_pushstring(vm, "constructor", -1);
    sq_newclosure(vm, sq_CppTreeNode_constructor, 0);
    sq_setparamscheck(vm, 2, ".i"); // this + integer
    sq_newslot(vm, -3, SQFalse);

    // Add class to root table
    sq_newslot(vm, -3, SQFalse);

    // Register global functions
    // Type masks: o=null, x=instance, i=integer, | combines types
    sq_pushstring(vm, "cpp_insertNode", -1);
    sq_newclosure(vm, sq_cpp_insertNode, 0);
    sq_setparamscheck(vm, 3, ".o|xi"); // this + null|instance + integer
    sq_newslot(vm, -3, SQFalse);

    sq_pushstring(vm, "cpp_inorderSum", -1);
    sq_newclosure(vm, sq_cpp_inorderSum, 0);
    sq_setparamscheck(vm, 2, ".o|x"); // this + null|instance
    sq_newslot(vm, -3, SQFalse);

    sq_pushstring(vm, "cpp_treeHeight", -1);
    sq_newclosure(vm, sq_cpp_treeHeight, 0);
    sq_setparamscheck(vm, 2, ".o|x"); // this + null|instance
    sq_newslot(vm, -3, SQFalse);

    sq_pushstring(vm, "cpp_rotateRight", -1);
    sq_newclosure(vm, sq_cpp_rotateRight, 0);
    sq_setparamscheck(vm, 2, ".o|x"); // this + null|instance
    sq_newslot(vm, -3, SQFalse);

    sq_pop(vm, 1); // pop root table
}
#endif

class squirrel_comparison : public suite {
public:
    squirrel_comparison() : suite("Squirrel Performance Comparison") {
        std::cout << "\n==============================================\n";
        std::cout << "Initializing engines for Squirrel comparison...\n";
        std::cout << "==============================================\n";

        // Create JaiScript engine
        std::cout << "Creating JaiScript engine...\n";
        jai_engine = engine::make();
        jai::stdlib::register_all(jai_engine);
        std::cout << "JaiScript engine ready.\n";

#ifdef HAVE_SQUIRREL
        // Create Squirrel VM
        std::cout << "Creating Squirrel VM...\n";
        try {
            sq_vm = std::make_shared<SquirrelVM>();

            // Test basic execution
            auto result = sq_vm->eval<int>("42");
            if (result != 42) {
                throw std::runtime_error("Squirrel basic eval test failed");
            }

            // Register C++ TreeNode binding for fair comparison
            sq_registerCppTreeNode(sq_vm->vm);

            std::cout << "Squirrel VM ready.\n";
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Squirrel initialization failed: " << e.what() << "\n";
            sq_vm.reset();
        }
#else
        std::cout << "Squirrel not available - comparison benchmarks disabled.\n";
#endif

        std::cout << "==============================================\n\n";

        // Pre-declare functions for JaiScript
        jai_engine->execute("function add(auto a, auto b) -> auto { return a + b; }");
        jai_engine->execute(R"(
            class Point {
                float x = 0.0;
                float y = 0.0;
                Point(float px, float py) { x = px; y = py; }
            }
        )");
        jai_engine->execute(R"(
            class Calculator {
                int add(int a, int b) { return a + b; }
            }
        )");
        jai_engine->execute("function factorial(auto n) -> auto { if (n <= 1) { return 1; } return n * factorial(n - 1); }");
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

            function inorderSum(TreeNode node) -> int {
                if (node == null) { return 0; }
                return inorderSum(node.left) + node.value + inorderSum(node.right);
            }

            function treeHeight(TreeNode node) -> int {
                if (node == null) { return 0; }
                auto leftH = treeHeight(node.left);
                auto rightH = treeHeight(node.right);
                if (leftH > rightH) { return 1 + leftH; }
                return 1 + rightH;
            }

            function rotateRight(TreeNode y) -> TreeNode {
                if (y == null) { return null; }
                if (y.left == null) { return y; }
                auto x = y.left;
                y.left = x.right;
                x.right = y;
                return x;
            }
        )");

        // Bind C++ TreeNode class to JaiScript for fair comparison
        class_builder<CppTreeNode>(*jai_engine, "CppTreeNode")
            .constructor<int>()
            .property("value", &CppTreeNode::value)
            .property("left", &CppTreeNode::left, jai::skip_type_check)
            .property("right", &CppTreeNode::right, jai::skip_type_check)
            .build();

        jai_engine->add_function("cpp_insertNode", &cpp_insertNode);
        jai_engine->add_function("cpp_inorderSum", &cpp_inorderSum);
        jai_engine->add_function("cpp_treeHeight", &cpp_treeHeight);
        jai_engine->add_function("cpp_rotateRight", &cpp_rotateRight);

        // Pre-declare variables for C++ BST benchmark
        jai_engine->execute("var root = null; var sum = 0; var height = 0;");

#ifdef HAVE_SQUIRREL
        // Pre-declare functions for Squirrel
        sq_vm->execute(R"(
            function add(a, b) { return a + b; }
        )");
        sq_vm->execute(R"(
            class Point {
                x = 0.0;
                y = 0.0;
                constructor(px, py) { x = px; y = py; }
            }
        )");
        sq_vm->execute(R"(
            class Calculator {
                function add(a, b) { return a + b; }
            }
        )");
        sq_vm->execute(R"(
            function factorial(n) {
                if (n <= 1) return 1;
                return n * factorial(n - 1);
            }
        )");
        sq_vm->execute(R"(
            function fib(n) {
                if (n <= 1) return n;
                return fib(n - 1) + fib(n - 2);
            }
        )");

        // Pre-declare variables for Squirrel
        sq_vm->execute("x <- 0; y <- 0; z <- 0;");
        sq_vm->execute("result <- 0; sum <- 0;");
        sq_vm->execute("arr <- []; m <- {};");
        sq_vm->execute("p <- null; calc <- null;");

        // Squirrel TreeNode class and BST functions
        sq_vm->execute(R"(
            class TreeNode {
                value = 0;
                left = null;
                right = null;

                constructor(val) {
                    value = val;
                }
            }
        )");

        sq_vm->execute(R"(
            function insertNode(root, val) {
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

            function inorderSum(node) {
                if (node == null) return 0;
                return inorderSum(node.left) + node.value + inorderSum(node.right);
            }

            function treeHeight(node) {
                if (node == null) return 0;
                local leftH = treeHeight(node.left);
                local rightH = treeHeight(node.right);
                if (leftH > rightH) return 1 + leftH;
                return 1 + rightH;
            }

            function rotateRight(y) {
                if (y == null) return null;
                if (y.left == null) return y;
                local x = y.left;
                y.left = x.right;
                x.right = y;
                return x;
            }
        )");

        // Pre-declare variables for Squirrel BST benchmark
        sq_vm->execute("tree_root <- null; tree_sum <- 0; tree_height <- 0;");
#endif
    }

    std::shared_ptr<jai::engine> jai_engine;
#ifdef HAVE_SQUIRREL
    std::shared_ptr<SquirrelVM> sq_vm;
#endif

    void forge_tests() override {
#ifdef HAVE_SQUIRREL
        if (!sq_vm) {
            test("Squirrel Initialization Failed", [this]() {
                std::cout << "Squirrel VM failed to initialize. Skipping comparison benchmarks.\n";
            });
            return;
        }

        // ===== Integer Addition =====
        test("JaiScript vs Squirrel: Integer Addition", [this]() {
            benchmark("JaiScript - Integer Addition", [this]() {
                jai_engine->execute("42 + 58;");
            }, 5000);

            benchmark("Squirrel - Integer Addition", [this]() {
                sq_vm->execute("42 + 58;");
            }, 5000);
        });

        // ===== Float Multiplication =====
        test("JaiScript vs Squirrel: Float Multiplication", [this]() {
            benchmark("JaiScript - Float Multiplication", [this]() {
                jai_engine->execute("3.14 * 2.71;");
            }, 5000);

            benchmark("Squirrel - Float Multiplication", [this]() {
                sq_vm->execute("3.14 * 2.71;");
            }, 5000);
        });

        // ===== Variable Operations =====
        test("JaiScript vs Squirrel: Variable Operations", [this]() {
            benchmark("JaiScript - Variable Operations", [this]() {
                jai_engine->execute("auto x = 10; auto y = 20; auto z = x + y;");
            });

            benchmark("Squirrel - Variable Operations", [this]() {
                sq_vm->execute("local x = 10; local y = 20; local z = x + y;");
            });
        });

        // ===== Function Calls =====
        test("JaiScript vs Squirrel: Function Calls", [this]() {
            benchmark("JaiScript - Function Calls", [this]() {
                jai_engine->execute("add(10, 20);");
            });

            benchmark("Squirrel - Function Calls", [this]() {
                sq_vm->execute("add(10, 20);");
            });
        });

        // ===== Array Push/Pop =====
        test("JaiScript vs Squirrel: Array Operations", [this]() {
            benchmark("JaiScript - Array Push/Pop", [this]() {
                jai_engine->execute(R"(
                    auto arr = [];
                    arr.push(1); arr.push(2); arr.push(3);
                    arr.pop();
                    arr.size();
                )");
            });

            benchmark("Squirrel - Array Push/Pop", [this]() {
                sq_vm->execute(R"(
                    local arr = [];
                    arr.push(1); arr.push(2); arr.push(3);
                    arr.pop();
                    arr.len();
                )");
            });
        });

        // ===== Table/Map Insert/Lookup =====
        test("JaiScript vs Squirrel: Map/Table Operations", [this]() {
            benchmark("JaiScript - Map Insert/Lookup", [this]() {
                jai_engine->execute(R"(
                    auto m = {};
                    m["key1"] = 100;
                    m["key2"] = 200;
                    auto val = m["key1"];
                )");
            });

            benchmark("Squirrel - Table Insert/Lookup", [this]() {
                sq_vm->execute(R"(
                    local m = {};
                    m["key1"] <- 100;
                    m["key2"] <- 200;
                    local val = m["key1"];
                )");
            });
        });

        // ===== Class Creation =====
        test("JaiScript vs Squirrel: Class Creation", [this]() {
            benchmark("JaiScript - Class Creation", [this]() {
                jai_engine->execute("auto p = Point(3.0, 4.0);");
            });

            benchmark("Squirrel - Class Creation", [this]() {
                sq_vm->execute("local p = Point(3.0, 4.0);");
            });
        });

        // ===== Method Invocation =====
        test("JaiScript vs Squirrel: Method Invocation", [this]() {
            benchmark("JaiScript - Method Invocation", [this]() {
                jai_engine->execute("auto calc = Calculator(); calc.add(5, 3);");
            });

            benchmark("Squirrel - Method Invocation", [this]() {
                sq_vm->execute("local calc = Calculator(); calc.add(5, 3);");
            });
        });

        // ===== For Loop =====
        test("JaiScript vs Squirrel: For Loop (100 iterations)", [this]() {
            benchmark("JaiScript - For Loop", [this]() {
                jai_engine->execute(R"(
                    auto sum = 0;
                    for (auto i = 0; i < 100; ++i) { sum += i; }
                )");
            });

            benchmark("Squirrel - For Loop", [this]() {
                sq_vm->execute(R"(
                    local sum = 0;
                    for (local i = 0; i < 100; i += 1) { sum += i; }
                )");
            });
        });

        // ===== Factorial (Recursion) =====
        test("JaiScript vs Squirrel: Factorial (Recursion)", [this]() {
            benchmark("JaiScript - Factorial(10)", [this]() {
                jai_engine->execute("factorial(10);");
            });

            benchmark("Squirrel - Factorial(10)", [this]() {
                sq_vm->execute("factorial(10);");
            });
        });

        // ===== Fibonacci (Deep Recursion) =====
        test("JaiScript vs Squirrel: Fibonacci (Deep Recursion)", [this]() {
            benchmark("JaiScript - Fibonacci(15)", [this]() {
                jai_engine->execute("fib(15);");
            });

            benchmark("Squirrel - Fibonacci(15)", [this]() {
                sq_vm->execute("fib(15);");
            });
        });

        // ===== Foreach / Range-For =====
        test("JaiScript vs Squirrel: Foreach Loop", [this]() {
            // Pre-declare arrays
            jai_engine->execute("auto testArr = [0,1,2,3,4,5,6,7,8,9];");
            sq_vm->execute("testArr <- [0,1,2,3,4,5,6,7,8,9];");

            benchmark("JaiScript - Range-For (10 elements)", [this]() {
                jai_engine->execute(R"(
                    auto s = 0;
                    for (auto x : testArr) { s += x; }
                )");
            });

            benchmark("Squirrel - Foreach (10 elements)", [this]() {
                sq_vm->execute(R"(
                    local s = 0;
                    foreach (x in testArr) { s += x; }
                )");
            });
        });

        // ===== String Operations =====
        test("JaiScript vs Squirrel: String Concatenation", [this]() {
            benchmark("JaiScript - String Concat", [this]() {
                jai_engine->execute(R"(
                    auto s = "";
                    for (auto i = 0; i < 20; ++i) { s = s + "x"; }
                )");
            });

            benchmark("Squirrel - String Concat", [this]() {
                sq_vm->execute(R"(
                    local s = "";
                    for (local i = 0; i < 20; i += 1) { s = s + "x"; }
                )");
            });
        });

        // ===== Null Handling =====
        test("JaiScript vs Squirrel: Null Handling", [this]() {
            benchmark("JaiScript - Null Check", [this]() {
                jai_engine->execute(R"(
                    var x = null;
                    if (x == null) { x = 42; }
                )");
            });

            benchmark("Squirrel - Null Check", [this]() {
                sq_vm->execute(R"(
                    local x = null;
                    if (x == null) { x = 42; }
                )");
            });
        });

        // ===== Hot Loop (1000 iterations) =====
        test("JaiScript vs Squirrel: Hot Loop (1000 iterations)", [this]() {
            benchmark("JaiScript - Hot Loop (1000 iter)", [this]() {
                jai_engine->execute(R"(
                    auto sum = 0;
                    for (auto i = 0; i < 1000; ++i) { sum += i; }
                )");
            });

            benchmark("Squirrel - Hot Loop (1000 iter)", [this]() {
                sq_vm->execute(R"(
                    local sum = 0;
                    for (local i = 0; i < 1000; i += 1) { sum += i; }
                )");
            });
        });

        // ===== Binary Search Tree Operations (Pure Script) =====
        test("JaiScript vs Squirrel: Binary Search Tree (Pure Script)", [this]() {
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

            benchmark("Squirrel - BST (15 nodes)", [this]() {
                sq_vm->execute(R"(
                    local tree_root = TreeNode(8);
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

                    local tree_sum = inorderSum(tree_root);
                    local tree_height = treeHeight(tree_root);
                    tree_root = rotateRight(tree_root);
                    tree_sum = inorderSum(tree_root);
                )");
            });
        });

        // ===== Binary Search Tree Operations (C++ Bound - Fair Comparison) =====
        test("JaiScript vs Squirrel: Binary Search Tree (C++ Bound)", [this]() {
            // This benchmark provides a fair comparison by using the same C++ TreeNode class
            // bound to both engines. This isolates pure scripting performance from class implementation.

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

            benchmark("Squirrel - C++ BST (15 nodes)", [this]() {
                sq_vm->execute(R"(
                    local root = CppTreeNode(8);
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

                    local sum = cpp_inorderSum(root);
                    local height = cpp_treeHeight(root);
                    root = cpp_rotateRight(root);
                    sum = cpp_inorderSum(root);
                )");
            });
        });

#else
        test("Squirrel Not Available", [this]() {
            std::cout << "\n";
            std::cout << "Squirrel comparison benchmarks are DISABLED.\n";
            std::cout << "To enable: Build Squirrel library and rebuild with -DHAVE_SQUIRREL=ON\n";
            std::cout << "Squirrel source is at: Source/JaiScript/squirrel\n";
        });
#endif
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::squirrel_comparison)

} // namespace jai::foundry::tests