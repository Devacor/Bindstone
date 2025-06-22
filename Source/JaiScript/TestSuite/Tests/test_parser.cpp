#include "../jai_test.hpp"
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/ast.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

// Helper to parse a string
std::vector<DeclarationPtr> parse(const std::string& code) {
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    Parser parser(tokens, "test.jai");
    return parser.parse();
}

// Helper to check parsing succeeds
void assertParseSucceeds(const std::string& code) {
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    Parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    expect_false(parser.hasErrors());
}

// Helper to check parsing fails
void assertParseFails(const std::string& code) {
    Lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    Parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    expect_true(parser.hasErrors());
}

JAI_TEST_SUITE(ParserTests)

JAI_TEST(expression_literals) {
    assertParseSucceeds("42;");
    assertParseSucceeds("3.14;");
    assertParseSucceeds("\"hello\";");
    assertParseSucceeds("'x';");
    assertParseSucceeds("true;");
    assertParseSucceeds("false;");
    assertParseSucceeds("null;");
}

JAI_TEST(expression_identifiers) {
    assertParseSucceeds("x;");
    assertParseSucceeds("foo_bar;");
    assertParseSucceeds("_private;");
}

JAI_TEST(binary_operators) {
    assertParseSucceeds("1 + 2;");
    assertParseSucceeds("x * y;");
    assertParseSucceeds("a && b || c;");
    assertParseSucceeds("x == y;");
    assertParseSucceeds("a < b;");
}

JAI_TEST(unary_operators) {
    assertParseSucceeds("-x;");
    assertParseSucceeds("!flag;");
    assertParseSucceeds("++i;");
    assertParseSucceeds("--count;");
    assertParseSucceeds("&value;");
}

JAI_TEST(assignment_operators) {
    assertParseSucceeds("x = 42;");
    assertParseSucceeds("y += 10;");
    assertParseSucceeds("z *= 2;");
}

JAI_TEST(ternary_operator) {
    assertParseSucceeds("x ? y : z;");
    assertParseSucceeds("a > b ? a : b;");
    assertParseSucceeds("flag ? 1 : 0;");
}

JAI_TEST(function_calls) {
    assertParseSucceeds("foo();");
    assertParseSucceeds("bar(1, 2, 3);");
    assertParseSucceeds("obj.method();");
    assertParseSucceeds("ptr->func(x, y);");
}

JAI_TEST(member_access) {
    assertParseSucceeds("obj.field;");
    assertParseSucceeds("ptr->member;");
    assertParseSucceeds("obj.method().field;");
}

JAI_TEST(array_access) {
    assertParseSucceeds("arr[0];");
    assertParseSucceeds("matrix[i][j];");
    assertParseSucceeds("mymap[\"key\"];");
}

JAI_TEST(new_expressions) {
    assertParseSucceeds("new MyClass();");
    assertParseSucceeds("new Point(x, y);");
    assertParseSucceeds("new array<int>();");
}

JAI_TEST(lambda_expressions) {
    assertParseSucceeds("[](){ return 42; };");
    assertParseSucceeds("[x](int y){ return x + y; };");
    assertParseSucceeds("[&total](int n) -> int { return total += n; };");
}

JAI_TEST(statement_blocks) {
    assertParseSucceeds("{ }");
    assertParseSucceeds("{ x = 1; y = 2; }");
    assertParseSucceeds("{ { nested; } }");
}

JAI_TEST(if_statements) {
    assertParseSucceeds("if (x > 0) y = 1;");
    assertParseSucceeds("if (flag) { foo(); }");
    assertParseSucceeds("if (a) b(); else c();");
    assertParseSucceeds("if (x) { } else if (y) { } else { }");
}

JAI_TEST(while_loops) {
    assertParseSucceeds("while (true) { }");
    assertParseSucceeds("while (i < 10) i++;");
    assertParseSucceeds("while (flag) { break; }");
}

JAI_TEST(for_loops) {
    assertParseSucceeds("for (int i = 0; i < 10; i++) { }");
    assertParseSucceeds("for (;;) break;");
    assertParseSucceeds("for (auto x = start; x != end; x++) sum += x;");
}

JAI_TEST(control_flow_statements) {
    assertParseSucceeds("return;");
    assertParseSucceeds("return 42;");
    assertParseSucceeds("break;");
    assertParseSucceeds("continue;");
}

JAI_TEST(variable_declarations) {
    assertParseSucceeds("int x;");
    assertParseSucceeds("float y = 3.14;");
    assertParseSucceeds("string name = \"test\";");
    assertParseSucceeds("auto value = null;");
    assertParseSucceeds("var dynamic = 42;");
}

JAI_TEST(function_declarations) {
    assertParseSucceeds("void foo() { }");
    assertParseSucceeds("int add(int a, int b) { return a + b; }");
    assertParseSucceeds("auto compute() -> float { return 3.14; }");
}

JAI_TEST(class_declarations) {
    assertParseSucceeds("class Empty { }");
    assertParseSucceeds(R"(
        class Point {
            float x;
            float y;
        }
    )");
    assertParseSucceeds(R"(
        class Circle : Shape {
        public:
            float radius;
            float area() { return 3.14 * radius * radius; }
        private:
            int id;
        }
    )");
}

JAI_TEST(complex_expressions) {
    assertParseSucceeds("result = (a + b) * (c - d) / e;");
    assertParseSucceeds("x = arr[i + 1][j * 2];");
    assertParseSucceeds("value = obj.method(x, y).field[index];");
}

JAI_TEST(method_chaining) {
    assertParseSucceeds("a.b.c.d();");
    assertParseSucceeds("ptr->next->next->value;");
    assertParseSucceeds("factory().create().initialize().run();");
}

JAI_TEST(parser_error_handling) {
    // Missing semicolon 
    assertParseFails("int x");    // declarations need semicolons
    assertParseSucceeds("x = 42"); // last expression can omit semicolon
    
    // Unmatched delimiters
    assertParseFails("(");
    assertParseFails("{");
    assertParseFails("[");
    assertParseFails("(]");
    assertParseFails("{)");
    
    // Invalid expressions
    assertParseFails("= 42;");
    assertParseFails("++;");
    assertParseFails("if while;");
    
    // Invalid declarations
    assertParseFails("int;");
    assertParseFails("int 123;");
    assertParseFails("class;");
    assertParseFails("class 123 { }");
}

JAI_TEST(range_based_for_loops) {
    assertParseSucceeds("for (auto x : container) { }");
    assertParseSucceeds("for (auto& element : collection) { element *= 2; }");
    assertParseSucceeds("for (auto& item : items) total += item.value;");
    assertParseSucceeds("for (int x : numbers) { }");
    assertParseSucceeds("for (auto x : list) { }");
    
    // Nested range-based for
    assertParseSucceeds(R"(
        for (auto row : matrix) {
            for (auto cell : row) {
                print(cell);
            }
        }
    )");
}

JAI_TEST(reference_parameters) {
    // Function declarations with ref
    assertParseSucceeds("void foo(int& x) { }");
    assertParseSucceeds("void bar(int& x, float& y) { }");
    assertParseSucceeds("void process(MyClass& obj) { }");
    assertParseSucceeds("void handle(string& str) { }");
    assertParseSucceeds("void work(array<int>& arr) { }");
    
    // Mix of parameter types
    assertParseSucceeds("void mixed(int val, string& ref, bool flag) { }");
    assertParseSucceeds("void complex(A& a, B& b, C c, D& d) { }");
    
    // Nested template types
    assertParseSucceeds("void nested(map<string, array<int>>& data) { }");
    assertParseSucceeds("void multi(array<float>& arr) { }");
    
    // Return ref
    assertParseSucceeds("int& getRef() { return x; }");
    assertParseSucceeds("string& getName() { return name; }");
    
    // Lambda with ref
    assertParseSucceeds("auto f = [](int& x) { return x * 2; };");
    assertParseSucceeds("auto g = [](string& s1, string& s2) { return s1 + s2; };");
    assertParseSucceeds("auto h = [](int a, float& b, char c) { return a + b; };");
    assertParseSucceeds("auto i = [&total](int& x) { total += x; };");
    
    // Variable declarations with ref
    assertParseSucceeds("int& x = getValue();");
    assertParseSucceeds("auto& y = getValue();");
    
    // Range-for with ref works
    assertParseSucceeds("for (auto& item : container) { }");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()