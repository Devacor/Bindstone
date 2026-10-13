#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

// Note: This requires Lua + sol2 to be available
// Lua 5.4 source: Source/JaiScript/lua, sol2: Source/JaiScript/sol2
#ifdef HAVE_SOL2
#include <sol/sol.hpp>
#endif

using namespace jai;
using namespace jai::foundry;

// C++ TreeNode class for fair performance comparison (anonymous namespace: the
// identically-shaped class in squirrel_comparison.cpp is a separate TU-local type,
// keeping the two suites ODR-independent).
namespace {

class CppTreeNode {
public:
	int value;
	std::shared_ptr<CppTreeNode> left;
	std::shared_ptr<CppTreeNode> right;

	CppTreeNode(int val) : value(val), left(nullptr), right(nullptr) {}
};

std::shared_ptr<CppTreeNode> cpp_insertNode(std::shared_ptr<CppTreeNode> root, int val) {
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
	y->left = x->right;
	x->right = y;
	return x;
}

} // namespace

namespace jai::foundry::tests {

// x20 unrolled trivial-op sources: runtime values defeat parse-time constant folding,
// and 20 ops lift the rows off the integer-uS floor (a 0uS row compares as noise).
// Identical shapes on both sides.
constexpr const char* k_jai_int_add_x20 = R"(
	auto a = 42;
	auto acc = 0;
	acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a;
	acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a;
	acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a;
	acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a; acc = acc + a;
	acc;
)";
constexpr const char* k_lua_int_add_x20 = R"(
	local a = 42
	local acc = 0
	acc = acc + a acc = acc + a acc = acc + a acc = acc + a acc = acc + a
	acc = acc + a acc = acc + a acc = acc + a acc = acc + a acc = acc + a
	acc = acc + a acc = acc + a acc = acc + a acc = acc + a acc = acc + a
	acc = acc + a acc = acc + a acc = acc + a acc = acc + a acc = acc + a
	return acc
)";
constexpr const char* k_jai_float_mul_x20 = R"(
	auto f = 1.001;
	auto acc = 3.14;
	acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f;
	acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f;
	acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f;
	acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f; acc = acc * f;
	acc;
)";
constexpr const char* k_lua_float_mul_x20 = R"(
	local f = 1.001
	local acc = 3.14
	acc = acc * f acc = acc * f acc = acc * f acc = acc * f acc = acc * f
	acc = acc * f acc = acc * f acc = acc * f acc = acc * f acc = acc * f
	acc = acc * f acc = acc * f acc = acc * f acc = acc * f acc = acc * f
	acc = acc * f acc = acc * f acc = acc * f acc = acc * f acc = acc * f
	return acc
)";

// Shared C++-bound BST source (plain rows re-submit it per iteration, the
// [precompiled] rows execute the compiled form) — one string so both paths
// measure identical work.
constexpr const char* k_jai_cpp_bst = R"(
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
constexpr const char* k_lua_cpp_bst = R"(
	local root = CppTreeNode.new(8)
	root = cpp_insertNode(root, 4)
	root = cpp_insertNode(root, 12)
	root = cpp_insertNode(root, 2)
	root = cpp_insertNode(root, 6)
	root = cpp_insertNode(root, 10)
	root = cpp_insertNode(root, 14)
	root = cpp_insertNode(root, 1)
	root = cpp_insertNode(root, 3)
	root = cpp_insertNode(root, 5)
	root = cpp_insertNode(root, 7)
	root = cpp_insertNode(root, 9)
	root = cpp_insertNode(root, 11)
	root = cpp_insertNode(root, 13)
	root = cpp_insertNode(root, 15)

	local sum = cpp_inorderSum(root)
	local height = cpp_treeHeight(root)
	root = cpp_rotateRight(root)
	sum = cpp_inorderSum(root)
)";

class sol2_comparison : public suite {
public:
	sol2_comparison() : suite("Lua (sol2) Performance Comparison") {
		std::cout << "\n==============================================\n";
		std::cout << "Initializing engines for Lua (sol2) comparison...\n";
		std::cout << "==============================================\n";

		// Create JaiScript engine
		std::cout << "Creating JaiScript engine...\n";
		jai_engine = make_engine();
		jai::stdlib::register_all(jai_engine);
		std::cout << "JaiScript engine ready.\n";

#ifdef HAVE_SOL2
		// Create Lua state
		std::cout << "Creating Lua state (sol2)...\n";
		try {
			lua = std::make_unique<sol::state>();
			// Subset of libs for fair benchmarking (mirrors Squirrel registering
			// only string+math): base (setmetatable/ipairs), string, math, table.
			lua->open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

			// Basic execution sanity check
			int result = lua->script("return 42");
			if (result != 42) {
				throw std::runtime_error("Lua basic eval test failed");
			}

			// Lua's incremental GC stays at defaults (honest out-of-box numbers); JaiScript is refcounted.

			// Register C++ TreeNode binding for fair comparison.
			// NOT sol::constructors: that creates a VALUE userdata, but the bound
			// functions traffic in shared_ptr<CppTreeNode> — the object must live in
			// a shared_ptr from birth or cpp_insertNode silently mutates a copy.
			lua->new_usertype<CppTreeNode>("CppTreeNode",
				"new", sol::factories([](int v) { return std::make_shared<CppTreeNode>(v); }),
				"value", &CppTreeNode::value,
				"left", &CppTreeNode::left,
				"right", &CppTreeNode::right);
			lua->set_function("cpp_insertNode", &cpp_insertNode);
			lua->set_function("cpp_inorderSum", &cpp_inorderSum);
			lua->set_function("cpp_treeHeight", &cpp_treeHeight);
			lua->set_function("cpp_rotateRight", &cpp_rotateRight);

			std::cout << "Lua state ready.\n";
		} catch (const std::exception& e) {
			std::cerr << "ERROR: Lua initialization failed: " << e.what() << "\n";
			lua.reset();
		}
#else
		std::cout << "Lua/sol2 not available - comparison benchmarks disabled.\n";
#endif

		std::cout << "==============================================\n\n";

		// Pre-declare functions for JaiScript (identical to squirrel_comparison.cpp)
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

		// Recursive function with 10 local variables - tests local variable lookup performance
		jai_engine->execute(R"(
			function recurseWithLocals(int depth) -> int {
				if (depth <= 0) { return 0; }

				// 10 local variables with computations
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

				// Use all locals to prevent optimization
				auto sum = a + b + c + d + e + f + g + h + i + j;

				return sum + recurseWithLocals(depth - 1);
			}
		)");

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

		// By-ref BST: reference params mutate the tree in place (matches Lua tables,
		// which are reference types and never deep-copy)
		jai_engine->execute(R"(
			function insertRef(TreeNode& node, int val) {
				if (node == null) { node = TreeNode(val); return; }
				if (val < node.value) { insertRef(node.left, val); } else { insertRef(node.right, val); }
			}
			function sumRef(TreeNode& node) -> int {
				if (node == null) { return 0; }
				return sumRef(node.left) + node.value + sumRef(node.right);
			}
		)");

		// Bind C++ TreeNode class to JaiScript for fair comparison
		dynamic_binder<CppTreeNode>(*jai_engine, "CppTreeNode")
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

#ifdef HAVE_SOL2
		if (lua) {
			// Pre-declare functions for Lua (bodies validated standalone under Lua 5.4.7)
			lua->script(R"(
				function add(a, b) return a + b end
			)");
			lua->script(R"(
				Point = {}
				Point.__index = Point
				function Point.new(px, py)
					local self = setmetatable({}, Point)
					self.x = px
					self.y = py
					return self
				end
			)");
			lua->script(R"(
				Calculator = {}
				Calculator.__index = Calculator
				function Calculator.new()
					return setmetatable({}, Calculator)
				end
				function Calculator:add(a, b)
					return a + b
				end
			)");
			lua->script(R"(
				function factorial(n)
					if n <= 1 then return 1 end
					return n * factorial(n - 1)
				end
			)");
			lua->script(R"(
				function fib(n)
					if n <= 1 then return n end
					return fib(n - 1) + fib(n - 2)
				end
			)");

			lua->script(R"(
				function recurseWithLocals(depth)
					if depth <= 0 then return 0 end

					-- 10 local variables with computations
					local a = depth * 2
					local b = depth + 3
					local c = a + b
					local d = c * 2
					local e = d - a
					local f = e + b
					local g = f * depth
					local h = g - c
					local i = h + d
					local j = i - e

					-- Use all locals to prevent optimization
					local sum = a + b + c + d + e + f + g + h + i + j

					return sum + recurseWithLocals(depth - 1)
				end
			)");

			// Lua TreeNode class (standard metatable idiom) and BST functions
			lua->script(R"(
				TreeNode = {}
				TreeNode.__index = TreeNode
				function TreeNode.new(val)
					local self = setmetatable({}, TreeNode)
					self.value = val
					self.left = nil
					self.right = nil
					return self
				end
			)");

			lua->script(R"(
				function insertNode(root, val)
					if root == nil then
						return TreeNode.new(val)
					end
					if val < root.value then
						root.left = insertNode(root.left, val)
					else
						root.right = insertNode(root.right, val)
					end
					return root
				end

				function inorderSum(node)
					if node == nil then return 0 end
					return inorderSum(node.left) + node.value + inorderSum(node.right)
				end

				function treeHeight(node)
					if node == nil then return 0 end
					local leftH = treeHeight(node.left)
					local rightH = treeHeight(node.right)
					if leftH > rightH then return 1 + leftH end
					return 1 + rightH
				end

				function rotateRight(y)
					if y == nil then return nil end
					if y.left == nil then return y end
					local x = y.left
					y.left = x.right
					x.right = y
					return x
				end
			)");
		}
#endif

		// ===== [precompiled] setup: both sides parse/compile ONCE here =====
		// The per-iteration pairs measure execute(src)/script(src): JaiScript's
		// 64-entry source cache skips re-parsing there while lua->script recompiles
		// every call, so those numbers mix Lua compile cost into the ratio. These
		// variants remove the asymmetry: jaibite vs a loaded protected_function,
		// execution only.
		jai_pre_int_add = jai_engine->jaibite(k_jai_int_add_x20);
		jai_pre_float_mul = jai_engine->jaibite(k_jai_float_mul_x20);
		jai_pre_var_ops = jai_engine->jaibite("auto x = 10; auto y = 20; auto z = x + y;");
		jai_pre_func_call = jai_engine->jaibite("add(10, 20);");
		jai_pre_method = jai_engine->jaibite("auto calc = Calculator(); calc.add(5, 3);");
		jai_pre_class = jai_engine->jaibite("auto p = Point(3.0, 4.0);");
		jai_pre_array = jai_engine->jaibite(R"(
			auto arr = [];
			arr.push(1); arr.push(2); arr.push(3);
			arr.pop();
			arr.size();
		)");
		jai_pre_map = jai_engine->jaibite(R"(
			auto m = {};
			m["key1"] = 100;
			m["key2"] = 200;
			auto val = m["key1"];
		)");
		jai_pre_for_loop = jai_engine->jaibite(R"(
			auto sum = 0;
			for (auto i = 0; i < 100; ++i) { sum += i; }
		)");
		jai_pre_factorial = jai_engine->jaibite("factorial(10);");
		jai_pre_fib = jai_engine->jaibite("fib(15);");
		jai_pre_recurse_locals = jai_engine->jaibite("recurseWithLocals(15);");
		jai_engine->execute("auto preArr = [0,1,2,3,4,5,6,7,8,9];");
		jai_pre_range_for = jai_engine->jaibite(R"(
			auto s = 0;
			for (auto x : preArr) { s += x; }
			s;
		)");
		jai_pre_str_concat = jai_engine->jaibite(R"(
			auto s = "";
			for (auto i = 0; i < 20; ++i) { s = s + "x"; }
		)");
		jai_pre_null_check = jai_engine->jaibite(R"(
			var x = null;
			if (x == null) { x = 42; }
			x;
		)");
		jai_pre_hot_loop = jai_engine->jaibite(R"(
			auto sum = 0;
			for (auto i = 0; i < 1000; ++i) { sum += i; }
		)");
		jai_pre_bst = jai_engine->jaibite(R"(
			var ref_root = null;
			insertRef(ref_root, 8);
			insertRef(ref_root, 4);
			insertRef(ref_root, 12);
			insertRef(ref_root, 2);
			insertRef(ref_root, 6);
			insertRef(ref_root, 10);
			insertRef(ref_root, 14);
			insertRef(ref_root, 1);
			insertRef(ref_root, 3);
			insertRef(ref_root, 5);
			insertRef(ref_root, 7);
			insertRef(ref_root, 9);
			insertRef(ref_root, 11);
			insertRef(ref_root, 13);
			insertRef(ref_root, 15);

			var ref_sum = sumRef(ref_root);
			ref_sum;
		)");

		jai_pre_cpp_bst = jai_engine->jaibite(k_jai_cpp_bst);

		// Correctness gates: each precompiled script produces the same value its
		// per-iteration sibling does
		check_eq((int64_t)840, jai_pre_int_add.execute().as_int());
		check_near(3.2034, jai_pre_float_mul.execute().as_float(), 0.001); // 3.14 * 1.001^20
		check_eq((int64_t)30, jai_pre_func_call.execute().as_int());
		check_eq((int64_t)8, jai_pre_method.execute().as_int());
		check_eq((int64_t)2, jai_pre_array.execute().as_int());
		check_eq((int64_t)3628800, jai_pre_factorial.execute().as_int());
		check_eq((int64_t)610, jai_pre_fib.execute().as_int());
		check_eq((int64_t)45, jai_pre_range_for.execute().as_int());
		check_eq((int64_t)42, jai_pre_null_check.execute().as_int());
		jai_pre_for_loop.execute();
		check_eq((int64_t)4950, jai_engine->execute("sum;").as_int());
		jai_pre_hot_loop.execute();
		check_eq((int64_t)499500, jai_engine->execute("sum;").as_int());
		check_eq((int64_t)120, jai_pre_bst.execute().as_int());
		jai_pre_cpp_bst.execute();
		check_eq((int64_t)120, jai_engine->execute("sum;").as_int());

#ifdef HAVE_SOL2
		if (lua) {
			auto lua_precompile = [this](const char* src) {
				sol::load_result loaded = lua->load(src);
				if (!loaded.valid()) {
					sol::error err = loaded;
					throw std::runtime_error(std::string("Lua precompile failed: ") + err.what());
				}
				return loaded.get<sol::protected_function>();
			};
			lua_pre_int_add = lua_precompile(k_lua_int_add_x20);
			lua_pre_float_mul = lua_precompile(k_lua_float_mul_x20);
			lua_pre_var_ops = lua_precompile("local x = 10 local y = 20 local z = x + y");
			lua_pre_func_call = lua_precompile("add(10, 20)");
			lua_pre_method = lua_precompile("local calc = Calculator.new() calc:add(5, 3)");
			lua_pre_class = lua_precompile("local p = Point.new(3.0, 4.0)");
			lua_pre_array = lua_precompile(R"(
					local arr = {}
					table.insert(arr, 1); table.insert(arr, 2); table.insert(arr, 3)
					table.remove(arr)
					local n = #arr
				)");
			lua_pre_map = lua_precompile(R"(
					local m = {}
					m["key1"] = 100
					m["key2"] = 200
					local val = m["key1"]
				)");
			lua_pre_for_loop = lua_precompile(R"(
					local sum = 0
					for i = 0, 99 do sum = sum + i end
				)");
			lua_pre_factorial = lua_precompile("factorial(10)");
			lua_pre_fib = lua_precompile("fib(15)");
			lua_pre_recurse_locals = lua_precompile("recurseWithLocals(15)");
			lua->script("preArr = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}");
			lua_pre_range_for = lua_precompile(R"(
					local s = 0
					for _, x in ipairs(preArr) do s = s + x end
				)");
			lua_pre_str_concat = lua_precompile(R"(
					local s = ""
					for i = 1, 20 do s = s .. "x" end
				)");
			lua_pre_null_check = lua_precompile(R"(
					local x = nil
					if x == nil then x = 42 end
				)");
			lua_pre_hot_loop = lua_precompile(R"(
					local sum = 0
					for i = 0, 999 do sum = sum + i end
				)");
			lua_pre_bst = lua_precompile(R"(
					local tree_root = TreeNode.new(8)
					tree_root = insertNode(tree_root, 4)
					tree_root = insertNode(tree_root, 12)
					tree_root = insertNode(tree_root, 2)
					tree_root = insertNode(tree_root, 6)
					tree_root = insertNode(tree_root, 10)
					tree_root = insertNode(tree_root, 14)
					tree_root = insertNode(tree_root, 1)
					tree_root = insertNode(tree_root, 3)
					tree_root = insertNode(tree_root, 5)
					tree_root = insertNode(tree_root, 7)
					tree_root = insertNode(tree_root, 9)
					tree_root = insertNode(tree_root, 11)
					tree_root = insertNode(tree_root, 13)
					tree_root = insertNode(tree_root, 15)

					local tree_sum = inorderSum(tree_root)
					return tree_sum
				)");

			lua_pre_cpp_bst = lua_precompile(k_lua_cpp_bst);

			// Gates: one-shot `return` variants of the same sources (locals are not
			// observable after the chunk), plus the BST function's own return value
			check_eq(840, (int)lua->script(k_lua_int_add_x20));
			check_eq(30, (int)lua->script("return add(10, 20)"));
			check_eq(3628800, (int)lua->script("return factorial(10)"));
			check_eq(45, (int)lua->script("local s = 0 for _, x in ipairs(preArr) do s = s + x end return s"));
			check_eq(8, (int)lua->script("local calc = Calculator.new() return calc:add(5, 3)"));
			check_eq(610, (int)lua->script("return fib(15)"));
			check_eq(499500, (int)lua->script("local sum = 0 for i = 0, 999 do sum = sum + i end return sum"));
			sol::protected_function_result bst_result = lua_pre_bst();
			check_true(bst_result.valid(), "precompiled Lua BST call failed");
			check_eq((int64_t)120, bst_result.get<int64_t>());
		}
#endif
	}

	std::shared_ptr<jai::engine> jai_engine;
	jai::jaibite jai_pre_int_add, jai_pre_func_call, jai_pre_method, jai_pre_fib, jai_pre_hot_loop, jai_pre_bst;
	jai::jaibite jai_pre_float_mul, jai_pre_var_ops, jai_pre_array, jai_pre_map, jai_pre_class,
		jai_pre_for_loop, jai_pre_factorial, jai_pre_recurse_locals, jai_pre_range_for,
		jai_pre_str_concat, jai_pre_null_check, jai_pre_cpp_bst;
#ifdef HAVE_SOL2
	std::unique_ptr<sol::state> lua;
	// Declared after lua so they release their registry refs before the state closes
	sol::protected_function lua_pre_int_add, lua_pre_func_call, lua_pre_method, lua_pre_fib, lua_pre_hot_loop, lua_pre_bst;
	sol::protected_function lua_pre_float_mul, lua_pre_var_ops, lua_pre_array, lua_pre_map, lua_pre_class,
		lua_pre_for_loop, lua_pre_factorial, lua_pre_recurse_locals, lua_pre_range_for,
		lua_pre_str_concat, lua_pre_null_check, lua_pre_cpp_bst;
#endif

	void forge_tests() override {
#ifdef HAVE_SOL2
		if (!lua) {
			test("Lua Initialization Failed", [this]() {
				std::cout << "Lua state failed to initialize. Skipping comparison benchmarks.\n";
			});
			return;
		}

		// Each lua->script() call compiles + runs, matching Squirrel's
		// sq_compilebuffer-per-execute.

		// ===== Integer Addition (x20 unrolled - see k_jai_int_add_x20) =====
		test("JaiScript vs Lua(sol2): Integer Addition (x20)", [this]() {
			benchmark("JaiScript - Integer Addition (x20)", [this]() {
				jai_engine->execute(k_jai_int_add_x20);
			}, 5000);

			benchmark("Lua(sol2) - Integer Addition (x20)", [this]() {
				lua->script(k_lua_int_add_x20);
			}, 5000);
		});

		// Same pair with the compile cost removed on BOTH sides (jaibite vs loaded
		// protected_function); the plain pair above keeps measuring the realistic
		// script(src) path.
		test("JaiScript vs Lua(sol2): Integer Addition (x20) [precompiled]", [this]() {
			benchmark("JaiScript - Integer Addition (x20) [precompiled]", [this]() {
				jai_pre_int_add.execute();
			}, 5000);

			benchmark("Lua(sol2) - Integer Addition (x20) [precompiled]", [this]() {
				lua_pre_int_add();
			}, 5000);
		});

		// ===== Float Multiplication (x20 unrolled) =====
		test("JaiScript vs Lua(sol2): Float Multiplication (x20)", [this]() {
			benchmark("JaiScript - Float Multiplication (x20)", [this]() {
				jai_engine->execute(k_jai_float_mul_x20);
			}, 5000);

			benchmark("Lua(sol2) - Float Multiplication (x20)", [this]() {
				lua->script(k_lua_float_mul_x20);
			}, 5000);
		});

		test("JaiScript vs Lua(sol2): Float Multiplication (x20) [precompiled]", [this]() {
			benchmark("JaiScript - Float Multiplication (x20) [precompiled]", [this]() {
				jai_pre_float_mul.execute();
			}, 5000);

			benchmark("Lua(sol2) - Float Multiplication (x20) [precompiled]", [this]() {
				lua_pre_float_mul();
			}, 5000);
		});

		// ===== Variable Operations =====
		test("JaiScript vs Lua(sol2): Variable Operations", [this]() {
			benchmark("JaiScript - Variable Operations", [this]() {
				jai_engine->execute("auto x = 10; auto y = 20; auto z = x + y;");
			});

			benchmark("Lua(sol2) - Variable Operations", [this]() {
				lua->script("local x = 10 local y = 20 local z = x + y");
			});
		});

		test("JaiScript vs Lua(sol2): Variable Operations [precompiled]", [this]() {
			benchmark("JaiScript - Variable Operations [precompiled]", [this]() {
				jai_pre_var_ops.execute();
			});

			benchmark("Lua(sol2) - Variable Operations [precompiled]", [this]() {
				lua_pre_var_ops();
			});
		});

		// ===== Function Calls =====
		test("JaiScript vs Lua(sol2): Function Calls", [this]() {
			benchmark("JaiScript - Function Calls", [this]() {
				jai_engine->execute("add(10, 20);");
			});

			benchmark("Lua(sol2) - Function Calls", [this]() {
				lua->script("add(10, 20)");
			});
		});

		test("JaiScript vs Lua(sol2): Function Calls [precompiled]", [this]() {
			benchmark("JaiScript - Function Calls [precompiled]", [this]() {
				jai_pre_func_call.execute();
			});

			benchmark("Lua(sol2) - Function Calls [precompiled]", [this]() {
				lua_pre_func_call();
			});
		});

		// ===== Array Push/Pop =====
		test("JaiScript vs Lua(sol2): Array Operations", [this]() {
			benchmark("JaiScript - Array Push/Pop", [this]() {
				jai_engine->execute(R"(
					auto arr = [];
					arr.push(1); arr.push(2); arr.push(3);
					arr.pop();
					arr.size();
				)");
			});

			benchmark("Lua(sol2) - Array Push/Pop", [this]() {
				lua->script(R"(
					local arr = {}
					table.insert(arr, 1); table.insert(arr, 2); table.insert(arr, 3)
					table.remove(arr)
					local n = #arr
				)");
			});
		});

		test("JaiScript vs Lua(sol2): Array Operations [precompiled]", [this]() {
			benchmark("JaiScript - Array Push/Pop [precompiled]", [this]() {
				jai_pre_array.execute();
			});

			benchmark("Lua(sol2) - Array Push/Pop [precompiled]", [this]() {
				lua_pre_array();
			});
		});

		// ===== Table/Map Insert/Lookup =====
		test("JaiScript vs Lua(sol2): Map/Table Operations", [this]() {
			benchmark("JaiScript - Map Insert/Lookup", [this]() {
				jai_engine->execute(R"(
					auto m = {};
					m["key1"] = 100;
					m["key2"] = 200;
					auto val = m["key1"];
				)");
			});

			benchmark("Lua(sol2) - Table Insert/Lookup", [this]() {
				lua->script(R"(
					local m = {}
					m["key1"] = 100
					m["key2"] = 200
					local val = m["key1"]
				)");
			});
		});

		test("JaiScript vs Lua(sol2): Map/Table Operations [precompiled]", [this]() {
			benchmark("JaiScript - Map Insert/Lookup [precompiled]", [this]() {
				jai_pre_map.execute();
			});

			benchmark("Lua(sol2) - Table Insert/Lookup [precompiled]", [this]() {
				lua_pre_map();
			});
		});

		// ===== Class Creation =====
		test("JaiScript vs Lua(sol2): Class Creation", [this]() {
			benchmark("JaiScript - Class Creation", [this]() {
				jai_engine->execute("auto p = Point(3.0, 4.0);");
			});

			benchmark("Lua(sol2) - Class Creation", [this]() {
				lua->script("local p = Point.new(3.0, 4.0)");
			});
		});

		test("JaiScript vs Lua(sol2): Class Creation [precompiled]", [this]() {
			benchmark("JaiScript - Class Creation [precompiled]", [this]() {
				jai_pre_class.execute();
			});

			benchmark("Lua(sol2) - Class Creation [precompiled]", [this]() {
				lua_pre_class();
			});
		});

		// ===== Method Invocation =====
		test("JaiScript vs Lua(sol2): Method Invocation", [this]() {
			benchmark("JaiScript - Method Invocation", [this]() {
				jai_engine->execute("auto calc = Calculator(); calc.add(5, 3);");
			});

			benchmark("Lua(sol2) - Method Invocation", [this]() {
				lua->script("local calc = Calculator.new() calc:add(5, 3)");
			});
		});

		test("JaiScript vs Lua(sol2): Method Invocation [precompiled]", [this]() {
			benchmark("JaiScript - Method Invocation [precompiled]", [this]() {
				jai_pre_method.execute();
			});

			benchmark("Lua(sol2) - Method Invocation [precompiled]", [this]() {
				lua_pre_method();
			});
		});

		// ===== For Loop =====
		test("JaiScript vs Lua(sol2): For Loop (100 iterations)", [this]() {
			benchmark("JaiScript - For Loop", [this]() {
				jai_engine->execute(R"(
					auto sum = 0;
					for (auto i = 0; i < 100; ++i) { sum += i; }
				)");
			});

			benchmark("Lua(sol2) - For Loop", [this]() {
				lua->script(R"(
					local sum = 0
					for i = 0, 99 do sum = sum + i end
				)");
			});
		});

		test("JaiScript vs Lua(sol2): For Loop (100 iterations) [precompiled]", [this]() {
			benchmark("JaiScript - For Loop [precompiled]", [this]() {
				jai_pre_for_loop.execute();
			});

			benchmark("Lua(sol2) - For Loop [precompiled]", [this]() {
				lua_pre_for_loop();
			});
		});

		// ===== Factorial (Recursion) =====
		test("JaiScript vs Lua(sol2): Factorial (Recursion)", [this]() {
			benchmark("JaiScript - Factorial(10)", [this]() {
				jai_engine->execute("factorial(10);");
			});

			benchmark("Lua(sol2) - Factorial(10)", [this]() {
				lua->script("factorial(10)");
			});
		});

		test("JaiScript vs Lua(sol2): Factorial (Recursion) [precompiled]", [this]() {
			benchmark("JaiScript - Factorial(10) [precompiled]", [this]() {
				jai_pre_factorial.execute();
			});

			benchmark("Lua(sol2) - Factorial(10) [precompiled]", [this]() {
				lua_pre_factorial();
			});
		});

		// ===== Fibonacci (Deep Recursion) =====
		test("JaiScript vs Lua(sol2): Fibonacci (Deep Recursion)", [this]() {
			benchmark("JaiScript - Fibonacci(15)", [this]() {
				jai_engine->execute("fib(15);");
			});

			benchmark("Lua(sol2) - Fibonacci(15)", [this]() {
				lua->script("fib(15)");
			});
		});

		test("JaiScript vs Lua(sol2): Fibonacci (Deep Recursion) [precompiled]", [this]() {
			benchmark("JaiScript - Fibonacci(15) [precompiled]", [this]() {
				jai_pre_fib.execute();
			});

			benchmark("Lua(sol2) - Fibonacci(15) [precompiled]", [this]() {
				lua_pre_fib();
			});
		});

		// ===== Recursion with 10 Local Variables =====
		test("JaiScript vs Lua(sol2): Recursion with 10 Locals", [this]() {
			// This tests the cost of local variable access in recursive functions.
			// Each call creates 10 local variables and uses them all before recursing.
			// Depth of 15 = 15 stack frames, each with 10+ locals.
			benchmark("JaiScript - Recurse with Locals (depth=15)", [this]() {
				jai_engine->execute("recurseWithLocals(15);");
			});

			benchmark("Lua(sol2) - Recurse with Locals (depth=15)", [this]() {
				lua->script("recurseWithLocals(15)");
			});
		});

		test("JaiScript vs Lua(sol2): Recursion with 10 Locals [precompiled]", [this]() {
			benchmark("JaiScript - Recurse with Locals (depth=15) [precompiled]", [this]() {
				jai_pre_recurse_locals.execute();
			});

			benchmark("Lua(sol2) - Recurse with Locals (depth=15) [precompiled]", [this]() {
				lua_pre_recurse_locals();
			});
		});

		// ===== Foreach / Range-For =====
		test("JaiScript vs Lua(sol2): Foreach Loop", [this]() {
			// Pre-declare arrays
			jai_engine->execute("auto testArr = [0,1,2,3,4,5,6,7,8,9];");
			lua->script("testArr = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}");

			benchmark("JaiScript - Range-For (10 elements)", [this]() {
				jai_engine->execute(R"(
					auto s = 0;
					for (auto x : testArr) { s += x; }
				)");
			});

			benchmark("Lua(sol2) - Foreach (10 elements)", [this]() {
				lua->script(R"(
					local s = 0
					for _, x in ipairs(testArr) do s = s + x end
				)");
			});
		});

		test("JaiScript vs Lua(sol2): Foreach Loop [precompiled]", [this]() {
			benchmark("JaiScript - Range-For (10 elements) [precompiled]", [this]() {
				jai_pre_range_for.execute();
			});

			benchmark("Lua(sol2) - Foreach (10 elements) [precompiled]", [this]() {
				lua_pre_range_for();
			});
		});

		// ===== String Operations =====
		test("JaiScript vs Lua(sol2): String Concatenation", [this]() {
			benchmark("JaiScript - String Concat", [this]() {
				jai_engine->execute(R"(
					auto s = "";
					for (auto i = 0; i < 20; ++i) { s = s + "x"; }
				)");
			});

			benchmark("Lua(sol2) - String Concat", [this]() {
				lua->script(R"(
					local s = ""
					for i = 1, 20 do s = s .. "x" end
				)");
			});
		});

		test("JaiScript vs Lua(sol2): String Concatenation [precompiled]", [this]() {
			benchmark("JaiScript - String Concat [precompiled]", [this]() {
				jai_pre_str_concat.execute();
			});

			benchmark("Lua(sol2) - String Concat [precompiled]", [this]() {
				lua_pre_str_concat();
			});
		});

		// ===== Null Handling =====
		test("JaiScript vs Lua(sol2): Null Handling", [this]() {
			benchmark("JaiScript - Null Check", [this]() {
				jai_engine->execute(R"(
					var x = null;
					if (x == null) { x = 42; }
				)");
			});

			benchmark("Lua(sol2) - Null Check", [this]() {
				lua->script(R"(
					local x = nil
					if x == nil then x = 42 end
				)");
			});
		});

		test("JaiScript vs Lua(sol2): Null Handling [precompiled]", [this]() {
			benchmark("JaiScript - Null Check [precompiled]", [this]() {
				jai_pre_null_check.execute();
			});

			benchmark("Lua(sol2) - Null Check [precompiled]", [this]() {
				lua_pre_null_check();
			});
		});

		// ===== Hot Loop (1000 iterations) =====
		test("JaiScript vs Lua(sol2): Hot Loop (1000 iterations)", [this]() {
			benchmark("JaiScript - Hot Loop (1000 iter)", [this]() {
				jai_engine->execute(R"(
					auto sum = 0;
					for (auto i = 0; i < 1000; ++i) { sum += i; }
				)");
			});

			benchmark("Lua(sol2) - Hot Loop (1000 iter)", [this]() {
				lua->script(R"(
					local sum = 0
					for i = 0, 999 do sum = sum + i end
				)");
			});
		});

		test("JaiScript vs Lua(sol2): Hot Loop (1000 iterations) [precompiled]", [this]() {
			benchmark("JaiScript - Hot Loop (1000 iter) [precompiled]", [this]() {
				jai_pre_hot_loop.execute();
			});

			benchmark("Lua(sol2) - Hot Loop (1000 iter) [precompiled]", [this]() {
				lua_pre_hot_loop();
			});
		});

		// ===== Binary Search Tree Operations (Pure Script) =====
		test("JaiScript vs Lua(sol2): Binary Search Tree (Pure Script)", [this]() {
			// Object creation, field access, recursion, pointer chasing.
			// 15 nodes; validated: inorder sum 120 pre+post rotate, height 4.

			benchmark("JaiScript - BST (15 nodes) [naive by-value]", [this]() {
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

			benchmark("Lua(sol2) - BST (15 nodes)", [this]() {
				lua->script(R"(
					local tree_root = TreeNode.new(8)
					tree_root = insertNode(tree_root, 4)
					tree_root = insertNode(tree_root, 12)
					tree_root = insertNode(tree_root, 2)
					tree_root = insertNode(tree_root, 6)
					tree_root = insertNode(tree_root, 10)
					tree_root = insertNode(tree_root, 14)
					tree_root = insertNode(tree_root, 1)
					tree_root = insertNode(tree_root, 3)
					tree_root = insertNode(tree_root, 5)
					tree_root = insertNode(tree_root, 7)
					tree_root = insertNode(tree_root, 9)
					tree_root = insertNode(tree_root, 11)
					tree_root = insertNode(tree_root, 13)
					tree_root = insertNode(tree_root, 15)

					local tree_sum = inorderSum(tree_root)
					local tree_height = treeHeight(tree_root)
					tree_root = rotateRight(tree_root)
					tree_sum = inorderSum(tree_root)
				)");
			});
		});

		// ===== Binary Search Tree (By-Ref Script) =====
		test("JaiScript vs Lua(sol2): Binary Search Tree (By-Ref Script)", [this]() {
			// Apples-to-apples: Lua tables are reference types (insertNode mutates
			// one tree, zero deep copies), so the JaiScript side uses explicit
			// by-ref parameters for the same in-place mutation. The Pure Script pair
			// above keeps the value-semantics JaiScript side for historical
			// comparability (that number is the value-semantics feature cost).
			const char* jai_ref_bst = R"(
				var ref_root = null;
				insertRef(ref_root, 8);
				insertRef(ref_root, 4);
				insertRef(ref_root, 12);
				insertRef(ref_root, 2);
				insertRef(ref_root, 6);
				insertRef(ref_root, 10);
				insertRef(ref_root, 14);
				insertRef(ref_root, 1);
				insertRef(ref_root, 3);
				insertRef(ref_root, 5);
				insertRef(ref_root, 7);
				insertRef(ref_root, 9);
				insertRef(ref_root, 11);
				insertRef(ref_root, 13);
				insertRef(ref_root, 15);

				var ref_sum = sumRef(ref_root);
				ref_sum;
			)";

			const char* lua_ref_bst = R"(
				local tree_root = TreeNode.new(8)
				tree_root = insertNode(tree_root, 4)
				tree_root = insertNode(tree_root, 12)
				tree_root = insertNode(tree_root, 2)
				tree_root = insertNode(tree_root, 6)
				tree_root = insertNode(tree_root, 10)
				tree_root = insertNode(tree_root, 14)
				tree_root = insertNode(tree_root, 1)
				tree_root = insertNode(tree_root, 3)
				tree_root = insertNode(tree_root, 5)
				tree_root = insertNode(tree_root, 7)
				tree_root = insertNode(tree_root, 9)
				tree_root = insertNode(tree_root, 11)
				tree_root = insertNode(tree_root, 13)
				tree_root = insertNode(tree_root, 15)

				local tree_sum = inorderSum(tree_root)
				return tree_sum
			)";

			// One-shot correctness gates: same insertion sequence as the value BST
			check_eq((int64_t)120, jai_engine->execute(jai_ref_bst).as_int());
			check_eq((int64_t)120, (int64_t)lua->script(lua_ref_bst).get<int64_t>());

			benchmark("JaiScript - BST by-ref (15 nodes)", [this, jai_ref_bst]() {
				jai_engine->execute(jai_ref_bst);
			});

			benchmark("Lua(sol2) - BST insert/sum (15 nodes)", [this, lua_ref_bst]() {
				lua->script(lua_ref_bst);
			});
		});

		test("JaiScript vs Lua(sol2): Binary Search Tree (By-Ref Script) [precompiled]", [this]() {
			benchmark("JaiScript - BST by-ref (15 nodes) [precompiled]", [this]() {
				jai_pre_bst.execute();
			});

			benchmark("Lua(sol2) - BST insert/sum (15 nodes) [precompiled]", [this]() {
				lua_pre_bst();
			});
		});

		// ===== Binary Search Tree Operations (C++ Bound - Fair Comparison) =====
		test("JaiScript vs Lua(sol2): Binary Search Tree (C++ Bound)", [this]() {
			// Same C++ TreeNode bound to both engines; isolates pure scripting
			// performance from class implementation.

			benchmark("JaiScript - C++ BST (15 nodes)", [this]() {
				jai_engine->execute(k_jai_cpp_bst);
			});

			benchmark("Lua(sol2) - C++ BST (15 nodes)", [this]() {
				lua->script(k_lua_cpp_bst);
			});
		});

		test("JaiScript vs Lua(sol2): Binary Search Tree (C++ Bound) [precompiled]", [this]() {
			benchmark("JaiScript - C++ BST (15 nodes) [precompiled]", [this]() {
				jai_pre_cpp_bst.execute();
			});

			benchmark("Lua(sol2) - C++ BST (15 nodes) [precompiled]", [this]() {
				lua_pre_cpp_bst();
			});
		});

#else
		test("Lua/sol2 Not Available", [this]() {
			std::cout << "\n";
			std::cout << "Lua (sol2) comparison benchmarks are DISABLED.\n";
			std::cout << "To enable: place Lua at Source/JaiScript/lua and sol2 at Source/JaiScript/sol2,\n";
			std::cout << "then reconfigure with -DJAISCRIPT_ENABLE_BENCHMARKS=ON\n";
		});
#endif
	}
};

FOUNDRY_REGISTER(jai::foundry::tests::sol2_comparison)

} // namespace jai::foundry::tests
