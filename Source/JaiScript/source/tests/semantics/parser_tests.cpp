#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/interpreter.hpp>  // For string_symbolizer
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/ast_serializer.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class parser_tests : public suite {
public:
    parser_tests() : suite("Parser Tests") {}
    
    void forge_tests() override {
        test("function_spelling_is_kept_for_the_checker", [this]() {
            // `function f = expr;` parses as an auto slot (the runtime slot is dynamic, like a
            // function-typed parameter) that remembers its spelling at top level, in a body and
            // as a field; the flag rides the jaibite so a re-check of a loaded bite sees it;
            // `auto` and `var` declarations do not carry it
            auto eng = make_engine();
            auto* symbolizer = eng->get_symbolizer();
            const std::string code =
                "function f = 42; auto g = 42; var h = 42; function later;"
                " void body() { function k = null; int n = 0; }"
                " class C { function cb = null; int n = 0; static function sf = null; static int sn = 0; }"
                " namespace N { function nf = 42; int nn = 0; }";
            lexer lex(code, symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            std::unordered_set<std::string> empty_types;
            parser p(tokens, symbolizer, eng.get(), empty_types, "test.jai");
            auto parsed = p.parse();
            check(!p.has_errors() && parsed.has_value(), "parses");
            auto decl_flag = [](const declaration_ptr& d) {
                return d && d->get_type() == node_type::variable_decl && static_cast<const variable_decl*>(d.get())->function_spelling;
            };
            auto probe = [&](const std::vector<declaration_ptr>& decls, const char* label) {
                check_eq((size_t)7, decls.size(), std::string(label) + " declaration count");
                check_true(decl_flag(decls[0]), std::string(label) + " function f");
                check_false(decl_flag(decls[1]), std::string(label) + " auto g");
                check_false(decl_flag(decls[2]), std::string(label) + " var h");
                check_true(decl_flag(decls[3]), std::string(label) + " function later;");
                check_true(!static_cast<const variable_decl*>(decls[0].get())->type, std::string(label) + " the slot stays auto");
                auto* fn = static_cast<const function_decl*>(decls[4].get());
                check_true(fn->body && fn->body->declarations.size() == 2 && decl_flag(fn->body->declarations[0]) &&
                           !decl_flag(fn->body->declarations[1]), std::string(label) + " body local");
                auto* cls = static_cast<const class_decl*>(decls[5].get());
                check_true(cls->members.size() == 4 && decl_flag(cls->members[0].declaration) &&
                           !decl_flag(cls->members[1].declaration), std::string(label) + " field");
                check_true(cls->members.size() == 4 && decl_flag(cls->members[2].declaration) &&
                           !decl_flag(cls->members[3].declaration), std::string(label) + " static field after the modifier");
                auto* ns = static_cast<const namespace_decl*>(decls[6].get());
                check_true(ns->declarations.size() == 2 && decl_flag(ns->declarations[0]) &&
                           !decl_flag(ns->declarations[1]), std::string(label) + " namespace variable");
            };
            probe(parsed.value(), "parsed");
            const auto bytes = detail::serialize_jaibite(parsed.value(), *symbolizer, eng->registration_fingerprint());
            uint64_t fingerprint = 0;
            auto loaded = detail::deserialize_jaibite(bytes.data(), bytes.size(), eng.get(), fingerprint);
            probe(loaded, "loaded");
        });

        test("class_typed_reference_declaration_parses_as_a_declaration", [this]() {
            // `Base& b = derived;` and `Base& b;` at statement level are reference
            // declarations (they read as bitwise-and plus assignment before); `Box& get(`
            // stays the reference-return function; a bitwise-and statement keeps its meaning
            check_parse_succeeds("class A { int x = 1; } class B : A {} B b = B(); A& a = b; a.x = 5;");
            check_parse_succeeds("class A {} A& r;");
            check_parse_succeeds("class Box { int v = 0; } Box b = Box(); Box& get() { return b; }");
            auto eng = make_engine();
            auto* symbolizer = eng->get_symbolizer();
            lexer lex("class A { int x = 1; } class B : A {} B b = B(); A& a = b;", symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            std::unordered_set<std::string> empty_types;
            parser p(tokens, symbolizer, eng.get(), empty_types, "test.jai");
            auto parsed = p.parse();
            check(!p.has_errors() && parsed.has_value(), "parses");
            const auto& decls = parsed.value();
            check_eq((size_t)4, decls.size());
            check_true(decls[3]->get_type() == node_type::variable_decl, "a declaration, not an expression");
            auto* ref = static_cast<const variable_decl*>(decls[3].get());
            check_true(ref->type && ref->type->base_type == script_value_type::jai_reference_type, "a reference type");
            check_eq(std::string("A&"), ref->type->type_name);
        });

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
            check_parse_fails("&value;");   // no address-of: the parser names auto& and T& parameters instead
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

        // 'map'/'array' are hard keywords by grammar contract outside member names; using one as a name
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

            // after a dot, and as a method's declared name, a keyword is a member name
            check(collect_parse_errors("obj.map; obj.string(); p->array;").empty(), "member access");
            check(collect_parse_errors("class C { int map(int x) { return x; } static bool default() { return true; } }").empty(), "method names");

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