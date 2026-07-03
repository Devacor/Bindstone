#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <optional>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class performance_benchmarks : public suite {
public:
    performance_benchmarks() : suite("Performance Benchmarks") {}

    // Reusable engine instance - created once, reused across benchmarks
    std::shared_ptr<jai::engine> test_engine;

    // Pre-parsed handle for the [jaibite] benchmark variants: parsed lazily on the
    // first (warmup) iteration, reused for every measured one.
    std::optional<jai::jaibite> bite;

    void pre_test() override {
        // Create engine once before tests
        test_engine = make_engine();
        jai::stdlib::register_all(test_engine);
        bite.reset();
    }

    void run_bite(const char* source) {
        if (!bite) {
            bite = test_engine->jaibite(source);
        }
        bite->execute();
    }

    void post_test() override {
        // Optional: Clean up after each test if needed
    }

    void forge_tests() override {
        // Note: Foundry's benchmark() runs 1000 iterations by default
        // Now we're measuring actual execution performance, not engine creation

        benchmark("Integer Addition", [this]() {
            auto result = test_engine->execute("42 + 58");
        });

        benchmark("Float Multiplication", [this]() {
            auto result = test_engine->execute("3.14 * 2.71");
        });

        benchmark("Variable Operations", [this]() {
            test_engine->execute(R"(
                auto x = 10;
                auto y = 20;
                auto z = x + y;
            )");
        });

        benchmark("Function Calls", [this]() {
            test_engine->execute(R"(
                function add(auto a, auto b) -> auto {
                    return a + b;
                }
                add(10, 20);
            )");
        });

        benchmark("Array Push/Pop", [this]() {
            test_engine->execute(R"(
                auto arr = [];
                arr.push(1);
                arr.push(2);
                arr.push(3);
                arr.pop();
                arr.size();
            )");
        });

        benchmark("Map Insert/Lookup", [this]() {
            test_engine->execute(R"(
                auto m = {};
                m["key1"] = 100;
                m["key2"] = 200;
                auto val = m["key1"];
            )");
        });

        benchmark("Class Creation", [this]() {
            test_engine->execute(R"(
                class Point {
                    float x = 0.0;
                    float y = 0.0;

                    Point(float px, float py) {
                        x = px;
                        y = py;
                    }
                }

                auto p = Point(3.0, 4.0);
            )");
        });

        benchmark("Method Invocation", [this]() {
            test_engine->execute(R"(
                class Calculator {
                    int add(int a, int b) {
                        return a + b;
                    }
                }

                auto calc = Calculator();
                calc.add(10, 20);
            )");
        });

        benchmark("For Loop (100 iterations)", [this]() {
            test_engine->execute(R"(
                auto sum = 0;
                for (auto i = 0; i < 100; i += 1) {
                    sum += i;
                }
            )");
        });

        benchmark("String Concatenation", [this]() {
            test_engine->execute(R"(
                auto s1 = "Hello";
                auto s2 = "World";
                auto s3 = s1 + " " + s2;
            )");
        });

        // Keep these separate - they measure engine overhead specifically
        benchmark("Engine Creation", []() {
            auto engine = make_engine();
        });

        benchmark("Stdlib Registration", []() {
            auto engine = make_engine();
            jai::stdlib::register_all(engine);
        });

        benchmark("Complex Expression", [this]() {
            test_engine->execute("(10 + 20) * (30 - 15) / 5");
        });

        benchmark("Class Inheritance", [this]() {
            test_engine->execute(R"(
                class Animal {
                    string name = "";
                    Animal(string n) { name = n; }
                }

                class Dog : Animal {
                    Dog(string n) : super(n) {}
                }

                auto dog = Dog("Buddy");
            )");
        });

        benchmark("Hot Loop (1000 iterations)", [this]() {
            test_engine->execute(R"(
                auto sum = 0;
                for (auto i = 0; i < 1000; i += 1) {
                    sum += i * 2;
                }
            )");
        });

        // Additional targeted benchmarks to isolate optimizations
        benchmark("Simple Compound Assignment (x100)", [this]() {
            test_engine->execute(R"(
                auto x = 0;
                x += 1; x += 1; x += 1; x += 1; x += 1;
                x += 1; x += 1; x += 1; x += 1; x += 1;
                x += 1; x += 1; x += 1; x += 1; x += 1;
                x += 1; x += 1; x += 1; x += 1; x += 1;
            )");
        });

        benchmark("Variable Lookup Heavy", [this]() {
            test_engine->execute(R"(
                auto a = 1;
                auto b = 2;
                auto c = 3;
                auto result = a + b + c + a + b + c + a + b + c + a;
            )");
        });

        // === var vs auto nested container benchmarks ===
        // These measure the overhead of homogeneity validation for auto

        benchmark("auto: Simple Array [10 ints]", [this]() {
            test_engine->execute(R"(
                auto arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
            )");
        });

        benchmark("var: Simple Array [10 ints]", [this]() {
            test_engine->execute(R"(
                var arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
            )");
        });

        benchmark("auto: 2D Array [[5x5 ints]]", [this]() {
            test_engine->execute(R"(
                auto matrix = [
                    [1, 2, 3, 4, 5],
                    [6, 7, 8, 9, 10],
                    [11, 12, 13, 14, 15],
                    [16, 17, 18, 19, 20],
                    [21, 22, 23, 24, 25]
                ];
            )");
        });

        benchmark("var: 2D Array [[5x5 ints]]", [this]() {
            test_engine->execute(R"(
                var matrix = [
                    [1, 2, 3, 4, 5],
                    [6, 7, 8, 9, 10],
                    [11, 12, 13, 14, 15],
                    [16, 17, 18, 19, 20],
                    [21, 22, 23, 24, 25]
                ];
            )");
        });

        benchmark("auto: 3D Array [[[2x2x2 ints]]]", [this]() {
            test_engine->execute(R"(
                auto cube = [
                    [[1, 2], [3, 4]],
                    [[5, 6], [7, 8]]
                ];
            )");
        });

        benchmark("var: 3D Array [[[2x2x2 ints]]]", [this]() {
            test_engine->execute(R"(
                var cube = [
                    [[1, 2], [3, 4]],
                    [[5, 6], [7, 8]]
                ];
            )");
        });

        benchmark("auto: Homogeneous Map {5 string keys -> ints}", [this]() {
            test_engine->execute(R"(
                auto m = {"a": 1, "b": 2, "c": 3, "d": 4, "e": 5};
            )");
        });

        benchmark("var: Heterogeneous Map {5 mixed values}", [this]() {
            test_engine->execute(R"(
                var m = {"name": "test", "age": 30, "active": true, "score": 3.14, "count": 100};
            )");
        });

        benchmark("auto: Nested Map 2 levels {k: {k: int}}", [this]() {
            test_engine->execute(R"(
                auto data = {
                    "group1": {"x": 1, "y": 2, "z": 3},
                    "group2": {"x": 4, "y": 5, "z": 6}
                };
            )");
        });

        benchmark("var: Nested Map 2 levels {k: {k: mixed}}", [this]() {
            test_engine->execute(R"(
                var data = {
                    "user1": {"name": "Alice", "age": 30},
                    "user2": {"name": "Bob", "age": 25}
                };
            )");
        });

        benchmark("auto: Mixed Array+Map 3 levels [[{k: int}]]", [this]() {
            test_engine->execute(R"(
                auto data = [
                    [{"a": 1, "b": 2}, {"c": 3, "d": 4}],
                    [{"e": 5, "f": 6}, {"g": 7, "h": 8}]
                ];
            )");
        });

        benchmark("var: Mixed Array+Map 3 levels [[{k: mixed}]]", [this]() {
            test_engine->execute(R"(
                var data = [
                    [{"name": "a", "val": 1}, {"name": "b", "val": 2}],
                    [{"name": "c", "val": 3}, {"name": "d", "val": 4}]
                ];
            )");
        });

        // === jaibite (pre-parsed) variants: steady-state execution cost only ===

        benchmark("Integer Addition [jaibite]", [this]() {
            run_bite("42 + 58");
        });

        benchmark("Function Calls [jaibite]", [this]() {
            run_bite(R"(
                function add(auto a, auto b) -> auto {
                    return a + b;
                }
                add(10, 20);
            )");
        });

        benchmark("Method Invocation [jaibite]", [this]() {
            run_bite(R"(
                class Calculator {
                    int add(int a, int b) {
                        return a + b;
                    }
                }

                auto calc = Calculator();
                calc.add(10, 20);
            )");
        });

        benchmark("For Loop (100 iterations) [jaibite]", [this]() {
            run_bite(R"(
                auto sum = 0;
                for (auto i = 0; i < 100; i += 1) {
                    sum += i;
                }
            )");
        });

        benchmark("Hot Loop (1000 iterations) [jaibite]", [this]() {
            run_bite(R"(
                auto sum = 0;
                for (auto i = 0; i < 1000; i += 1) {
                    sum += i * 2;
                }
            )");
        });

        // === Worst cases: recursion + allocation heavy (from the Squirrel comparison) ===

        benchmark("Fibonacci(15) [recursion]", [this]() {
            if (!bite) {
                test_engine->execute("function fib(auto n) -> auto { if (n <= 1) { return n; } return fib(n - 1) + fib(n - 2); }");
                bite = test_engine->jaibite("fib(15);");
            }
            bite->execute();
        });

        benchmark("Recurse 10 Locals (depth=15)", [this]() {
            if (!bite) {
                test_engine->execute(R"(
                    function recurseWithLocals(int depth) -> int {
                        if (depth <= 0) { return 0; }
                        auto a = depth * 2;
                        auto b = depth + 3;
                        auto c = a + b;
                        auto d = c * 2;
                        auto e = d - a;
                        auto f = e + b;
                        auto g = f * depth;
                        auto h = g - c;
                        auto i = h + d;
                        auto j = i - e;
                        auto sum = a + b + c + d + e + f + g + h + i + j;
                        return sum + recurseWithLocals(depth - 1);
                    }
                )");
                bite = test_engine->jaibite("recurseWithLocals(15);");
            }
            bite->execute();
        });

        benchmark("BST insert/sum/rotate (15 nodes)", [this]() {
            if (!bite) {
                test_engine->execute(R"(
                    class TreeNode {
                        int value = 0;
                        TreeNode left = null;
                        TreeNode right = null;
                        TreeNode(int val) { value = val; }
                    }
                    function insertNode(TreeNode root, int val) -> TreeNode {
                        if (root == null) { return TreeNode(val); }
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
                bite = test_engine->jaibite(R"(
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
            }
            bite->execute();
        });

        benchmark("BST shared_ptr insert/sum (15 nodes)", [this]() {
            // Reference-semantic variant: apples-to-apples with Squirrel/Lua instances
            // (the value-semantic BST above deep-copies subtrees per by-value pass)
            if (!bite) {
                test_engine->execute(R"(
                    class SNode {
                        int value = 0;
                        SNode left = null;
                        SNode right = null;
                        SNode(int val) { value = val; }
                    }
                    function sInsert(SNode root, int val) -> SNode {
                        if (root == null) { return shared_ptr<SNode>(val); }
                        if (val < root.value) {
                            root.left = sInsert(root.left, val);
                        } else {
                            root.right = sInsert(root.right, val);
                        }
                        return root;
                    }
                    function sSum(SNode node) -> int {
                        if (node == null) { return 0; }
                        return sSum(node.left) + node.value + sSum(node.right);
                    }
                )");
                bite = test_engine->jaibite(R"(
                    auto s_root = shared_ptr<SNode>(8);
                    s_root = sInsert(s_root, 4);
                    s_root = sInsert(s_root, 12);
                    s_root = sInsert(s_root, 2);
                    s_root = sInsert(s_root, 6);
                    s_root = sInsert(s_root, 10);
                    s_root = sInsert(s_root, 14);
                    s_root = sInsert(s_root, 1);
                    s_root = sInsert(s_root, 3);
                    s_root = sInsert(s_root, 5);
                    s_root = sInsert(s_root, 7);
                    s_root = sInsert(s_root, 9);
                    s_root = sInsert(s_root, 11);
                    s_root = sInsert(s_root, 13);
                    s_root = sInsert(s_root, 15);
                    auto s_total = sSum(s_root);
                )");
            }
            bite->execute();
        });

        // === String Performance Benchmarks ===
        // These measure the shared_ptr string optimization effectiveness

        benchmark("String Copy (Long String)", [this]() {
            test_engine->execute(R"(
                auto original = "This is a longer string that would be expensive to copy without shared_ptr optimization";
                auto copy1 = original;
                auto copy2 = original;
                auto copy3 = original;
                auto copy4 = original;
                auto copy5 = original;
            )");
        });

        benchmark("String Passing to Function", [this]() {
            test_engine->execute(R"(
                function processString(string s) -> int {
                    return s.length();
                }
                auto longStr = "This is a test string that gets passed to a function multiple times";
                processString(longStr);
                processString(longStr);
                processString(longStr);
            )");
        });

        benchmark("String Method Chaining", [this]() {
            test_engine->execute(R"(
                auto s = "  HELLO WORLD  ";
                s.trim().to_lower().replace_all(" ", "_");
            )");
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register this test suite with Foundry
FOUNDRY_REGISTER(jai::foundry::tests::performance_benchmarks)
