// Standalone BST profiling executable for Visual Studio profiler
// Build target: bst_profiler
// Run this with VS profiler to find hot paths in C++ FFI

#include <jaiscript/jaiscript.hpp>
#include <iostream>
#include <chrono>
#include <memory>

using namespace jai;

// C++ TreeNode class - same as in chaiscript_comparison.cpp
struct CppTreeNode {
    int value;
    std::shared_ptr<CppTreeNode> left;
    std::shared_ptr<CppTreeNode> right;

    explicit CppTreeNode(int v) : value(v), left(nullptr), right(nullptr) {}
};

// C++ tree operations
std::shared_ptr<CppTreeNode> cpp_insertNode(std::shared_ptr<CppTreeNode> root, int value) {
    if (!root) return std::make_shared<CppTreeNode>(value);
    if (value < root->value) {
        root->left = cpp_insertNode(root->left, value);
    } else {
        root->right = cpp_insertNode(root->right, value);
    }
    return root;
}

int cpp_inorderSum(std::shared_ptr<CppTreeNode> node) {
    if (!node) return 0;
    return cpp_inorderSum(node->left) + node->value + cpp_inorderSum(node->right);
}

int cpp_treeHeight(std::shared_ptr<CppTreeNode> node) {
    if (!node) return 0;
    int leftH = cpp_treeHeight(node->left);
    int rightH = cpp_treeHeight(node->right);
    return 1 + (leftH > rightH ? leftH : rightH);
}

std::shared_ptr<CppTreeNode> cpp_rotateRight(std::shared_ptr<CppTreeNode> y) {
    if (!y || !y->left) return y;
    auto x = y->left;
    auto T2 = x->right;
    x->right = y;
    y->left = T2;
    return x;
}

int main(int argc, char* argv[]) {
    // Parse iteration count from command line (default 10000)
    int iterations = 10000;
    if (argc > 1) {
        iterations = std::atoi(argv[1]);
        if (iterations <= 0) iterations = 10000;
    }

    std::cout << "=== JaiScript C++ BST Profiling Target ===\n";
    std::cout << "Iterations: " << iterations << "\n";
    std::cout << "Run with Visual Studio Profiler to find hot paths.\n\n";

    // Create engine and register types (done ONCE, not in the loop)
    auto eng = engine::make();

    // Bind C++ TreeNode class
    dynamic_binder<CppTreeNode>(*eng, "CppTreeNode")
        .constructor<int>()
        .property("value", &CppTreeNode::value)
        .property("left", &CppTreeNode::left, jai::skip_type_check)
        .property("right", &CppTreeNode::right, jai::skip_type_check)
        .build();

    // Register C++ tree operations
    eng->add_function("cpp_insertNode", &cpp_insertNode);
    eng->add_function("cpp_inorderSum", &cpp_inorderSum);
    eng->add_function("cpp_treeHeight", &cpp_treeHeight);
    eng->add_function("cpp_rotateRight", &cpp_rotateRight);

    // Pre-declare variables (for fair comparison - same as benchmark)
    eng->execute("var root = null;");
    eng->execute("var sum = 0;");
    eng->execute("var height = 0;");

    // The BST script - same as benchmark
    const char* bst_script = R"(
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
    )";

    // Warmup
    std::cout << "Warming up (100 iterations)...\n";
    for (int i = 0; i < 100; ++i) {
        eng->execute(bst_script);
    }

    // Timed run
    std::cout << "Running " << iterations << " iterations...\n";
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        eng->execute(bst_script);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_us = static_cast<double>(duration.count()) / iterations;
    std::cout << "\n=== Results ===\n";
    std::cout << "Total time: " << duration.count() << " uS\n";
    std::cout << "Average per iteration: " << avg_us << " uS\n";
    std::cout << "Iterations per second: " << (1000000.0 / avg_us) << "\n";

    return 0;
}
