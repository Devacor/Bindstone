#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/interpreter.hpp>  // For string_symbolizer
#include <jaiscript/detail/ast.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class parser_tests : public suite {
public:
    parser_tests() : suite("Parser Tests") {}
    
    void forge_tests() override {
        test("expression_literals", [this]() {
            check_parse_succeeds("42;");
            check_parse_succeeds("3.14;");
            check_parse_succeeds("\"hello\";");
            check_parse_succeeds("'x';");
            check_parse_succeeds("true;");
            check_parse_succeeds("false;");
            check_parse_succeeds("null;");
        });

        test("expression_identifiers", [this]() {
            check_parse_succeeds("x;");
            check_parse_succeeds("foo_bar;");
            check_parse_succeeds("_private;");
        });

        test("binary_operators", [this]() {
            check_parse_succeeds("1 + 2;");
            check_parse_succeeds("x * y;");
            check_parse_succeeds("a && b || c;");
            check_parse_succeeds("x == y;");
            check_parse_succeeds("a < b;");
        });

        test("unary_operators", [this]() {
            check_parse_succeeds("-x;");
            check_parse_succeeds("!flag;");
            check_parse_succeeds("++i;");
            check_parse_succeeds("--count;");
            check_parse_succeeds("&value;");
        });

        test("assignment_operators", [this]() {
            check_parse_succeeds("x = 42;");
            check_parse_succeeds("y += 10;");
            check_parse_succeeds("z *= 2;");
        });

        test("ternary_operator", [this]() {
            check_parse_succeeds("x ? y : z;");
            check_parse_succeeds("a > b ? a : b;");
            check_parse_succeeds("flag ? 1 : 0;");
        });

        test("parenthesized_expressions", [this]() {
            check_parse_succeeds("(42);");
            check_parse_succeeds("(x + y) * z;");
            check_parse_succeeds("((a + b) * c) / d;");
        });

        test("function_calls", [this]() {
            check_parse_succeeds("foo();");
            check_parse_succeeds("bar(x);");
            check_parse_succeeds("baz(a, b, c);");
            check_parse_succeeds("nested(func(x));");
        });

        test("member_access", [this]() {
            check_parse_succeeds("obj.property;");
            check_parse_succeeds("ptr->member;");
            check_parse_succeeds("nested.obj.prop;");
        });

        test("array_access", [this]() {
            check_parse_succeeds("arr[0];");
            check_parse_succeeds("matrix[i][j];");
            // Note: map["key"] requires identifier 'map' to be defined
        });

        test("array_literals", [this]() {
            check_parse_succeeds("[1, 2, 3];");
            check_parse_succeeds("[];");
            check_parse_succeeds("[x, y + z, func()];");
        });

        test("map_literals", [this]() {
            check_parse_succeeds("{\"key\": value};");
            check_parse_succeeds("{};");
            check_parse_succeeds("{\"a\": 1, \"b\": 2};");
        });

        test("variable_declarations", [this]() {
            check_parse_succeeds("var x = 42;");
            check_parse_succeeds("let y = \"hello\";");
            check_parse_succeeds("auto w = getValue();");
        });

        test("function_declarations", [this]() {
            check_parse_succeeds("function add(auto a, auto b) -> auto { return a + b; }");
            check_parse_succeeds("auto multiply(auto x, auto y) -> auto { return x * y; }");
            check_parse_succeeds("function getValue() -> auto { return 42; }");
        });

        test("control_flow_statements", [this]() {
            check_parse_succeeds("if (x > 0) { print(x); }");
            check_parse_succeeds("if (flag) doSomething(); else doOther();");
            check_parse_succeeds("while (condition) { update(); }");
            check_parse_succeeds("for (var i = 0; i < 10; ++i) { work(i); }");
        });

        test("break_continue_statements", [this]() {
            check_parse_succeeds("while (true) { if (done) break; }");
            check_parse_succeeds("for (var i = 0; i < 10; ++i) { if (skip) continue; work(); }");
        });

        test("return_statements", [this]() {
            check_parse_succeeds("return;");
            check_parse_succeeds("return 42;");
            check_parse_succeeds("return x + y;");
        });

        test("lambda_expressions", [this]() {
            check_parse_succeeds("auto lambda = [](){ return 42; };");
            check_parse_succeeds("auto typed = [](auto x) -> auto { return x * 2; };");
            // Note: capture lists [x, y] and [&x] not yet implemented
        });

        test("complex_expressions", [this]() {
            check_parse_succeeds("result = (a + b) * c.getValue() - arr[i]->member;");
            check_parse_succeeds("bool check = x > 0 && func(y) != null;");
            check_parse_succeeds("auto final = condition ? getValue() : getDefault();");
        });

        test("error_cases", [this]() {
            check_parse_fails("42 +;");
            check_parse_fails("if (x > 0;");
            // "function () { }" is legal since the anonymous-function-expression desugar
            // Note: "var = 42;" is valid syntax - undeclared variable errors happen at runtime
            check_parse_fails("return return;");
        });

        // 'map'/'array' are hard keywords by grammar contract; using one as a name
        // used to report a generic "Expected X name" (or "Expected '<'") with no
        // cause. The diagnostic now names the keyword at the true token.
        test("type_keyword_as_name_diagnostics", [this]() {
            auto contains = [](const std::vector<std::string>& errs, const std::string& needle) {
                for (const auto& e : errs) {
                    if (e.find(needle) != std::string::npos) return true;
                }
                return false;
            };
            auto fieldErrs = collect_parse_errors("class C { var map = {}; }");
            check(contains(fieldErrs, "'map' is a type keyword"), "field: " + (fieldErrs.empty() ? "<none>" : fieldErrs[0]));
            check(contains(fieldErrs, "member name"), "field what");

            auto arrayErrs = collect_parse_errors("class C { var array = []; }");
            check(contains(arrayErrs, "'array' is a type keyword"), "array field");

            auto globalErrs = collect_parse_errors("int map = 5;");
            check(contains(globalErrs, "'map' is a type keyword"), "global: " + (globalErrs.empty() ? "<none>" : globalErrs[0]));
            check(contains(globalErrs, "variable name"), "global what");

            auto paramErrs = collect_parse_errors("function f(var map) { return 0; }");
            check(contains(paramErrs, "'map' is a type keyword"), "param");

            auto destructureErrs = collect_parse_errors("auto [x, map] = p;");
            check(contains(destructureErrs, "'map' is a type keyword"), "destructuring");

            auto memberErrs = collect_parse_errors("obj.map;");
            check(contains(memberErrs, "'map' is a type keyword"), "member access");

            // Statement starting with the keyword commits to a type parse - the '<'
            // message now explains the collision instead of a bare "Expected '<'"
            auto stmtErrs = collect_parse_errors("function g() { map = m; }");
            check(contains(stmtErrs, "built-in map type keyword"), "statement: " + (stmtErrs.empty() ? "<none>" : stmtErrs[0]));
        });

        // A failed member/ctor body used to recover at the first ';' INSIDE the body:
        // the member loop resumed mid-body, took the method's '}' for the class's, and
        // every later member leaked to top level with a spurious "Expected expression"
        // at the class's real closing brace. Brace-aware recovery keeps errors at the
        // true sites and the class intact.
        test("class_recovery_stays_in_class", [this]() {
            const std::string code =
                "class Level {\n"              // 1
                "\tstring map = \"\";\n"       // 2  <- true site #1 (field name)
                "\tint kills = 0;\n"           // 3
                "\tLevel(string m) {\n"        // 4
                "\t\tmap = m;\n"               // 5  <- true site #2 (use in ctor body)
                "\t}\n"                        // 6
                "\tvoid report() {\n"          // 7
                "\t\tkills = kills + 0;\n"     // 8
                "\t}\n"                        // 9
                "\tint total() {\n"            // 10
                "\t\treturn kills;\n"          // 11
                "\t}\n"                        // 12
                "}\n";                         // 13 <- no error may land here
            auto errs = collect_parse_errors(code);
            check_eq((size_t)2, errs.size());
            check(errs.size() == 2 && errs[0].find("2:") != std::string::npos &&
                  errs[0].find("'map' is a type keyword") != std::string::npos,
                  "first error at the field: " + (errs.empty() ? "<none>" : errs[0]));
            check(errs.size() == 2 && errs[1].find("5:") != std::string::npos &&
                  errs[1].find("built-in map type keyword") != std::string::npos,
                  "second error at the ctor-body use: " + (errs.size() > 1 ? errs[1] : "<none>"));
            for (const auto& e : errs) {
                check(e.find("Expected expression") == std::string::npos, "no downstream cascade: " + e);
                check(e.find("13:") == std::string::npos, "no error at the class close: " + e);
            }
        });
    }

private:
    std::vector<std::string> collect_parse_errors(const std::string& code) {
        auto eng = make_engine();
        auto* symbolizer = eng->get_symbolizer();
        lexer lex(code, symbolizer, "test.jai");
        auto tokens = lex.tokenize();
        std::unordered_set<std::string> empty_types;
        parser p(tokens, symbolizer, eng.get(), empty_types, "test.jai");
        try {
            auto ast = p.parse();
        } catch (const parse_error&) {
            // errors_ still carries the reports
        }
        return p.get_errors();
    }

    void check_parse_succeeds(const std::string& code) {
        auto eng = make_engine();
        auto* symbolizer = eng->get_symbolizer();
        lexer lex(code, symbolizer, "test.jai");
        auto tokens = lex.tokenize();
        std::unordered_set<std::string> empty_types;
        parser p(tokens, symbolizer, eng.get(), empty_types, "test.jai");
        auto ast = p.parse();
        check(!p.has_errors(), "Expected parsing to succeed for: " + code);
    }

    void check_parse_fails(const std::string& code) {
        auto eng = make_engine();
        auto* symbolizer = eng->get_symbolizer();
        lexer lex(code, symbolizer, "test.jai");
        auto tokens = lex.tokenize();
        std::unordered_set<std::string> empty_types;
        parser p(tokens, symbolizer, eng.get(), empty_types, "test.jai");
        try {
            auto ast = p.parse();
            check(p.has_errors(), "Expected parsing to fail for: " + code);
        } catch (const parse_error&) {
            // Expected - parsing failed as intended
            check(true);
        }
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::parser_tests)