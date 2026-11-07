#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

#ifdef HAVE_ANGELSCRIPT
#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <scriptdictionary/scriptdictionary.h>
#include <scripthelper/scripthelper.h>
#include <scriptmath/scriptmath.h>
#include <scriptstdstring/scriptstdstring.h>
#include <cstdio>
#include <new>
#endif

using namespace jai;
using namespace jai::foundry;

// TU-local like sol2_comparison.cpp's: squirrel_comparison.cpp defines a same-named CppTreeNode at global scope.
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

#ifdef HAVE_ANGELSCRIPT
using CppTreeNodePointer = std::shared_ptr<CppTreeNode>;

void messageCallback(const asSMessageInfo* a_message, void*) {
	const char* kind = a_message->type == asMSGTYPE_ERROR ? "error" : a_message->type == asMSGTYPE_WARNING ? "warning" : "info";
	std::fprintf(stderr, "%s (%d, %d) : %s : %s\n", a_message->section, a_message->row, a_message->col, kind, a_message->message);
}

void constructEmptyCppTreeNode(void* a_memory) {
	new (a_memory) CppTreeNodePointer();
}

void constructCppTreeNode(int a_value, void* a_memory) {
	new (a_memory) CppTreeNodePointer(std::make_shared<CppTreeNode>(a_value));
}

void copyConstructCppTreeNode(const CppTreeNodePointer& a_other, void* a_memory) {
	new (a_memory) CppTreeNodePointer(a_other);
}

void destructCppTreeNode(void* a_memory) {
	static_cast<CppTreeNodePointer*>(a_memory)->~CppTreeNodePointer();
}

CppTreeNodePointer& assignCppTreeNode(const CppTreeNodePointer& a_other, CppTreeNodePointer* a_self) {
	*a_self = a_other;
	return *a_self;
}

// The script's CppTreeNode is the shared_ptr itself (a value type), so the helpers bind unwrapped, natively.
void registerCppTreeNode(asIScriptEngine* a_engine) {
	int r = a_engine->RegisterObjectType("CppTreeNode", sizeof(CppTreeNodePointer), asOBJ_VALUE | asGetTypeTraits<CppTreeNodePointer>());
	if (r >= 0) r = a_engine->RegisterObjectBehaviour("CppTreeNode", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(constructEmptyCppTreeNode), asCALL_CDECL_OBJLAST);
	if (r >= 0) r = a_engine->RegisterObjectBehaviour("CppTreeNode", asBEHAVE_CONSTRUCT, "void f(int)", asFUNCTION(constructCppTreeNode), asCALL_CDECL_OBJLAST);
	if (r >= 0) r = a_engine->RegisterObjectBehaviour("CppTreeNode", asBEHAVE_CONSTRUCT, "void f(const CppTreeNode &in)", asFUNCTION(copyConstructCppTreeNode), asCALL_CDECL_OBJLAST);
	if (r >= 0) r = a_engine->RegisterObjectBehaviour("CppTreeNode", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(destructCppTreeNode), asCALL_CDECL_OBJLAST);
	if (r >= 0) r = a_engine->RegisterObjectMethod("CppTreeNode", "CppTreeNode& opAssign(const CppTreeNode &in)", asFUNCTION(assignCppTreeNode), asCALL_CDECL_OBJLAST);
	if (r >= 0) r = a_engine->RegisterGlobalFunction("CppTreeNode cpp_insertNode(CppTreeNode, int)", asFUNCTION(cpp_insertNode), asCALL_CDECL);
	if (r >= 0) r = a_engine->RegisterGlobalFunction("int cpp_inorderSum(CppTreeNode)", asFUNCTION(cpp_inorderSum), asCALL_CDECL);
	if (r >= 0) r = a_engine->RegisterGlobalFunction("int cpp_treeHeight(CppTreeNode)", asFUNCTION(cpp_treeHeight), asCALL_CDECL);
	if (r >= 0) r = a_engine->RegisterGlobalFunction("CppTreeNode cpp_rotateRight(CppTreeNode)", asFUNCTION(cpp_rotateRight), asCALL_CDECL);
	if (r < 0) {
		throw std::runtime_error("AngelScript CppTreeNode registration failed: " + std::to_string(r));
	}
}
#endif

} // namespace

namespace jai::foundry::tests {

#ifdef HAVE_ANGELSCRIPT
// One engine, one module built from every declaration at once, one reused context. execute() is the per-iteration
// body of the plain rows: ExecuteString compiles the code as a function in the module's scope, then runs it.
class AngelScriptHost {
public:
	asIScriptEngine* engine = nullptr;
	asIScriptModule* module = nullptr;
	asIScriptContext* context = nullptr;

	AngelScriptHost() {
		engine = asCreateScriptEngine();
		if (!engine) {
			throw std::runtime_error("asCreateScriptEngine failed");
		}
		engine->SetMessageCallback(asFUNCTION(messageCallback), nullptr, asCALL_CDECL);
		RegisterStdString(engine);
		RegisterScriptArray(engine, true);
		RegisterScriptMath(engine);
		RegisterScriptDictionary(engine);
		registerCppTreeNode(engine);
		module = engine->GetModule("bench", asGM_ALWAYS_CREATE);
		context = engine->CreateContext();
	}

	AngelScriptHost(const AngelScriptHost&) = delete;
	AngelScriptHost& operator=(const AngelScriptHost&) = delete;

	~AngelScriptHost() {
		if (context) {
			context->Release();
		}
		engine->ShutDownAndRelease();
	}

	void build(const char* a_source) {
		if (module->AddScriptSection("bench", a_source) < 0 || module->Build() < 0) {
			throw std::runtime_error("AngelScript module build failed");
		}
	}

	void execute(const char* a_code) {
		checkStatus(ExecuteString(engine, a_code, module, context), a_code);
	}

	int evalInt(const std::string& a_statements, const char* a_expression) {
		int result = 0;
		std::string code = a_statements + "\nreturn " + a_expression + ";";
		checkStatus(ExecuteString(engine, code.c_str(), &result, asTYPEID_INT32, module, context), code.c_str());
		return result;
	}

	double evalDouble(const std::string& a_statements, const char* a_expression) {
		double result = 0.0;
		std::string code = a_statements + "\nreturn " + a_expression + ";";
		checkStatus(ExecuteString(engine, code.c_str(), &result, asTYPEID_DOUBLE, module, context), code.c_str());
		return result;
	}

	void checkStatus(int a_status, const char* a_code) {
		if (a_status == asEXECUTION_EXCEPTION) {
			throw std::runtime_error("AngelScript exception: " + GetExceptionInfo(context, true));
		}
		if (a_status != asEXECUTION_FINISHED) {
			throw std::runtime_error("AngelScript failed (" + std::to_string(a_status) + ") running:\n" + a_code);
		}
	}
};

// Function compiled once into the module's scope (CompileFunction) and retained; call() is the per-iteration body of
// the [precompiled] variants: Prepare (the same function again, so the context reuses its setup) and Execute.
class AngelScriptFunction {
public:
	AngelScriptFunction() = default;
	AngelScriptFunction(const AngelScriptFunction&) = delete;
	AngelScriptFunction& operator=(const AngelScriptFunction&) = delete;

	~AngelScriptFunction() {
		if (function) {
			function->Release();
		}
	}

	void compile(AngelScriptHost& a_host, const char* a_body, const char* a_name) {
		host = &a_host;
		std::string code = std::string("void ") + a_name + "() {\n" + a_body + "\n}";
		if (host->module->CompileFunction(a_name, code.c_str(), 0, 0, &function) < 0) {
			throw std::runtime_error(std::string("AngelScript precompile failed: ") + a_name);
		}
	}

	void call() {
		host->context->Prepare(function);
		int status = host->context->Execute();
		if (status != asEXECUTION_FINISHED) {
			throw std::runtime_error("AngelScript precompiled call failed");
		}
	}

	AngelScriptHost* host = nullptr;
	asIScriptFunction* function = nullptr;
};

constexpr const char* k_as_module = R"(
	int add(int a, int b) { return a + b; }

	class Point {
		double x = 0.0;
		double y = 0.0;
		Point(double px, double py) { x = px; y = py; }
	}

	class Calculator {
		int add(int a, int b) { return a + b; }
	}

	int factorial(int n) {
		if (n <= 1) { return 1; }
		return n * factorial(n - 1);
	}

	int fib(int n) {
		if (n <= 1) { return n; }
		return fib(n - 1) + fib(n - 2);
	}

	int recurseWithLocals(int depth) {
		if (depth <= 0) { return 0; }

		int a = depth * 2;
		int b = depth + 3;
		int c = a + b;
		int d = c * 2;
		int e = d - a;
		int f = e + b;
		int g = f * depth;
		int h = g - c;
		int i = h + d;
		int j = i - e;

		int sum = a + b + c + d + e + f + g + h + i + j;

		return sum + recurseWithLocals(depth - 1);
	}

	class TreeNode {
		int value = 0;
		TreeNode@ left;
		TreeNode@ right;

		TreeNode(int val) {
			value = val;
		}
	}

	TreeNode@ insertNode(TreeNode@ root, int val) {
		if (root is null) {
			return TreeNode(val);
		}
		if (val < root.value) {
			@root.left = insertNode(root.left, val);
		} else {
			@root.right = insertNode(root.right, val);
		}
		return root;
	}

	int inorderSum(TreeNode@ node) {
		if (node is null) { return 0; }
		return inorderSum(node.left) + node.value + inorderSum(node.right);
	}

	int treeHeight(TreeNode@ node) {
		if (node is null) { return 0; }
		int leftH = treeHeight(node.left);
		int rightH = treeHeight(node.right);
		if (leftH > rightH) { return 1 + leftH; }
		return 1 + rightH;
	}

	TreeNode@ rotateRight(TreeNode@ y) {
		if (y is null) { return null; }
		if (y.left is null) { return y; }
		TreeNode@ x = y.left;
		@y.left = x.right;
		@x.right = y;
		return x;
	}

	array<int> testArr = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	Point@ fallbackPoint = Point(42.0, 0.0);
)";
#endif

// Each row's script on both sides, shared by its benchmark and its correctness gate.
constexpr const char* k_jai_var_ops = "auto x = 10; auto y = 20; auto z = x + y;";
constexpr const char* k_as_var_ops = "int x = 10; int y = 20; int z = x + y;";
constexpr const char* k_jai_array = R"(
	auto arr = [];
	arr.push(1); arr.push(2); arr.push(3);
	arr.pop();
	arr.size();
)";
constexpr const char* k_as_array = R"(
	array<int> arr;
	arr.insertLast(1); arr.insertLast(2); arr.insertLast(3);
	arr.removeLast();
	arr.length();
)";
constexpr const char* k_jai_map = R"(
	auto m = {};
	m["key1"] = 100;
	m["key2"] = 200;
	auto val = m["key1"];
)";
constexpr const char* k_as_map = R"(
	dictionary m;
	m["key1"] = 100;
	m["key2"] = 200;
	int val = int(m["key1"]);
)";
constexpr const char* k_jai_class = "auto p = Point(3.0, 4.0);";
constexpr const char* k_as_class = "Point p(3.0, 4.0);";
constexpr const char* k_jai_method = "auto calc = Calculator(); calc.add(5, 3);";
constexpr const char* k_as_method = "Calculator calc; calc.add(5, 3);";
constexpr const char* k_jai_for_loop = R"(
	auto sum = 0;
	for (auto i = 0; i < 100; ++i) { sum += i; }
)";
constexpr const char* k_as_for_loop = R"(
	int sum = 0;
	for (int i = 0; i < 100; ++i) { sum += i; }
)";
constexpr const char* k_jai_range_for = R"(
	auto s = 0;
	for (auto x : testArr) { s += x; }
)";
constexpr const char* k_as_range_for = R"(
	int s = 0;
	foreach (int x : testArr) { s += x; }
)";
constexpr const char* k_jai_str_concat = R"(
	auto s = "";
	for (auto i = 0; i < 20; ++i) { s = s + "x"; }
)";
constexpr const char* k_as_str_concat = R"(
	string s = "";
	for (int i = 0; i < 20; ++i) { s = s + "x"; }
)";
constexpr const char* k_jai_null_check = R"(
	var x = null;
	if (x == null) { x = 42; }
)";
// No nullable int in AngelScript: null lives in handles, so the row null-checks a handle and assigns one.
constexpr const char* k_as_null_check = R"(
	Point@ x = null;
	if (x is null) { @x = fallbackPoint; }
)";
constexpr const char* k_jai_hot_loop = R"(
	auto sum = 0;
	for (auto i = 0; i < 1000; ++i) { sum += i; }
)";
constexpr const char* k_as_hot_loop = R"(
	int sum = 0;
	for (int i = 0; i < 1000; ++i) { sum += i; }
)";
constexpr const char* k_jai_bst = R"(
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
)";
constexpr const char* k_as_bst = R"(
	TreeNode@ tree_root = TreeNode(8);
	@tree_root = insertNode(tree_root, 4);
	@tree_root = insertNode(tree_root, 12);
	@tree_root = insertNode(tree_root, 2);
	@tree_root = insertNode(tree_root, 6);
	@tree_root = insertNode(tree_root, 10);
	@tree_root = insertNode(tree_root, 14);
	@tree_root = insertNode(tree_root, 1);
	@tree_root = insertNode(tree_root, 3);
	@tree_root = insertNode(tree_root, 5);
	@tree_root = insertNode(tree_root, 7);
	@tree_root = insertNode(tree_root, 9);
	@tree_root = insertNode(tree_root, 11);
	@tree_root = insertNode(tree_root, 13);
	@tree_root = insertNode(tree_root, 15);

	int tree_sum = inorderSum(tree_root);
	int tree_height = treeHeight(tree_root);
	@tree_root = rotateRight(tree_root);
	tree_sum = inorderSum(tree_root);
)";
constexpr const char* k_jai_ref_bst = R"(
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
constexpr const char* k_as_ref_bst = R"(
	TreeNode@ tree_root = TreeNode(8);
	@tree_root = insertNode(tree_root, 4);
	@tree_root = insertNode(tree_root, 12);
	@tree_root = insertNode(tree_root, 2);
	@tree_root = insertNode(tree_root, 6);
	@tree_root = insertNode(tree_root, 10);
	@tree_root = insertNode(tree_root, 14);
	@tree_root = insertNode(tree_root, 1);
	@tree_root = insertNode(tree_root, 3);
	@tree_root = insertNode(tree_root, 5);
	@tree_root = insertNode(tree_root, 7);
	@tree_root = insertNode(tree_root, 9);
	@tree_root = insertNode(tree_root, 11);
	@tree_root = insertNode(tree_root, 13);
	@tree_root = insertNode(tree_root, 15);

	int tree_sum = inorderSum(tree_root);
)";
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
constexpr const char* k_as_cpp_bst = R"(
	CppTreeNode root(8);
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

	int sum = cpp_inorderSum(root);
	int height = cpp_treeHeight(root);
	root = cpp_rotateRight(root);
	sum = cpp_inorderSum(root);
)";

class angelscript_comparison : public suite {
public:
	angelscript_comparison() : suite("AngelScript Performance Comparison") {
		std::cout << "\n==============================================\n";
		std::cout << "Initializing engines for AngelScript comparison...\n";
		std::cout << "==============================================\n";

		std::cout << "Creating JaiScript engine...\n";
		jai_engine = make_engine();
		jai::stdlib::register_all(jai_engine);
		std::cout << "JaiScript engine ready.\n";

#ifdef HAVE_ANGELSCRIPT
		std::cout << "Creating AngelScript engine " << ANGELSCRIPT_VERSION_STRING << "...\n";
		try {
			as = std::make_unique<AngelScriptHost>();
			as->build(k_as_module);
			if (as->evalInt("", "42") != 42) {
				throw std::runtime_error("AngelScript basic eval test failed");
			}
			std::cout << "AngelScript engine ready.\n";
		} catch (const std::exception& e) {
			std::cerr << "ERROR: AngelScript initialization failed: " << e.what() << "\n";
			as.reset();
		}
#else
		std::cout << "AngelScript not available - comparison benchmarks disabled.\n";
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

		jai_engine->execute(R"(
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

		// By-ref BST: reference params mutate the tree in place (matches AngelScript's handles to reference-type
		// script objects, which never deep-copy)
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

		jai_engine->execute("var root = null; var sum = 0; var height = 0;");

		// ===== [precompiled] setup: both sides parse/compile ONCE here =====
		// The per-iteration pairs measure execute(src): JaiScript's 64-entry source cache skips re-parsing there while
		// AngelScript's ExecuteString recompiles every call, so those numbers mix AngelScript compile cost into the
		// ratio. These variants remove the asymmetry: jaibite vs a retained compiled function, execution only.
		jai_pre_int_add = jai_engine->jaibite("42 + 58;");
		jai_pre_func_call = jai_engine->jaibite("add(10, 20);");
		jai_pre_method = jai_engine->jaibite(k_jai_method);
		jai_pre_fib = jai_engine->jaibite("fib(15);");
		jai_pre_hot_loop = jai_engine->jaibite(k_jai_hot_loop);
		jai_pre_bst = jai_engine->jaibite(k_jai_ref_bst);

		check_eq((int64_t)100, jai_pre_int_add.execute().as_int());
		check_eq((int64_t)30, jai_pre_func_call.execute().as_int());
		check_eq((int64_t)8, jai_pre_method.execute().as_int());
		check_eq((int64_t)610, jai_pre_fib.execute().as_int());
		jai_pre_hot_loop.execute();
		check_eq((int64_t)499500, jai_engine->execute("sum;").as_int());
		check_eq((int64_t)120, jai_pre_bst.execute().as_int());

#ifdef HAVE_ANGELSCRIPT
		if (as) {
			as_pre_int_add.compile(*as, "42 + 58;", "pre_int_add");
			as_pre_func_call.compile(*as, "add(10, 20);", "pre_func_call");
			as_pre_method.compile(*as, k_as_method, "pre_method");
			as_pre_fib.compile(*as, "fib(15);", "pre_fib");
			as_pre_hot_loop.compile(*as, k_as_hot_loop, "pre_hot_loop");
			as_pre_bst.compile(*as, k_as_ref_bst, "pre_bst");
		}
#endif
	}

	std::shared_ptr<jai::engine> jai_engine;
	jai::jaibite jai_pre_int_add, jai_pre_func_call, jai_pre_method, jai_pre_fib, jai_pre_hot_loop, jai_pre_bst;
#ifdef HAVE_ANGELSCRIPT
	std::unique_ptr<AngelScriptHost> as;
	// Declared after as so they release their functions before the engine shuts down
	AngelScriptFunction as_pre_int_add, as_pre_func_call, as_pre_method, as_pre_fib, as_pre_hot_loop, as_pre_bst;

	int64_t jaiInt(const char* a_source, const char* a_expression) {
		return jai_engine->execute(std::string(a_source) + "\n" + a_expression + ";").as_int();
	}

	double jaiFloat(const char* a_source, const char* a_expression) {
		return jai_engine->execute(std::string(a_source) + "\n" + a_expression + ";").as_float();
	}

	// Correctness gate: the row's AngelScript script ends with the value its JaiScript twin ends with
	void gateInt(const char* a_jaiSource, const char* a_jaiExpression, const char* a_asSource, const char* a_asExpression) {
		int64_t expected = jaiInt(a_jaiSource, a_jaiExpression);
		check_eq(expected, (int64_t)as->evalInt(a_asSource, a_asExpression), std::string("AngelScript ") + a_asExpression);
	}

	void gateFloat(const char* a_jaiSource, const char* a_jaiExpression, const char* a_asSource, const char* a_asExpression) {
		check_near(jaiFloat(a_jaiSource, a_jaiExpression), as->evalDouble(a_asSource, a_asExpression), 1e-9);
	}
#endif

	void forge_tests() override {
#ifdef HAVE_ANGELSCRIPT
		if (!as) {
			test("AngelScript Initialization Failed", [this]() {
				std::cout << "AngelScript engine failed to initialize. Skipping comparison benchmarks.\n";
			});
			return;
		}

		// ===== Integer Addition =====
		test("JaiScript vs AngelScript: Integer Addition", [this]() {
			gateInt("", "42 + 58", "", "42 + 58");

			benchmark("JaiScript - Integer Addition", [this]() {
				jai_engine->execute("42 + 58;");
			}, 5000);

			benchmark("AngelScript - Integer Addition", [this]() {
				as->execute("42 + 58;");
			}, 5000);
		});

		// Same pair with the compile cost removed on BOTH sides (jaibite vs retained function); the plain pair above
		// keeps measuring the realistic execute(src) path.
		test("JaiScript vs AngelScript: Integer Addition [precompiled]", [this]() {
			as_pre_int_add.call();

			benchmark("JaiScript - Integer Addition [precompiled]", [this]() {
				jai_pre_int_add.execute();
			}, 5000);

			benchmark("AngelScript - Integer Addition [precompiled]", [this]() {
				as_pre_int_add.call();
			}, 5000);
		});

		// ===== Float Multiplication =====
		test("JaiScript vs AngelScript: Float Multiplication", [this]() {
			gateFloat("", "3.14 * 2.71", "", "3.14 * 2.71");

			benchmark("JaiScript - Float Multiplication", [this]() {
				jai_engine->execute("3.14 * 2.71;");
			}, 5000);

			benchmark("AngelScript - Float Multiplication", [this]() {
				as->execute("3.14 * 2.71;");
			}, 5000);
		});

		// ===== Variable Operations =====
		test("JaiScript vs AngelScript: Variable Operations", [this]() {
			gateInt(k_jai_var_ops, "z", k_as_var_ops, "z");

			benchmark("JaiScript - Variable Operations", [this]() {
				jai_engine->execute(k_jai_var_ops);
			});

			benchmark("AngelScript - Variable Operations", [this]() {
				as->execute(k_as_var_ops);
			});
		});

		// ===== Function Calls =====
		test("JaiScript vs AngelScript: Function Calls", [this]() {
			gateInt("", "add(10, 20)", "", "add(10, 20)");

			benchmark("JaiScript - Function Calls", [this]() {
				jai_engine->execute("add(10, 20);");
			});

			benchmark("AngelScript - Function Calls", [this]() {
				as->execute("add(10, 20);");
			});
		});

		test("JaiScript vs AngelScript: Function Calls [precompiled]", [this]() {
			as_pre_func_call.call();

			benchmark("JaiScript - Function Calls [precompiled]", [this]() {
				jai_pre_func_call.execute();
			});

			benchmark("AngelScript - Function Calls [precompiled]", [this]() {
				as_pre_func_call.call();
			});
		});

		// ===== Array Push/Pop =====
		test("JaiScript vs AngelScript: Array Operations", [this]() {
			gateInt(k_jai_array, "arr.size()", k_as_array, "int(arr.length())");

			benchmark("JaiScript - Array Push/Pop", [this]() {
				jai_engine->execute(k_jai_array);
			});

			benchmark("AngelScript - Array Push/Pop", [this]() {
				as->execute(k_as_array);
			});
		});

		// ===== Map/Dictionary Insert/Lookup =====
		test("JaiScript vs AngelScript: Map/Dictionary Operations", [this]() {
			gateInt(k_jai_map, "val", k_as_map, "val");

			benchmark("JaiScript - Map Insert/Lookup", [this]() {
				jai_engine->execute(k_jai_map);
			});

			benchmark("AngelScript - Dictionary Insert/Lookup", [this]() {
				as->execute(k_as_map);
			});
		});

		// ===== Class Creation =====
		test("JaiScript vs AngelScript: Class Creation", [this]() {
			gateFloat(k_jai_class, "p.x + p.y", k_as_class, "p.x + p.y");

			benchmark("JaiScript - Class Creation", [this]() {
				jai_engine->execute(k_jai_class);
			});

			benchmark("AngelScript - Class Creation", [this]() {
				as->execute(k_as_class);
			});
		});

		// ===== Method Invocation =====
		test("JaiScript vs AngelScript: Method Invocation", [this]() {
			gateInt(k_jai_method, "calc.add(5, 3)", k_as_method, "calc.add(5, 3)");

			benchmark("JaiScript - Method Invocation", [this]() {
				jai_engine->execute(k_jai_method);
			});

			benchmark("AngelScript - Method Invocation", [this]() {
				as->execute(k_as_method);
			});
		});

		test("JaiScript vs AngelScript: Method Invocation [precompiled]", [this]() {
			as_pre_method.call();

			benchmark("JaiScript - Method Invocation [precompiled]", [this]() {
				jai_pre_method.execute();
			});

			benchmark("AngelScript - Method Invocation [precompiled]", [this]() {
				as_pre_method.call();
			});
		});

		// ===== For Loop =====
		test("JaiScript vs AngelScript: For Loop (100 iterations)", [this]() {
			gateInt(k_jai_for_loop, "sum", k_as_for_loop, "sum");

			benchmark("JaiScript - For Loop", [this]() {
				jai_engine->execute(k_jai_for_loop);
			});

			benchmark("AngelScript - For Loop", [this]() {
				as->execute(k_as_for_loop);
			});
		});

		// ===== Factorial (Recursion) =====
		test("JaiScript vs AngelScript: Factorial (Recursion)", [this]() {
			gateInt("", "factorial(10)", "", "factorial(10)");

			benchmark("JaiScript - Factorial(10)", [this]() {
				jai_engine->execute("factorial(10);");
			});

			benchmark("AngelScript - Factorial(10)", [this]() {
				as->execute("factorial(10);");
			});
		});

		// ===== Fibonacci (Deep Recursion) =====
		test("JaiScript vs AngelScript: Fibonacci (Deep Recursion)", [this]() {
			gateInt("", "fib(15)", "", "fib(15)");

			benchmark("JaiScript - Fibonacci(15)", [this]() {
				jai_engine->execute("fib(15);");
			});

			benchmark("AngelScript - Fibonacci(15)", [this]() {
				as->execute("fib(15);");
			});
		});

		test("JaiScript vs AngelScript: Fibonacci (Deep Recursion) [precompiled]", [this]() {
			as_pre_fib.call();

			benchmark("JaiScript - Fibonacci(15) [precompiled]", [this]() {
				jai_pre_fib.execute();
			});

			benchmark("AngelScript - Fibonacci(15) [precompiled]", [this]() {
				as_pre_fib.call();
			});
		});

		// ===== Recursion with 10 Local Variables =====
		test("JaiScript vs AngelScript: Recursion with 10 Locals", [this]() {
			// Each call creates 10 locals and uses them all before recursing; depth 15 = 15 frames.
			gateInt("", "recurseWithLocals(15)", "", "recurseWithLocals(15)");

			benchmark("JaiScript - Recurse with Locals (depth=15)", [this]() {
				jai_engine->execute("recurseWithLocals(15);");
			});

			benchmark("AngelScript - Recurse with Locals (depth=15)", [this]() {
				as->execute("recurseWithLocals(15);");
			});
		});

		// ===== Foreach / Range-For =====
		test("JaiScript vs AngelScript: Foreach Loop", [this]() {
			// AngelScript's testArr is a module global (a function body cannot declare one)
			jai_engine->execute("auto testArr = [0,1,2,3,4,5,6,7,8,9];");
			gateInt(k_jai_range_for, "s", k_as_range_for, "s");

			benchmark("JaiScript - Range-For (10 elements)", [this]() {
				jai_engine->execute(k_jai_range_for);
			});

			benchmark("AngelScript - Foreach (10 elements)", [this]() {
				as->execute(k_as_range_for);
			});
		});

		// ===== String Operations =====
		test("JaiScript vs AngelScript: String Concatenation", [this]() {
			gateInt(k_jai_str_concat, "s.size()", k_as_str_concat, "int(s.length())");

			benchmark("JaiScript - String Concat", [this]() {
				jai_engine->execute(k_jai_str_concat);
			});

			benchmark("AngelScript - String Concat", [this]() {
				as->execute(k_as_str_concat);
			});
		});

		// ===== Null Handling =====
		test("JaiScript vs AngelScript: Null Handling", [this]() {
			gateInt(k_jai_null_check, "x", k_as_null_check, "int(x.x)");

			benchmark("JaiScript - Null Check", [this]() {
				jai_engine->execute(k_jai_null_check);
			});

			benchmark("AngelScript - Null Check", [this]() {
				as->execute(k_as_null_check);
			});
		});

		// ===== Hot Loop (1000 iterations) =====
		test("JaiScript vs AngelScript: Hot Loop (1000 iterations)", [this]() {
			gateInt(k_jai_hot_loop, "sum", k_as_hot_loop, "sum");

			benchmark("JaiScript - Hot Loop (1000 iter)", [this]() {
				jai_engine->execute(k_jai_hot_loop);
			});

			benchmark("AngelScript - Hot Loop (1000 iter)", [this]() {
				as->execute(k_as_hot_loop);
			});
		});

		test("JaiScript vs AngelScript: Hot Loop (1000 iterations) [precompiled]", [this]() {
			as_pre_hot_loop.call();

			benchmark("JaiScript - Hot Loop (1000 iter) [precompiled]", [this]() {
				jai_pre_hot_loop.execute();
			});

			benchmark("AngelScript - Hot Loop (1000 iter) [precompiled]", [this]() {
				as_pre_hot_loop.call();
			});
		});

		// ===== Binary Search Tree Operations (Pure Script) =====
		test("JaiScript vs AngelScript: Binary Search Tree (Pure Script)", [this]() {
			// Object creation, field access, recursion, pointer chasing: a 15-node BST, traversed, measured and rotated.
			gateInt(k_jai_bst, "tree_sum", k_as_bst, "tree_sum");
			gateInt(k_jai_bst, "tree_height", k_as_bst, "tree_height");

			benchmark("JaiScript - BST (15 nodes) [naive by-value]", [this]() {
				jai_engine->execute(k_jai_bst);
			});

			benchmark("AngelScript - BST (15 nodes)", [this]() {
				as->execute(k_as_bst);
			});
		});

		// ===== Binary Search Tree (By-Ref Script) =====
		test("JaiScript vs AngelScript: Binary Search Tree (By-Ref Script)", [this]() {
			// Apples-to-apples: AngelScript script objects are reference types held through handles (insertNode
			// mutates one tree, zero deep copies), so the JaiScript side uses explicit by-ref parameters for the same
			// in-place mutation. The Pure Script pair above keeps the value-semantics JaiScript side.
			check_eq((int64_t)120, jai_engine->execute(k_jai_ref_bst).as_int());
			gateInt(k_jai_ref_bst, "ref_sum", k_as_ref_bst, "tree_sum");

			benchmark("JaiScript - BST by-ref (15 nodes)", [this]() {
				jai_engine->execute(k_jai_ref_bst);
			});

			benchmark("AngelScript - BST insert/sum (15 nodes)", [this]() {
				as->execute(k_as_ref_bst);
			});
		});

		test("JaiScript vs AngelScript: Binary Search Tree (By-Ref Script) [precompiled]", [this]() {
			as_pre_bst.call();

			benchmark("JaiScript - BST by-ref (15 nodes) [precompiled]", [this]() {
				jai_pre_bst.execute();
			});

			benchmark("AngelScript - BST insert/sum (15 nodes) [precompiled]", [this]() {
				as_pre_bst.call();
			});
		});

		// ===== Binary Search Tree Operations (C++ Bound - Fair Comparison) =====
		test("JaiScript vs AngelScript: Binary Search Tree (C++ Bound)", [this]() {
			// The same C++ TreeNode and helpers bound to both engines (AngelScript through its native calling
			// convention); isolates pure scripting performance from class implementation.
			gateInt(k_jai_cpp_bst, "sum", k_as_cpp_bst, "sum");
			gateInt(k_jai_cpp_bst, "height", k_as_cpp_bst, "height");

			benchmark("JaiScript - C++ BST (15 nodes)", [this]() {
				jai_engine->execute(k_jai_cpp_bst);
			});

			benchmark("AngelScript - C++ BST (15 nodes)", [this]() {
				as->execute(k_as_cpp_bst);
			});
		});

#else
		test("AngelScript Not Available", [this]() {
			std::cout << "\n";
			std::cout << "AngelScript comparison benchmarks are DISABLED.\n";
			std::cout << "To enable: unpack the AngelScript 2.38 SDK so third_party/angelscript/angelscript/include/angelscript.h\n";
			std::cout << "exists (or point COMPARISONS_ANGELSCRIPT_ROOT at it) and reconfigure.\n";
		});
#endif
	}
};

FOUNDRY_REGISTER(jai::foundry::tests::angelscript_comparison)

} // namespace jai::foundry::tests
