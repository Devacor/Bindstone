#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

#include <string>

using namespace jai::foundry;

namespace jai::foundry::tests {

// C++ object lifetimes (the contract): a block's locals die at its closing brace in reverse declaration order, a loop
// body's at the end of each iteration, parameters at the end of the callee after its locals, a prvalue argument is the
// parameter itself, a member dies after its own class's destructor body and before its base's, and program-scope
// variables die at engine teardown in reverse construction order. Every expected log below is the C++ program's.
class destructor_timing_tests : public suite {
public:
    destructor_timing_tests() : suite("Destructor Timing") {}

    static constexpr const char* k_prelude = R"(
        string LOG = "";
        class Token { string name = ""; Token() {} Token(string n) { name = n; } ~Token() { LOG = LOG + name; } }
    )";

    // The program's last expression is the log; both the default engine and one with proofs and passes off run it
    void expect_log(const std::string& program, const std::string& expected) {
        for (int variant = 0; variant < 2; ++variant) {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            if (variant == 1) {
                eng->type_proofs(false);
                eng->ir_passes(false);
            }
            std::string got;
            try {
                got = eng->execute(std::string(k_prelude) + program).as_string();
            } catch (const std::exception& e) {
                got = std::string("threw: ") + e.what();
            }
            check_eq(expected, got, variant == 0 ? "default engine" : "proofs and passes off");
        }
    }

    void forge_tests() override {
        test("block_locals_die_at_the_closing_brace_in_reverse", [this]() {
            expect_log(R"(
                function f() -> void { Token a = Token("a"); { Token i = Token("i"); Token j = Token("j"); } LOG = LOG + "|"; Token b = Token("b"); }
                f(); LOG;
            )", "ji|ba");
        });

        test("loop_body_locals_die_each_iteration", [this]() {
            expect_log(R"(
                function f() -> void { for (int k = 0; k < 2; k++) { Token t = Token(to_string(k)); LOG = LOG + "."; } LOG = LOG + "|"; }
                f(); LOG;
            )", ".0.1|");
            expect_log(R"(
                function f() -> void { int k = 0; while (k < 2) { Token t = Token(to_string(k)); k++; LOG = LOG + "."; } LOG = LOG + "|"; }
                f(); LOG;
            )", ".0.1|");
        });

        test("break_and_continue_destroy_the_scopes_they_leave", [this]() {
            expect_log(R"(
                function f() -> void { for (int k = 0; k < 3; k++) { Token t = Token(to_string(k)); { Token u = Token("u"); if (k == 1) { break; } } LOG = LOG + "."; } LOG = LOG + "|"; }
                f(); LOG;
            )", "u.0u1|");
            expect_log(R"(
                function f() -> void { for (int k = 0; k < 2; k++) { Token t = Token(to_string(k)); if (k == 0) { continue; } LOG = LOG + "."; } LOG = LOG + "|"; }
                f(); LOG;
            )", "0.1|");
        });

        test("return_destroys_every_open_scope_innermost_first", [this]() {
            expect_log(R"(
                function f() -> void { Token a = Token("a"); { Token b = Token("b"); if (true) { Token c = Token("c"); return; } } }
                f(); LOG;
            )", "cba");
        });

        test("throw_destroys_the_scopes_it_leaves", [this]() {
            expect_log(R"(
                function f() -> void { try { Token a = Token("a"); { Token b = Token("b"); throw 1; } } catch (e) { LOG = LOG + "!"; } LOG = LOG + "|"; }
                f(); LOG;
            )", "ba!|");
            expect_log(R"(
                function g() -> void { Token a = Token("a"); { Token b = Token("b"); throw 1; } }
                function f() -> void { try { g(); } catch (e) { LOG = LOG + "!"; } }
                f(); LOG;
            )", "ba!");
        });

        test("prvalue_arguments_are_the_parameters", [this]() {
            expect_log(R"(
                function args(Token p, Token q) -> void { LOG = LOG + "body"; }
                args(Token("x"), Token("y")); LOG;
            )", "bodyyx");
        });

        test("parameters_die_at_the_callee_end_after_its_locals", [this]() {
            expect_log(R"(
                function f(Token p) -> void { Token l = Token("l"); LOG = LOG + "body"; }
                f(Token("p")); LOG;
            )", "bodylp");
            expect_log(R"(
                function one(Token p) -> void { LOG = LOG + "body"; }
                function f() -> void { Token t = Token("t"); one(t); LOG = LOG + "|"; }
                f(); LOG;
            )", "bodyt|t");
        });

        test("a_returned_local_moves", [this]() {
            expect_log(R"(
                function make() -> Token { Token t = Token("m"); return t; }
                function f() -> void { Token x = make(); LOG = LOG + "|"; }
                f(); LOG;
            )", "|m");
        });

        test("by_value_range_for_variable_dies_each_iteration", [this]() {
            expect_log(R"(
                function f(array<Token>& ts) -> void { for (Token t : ts) { LOG = LOG + "<" + t.name + ">"; } LOG = LOG + "|"; }
                array<Token> TS; TS.push(Token("p")); TS.push(Token("q"));
                LOG = ""; f(TS); LOG;
            )", "<p>p<q>q|");
            expect_log(R"(
                function f(array<Token>& ts) -> void { for (auto& t : ts) { LOG = LOG + "<" + t.name + ">"; } LOG = LOG + "|"; }
                array<Token> TS; TS.push(Token("p")); TS.push(Token("q"));
                LOG = ""; f(TS); LOG;
            )", "<p><q>|");
        });

        test("for_initializer_variables_die_after_the_loop", [this]() {
            expect_log(R"(
                function f() -> void { for (Token t = Token("i"); LOG.size() < 3; LOG = LOG + ".") { } LOG = LOG + "|"; }
                f(); LOG;
            )", "...i|");
        });

        test("members_die_after_their_class_body_and_before_the_base", [this]() {
            expect_log(R"(
                class Base { Token bm = Token("bm"); Base() {} ~Base() { LOG = LOG + "~B"; } }
                class Derived : Base { Token d1 = Token("d1"); Token d2 = Token("d2"); Derived() {} ~Derived() { LOG = LOG + "~D"; } }
                function f() -> void { Derived d = Derived(); LOG = ""; }
                f(); LOG;
            )", "~Dd2d1~Bbm");
        });

        test("a_base_destructor_runs_for_a_class_that_declares_none", [this]() {
            expect_log(R"(
                class Base { Token bm = Token("bm"); Base() {} ~Base() { LOG = LOG + "~B"; } }
                class Plain : Base { Token pm = Token("pm"); Plain() {} }
                function f() -> void { Plain p = Plain(); LOG = ""; }
                f(); LOG;
            )", "pm~Bbm");
        });

        test("temporaries_in_conditions_die_before_the_body", [this]() {
            expect_log(R"(
                function f() -> void { if (Token("c").name == "c") { LOG = LOG + "body"; } }
                f(); LOG;
            )", "cbody");
            expect_log(R"(
                function f() -> void { int k = 0; while (Token(to_string(k)).name != "2") { LOG = LOG + "."; k++; } LOG = LOG + "|"; }
                f(); LOG;
            )", "0.1.2|");
        });

        test("program_scope_variables_die_at_teardown_in_reverse", [this]() {
            for (int variant = 0; variant < 2; ++variant) {
                std::string log;
                {
                    auto eng = make_engine();
                    if (variant == 1) { eng->type_proofs(false); eng->ir_passes(false); }
                    eng->add_function("note", [&log](std::string s) { log += s; });
                    eng->execute(R"(
                        class T { string n = ""; T(string s) { n = s; } ~T() { note(n); } }
                        T G1 = T("1");
                        T G2 = T("2");
                        array<T> GA;
                        GA.push(T("a"));
                    )");
                    log.clear();
                }
                check_eq(std::string("a21"), log, "the last constructed dies first");
            }
        });

        test("a_destructor_error_at_teardown_is_reported", [this]() {
            std::string reported;
            bool threw = false;
            try {
                auto eng = make_engine();
                eng->set_script_error_handler([&reported](const std::string& message) { reported += message; });
                eng->execute(R"(
                    class Bad { Bad() {} ~Bad() { throw "teardown"; } }
                    Bad B = Bad();
                )");
            } catch (...) {
                threw = true;
            }
            check_false(threw, "teardown does not propagate");
            check_true(reported.find("teardown") != std::string::npos, "reported: " + reported);
        });

        // A destructor that runs while an expression releases a temporary leaves nothing of its own body on the value stack
        test("a_destructor_inside_an_expression_keeps_the_expression_value", [this]() {
            expect_log(R"(
                function a2() -> bool { string s = Token("a").name; return s == "a"; }
                function a5() -> int { return Token("abc").name.size(); }
                bool ok = a2(); int n = a5();
                LOG = LOG + "|" + to_string(ok) + to_string(n); LOG;
            )", "aabc|true3");
        });

        test("break_out_of_a_block_in_a_case_destroys_it", [this]() {
            expect_log(R"(
                function f(int k) -> void { switch (k) { case 1: { Token t = Token("c"); LOG = LOG + "."; break; } default: LOG = LOG + "d"; } LOG = LOG + "|"; }
                f(1); f(2); LOG;
            )", ".c|d|");
        });

        test("handle_and_var_locals_die_at_their_block_end", [this]() {
            expect_log(R"(
                function f() -> void { { shared_ptr<Token> h = new Token("h"); LOG = LOG + "."; } LOG = LOG + "|"; }
                f(); LOG;
            )", ".h|");
            expect_log(R"(
                function f() -> void { { var v = Token("v"); LOG = LOG + "."; } LOG = LOG + "|"; }
                f(); LOG;
            )", ".v|");
            expect_log(R"(
                function f() -> void { shared_ptr<Token> keep = null; { shared_ptr<Token> h = new Token("h"); keep = h; } LOG = LOG + "|"; }
                f(); LOG;
            )", "|h");
        });

        test("an_abandoned_coroutine_destroys_its_locals", [this]() {
            expect_log(R"(
                coroutine gen() -> int { Token a = Token("a"); yield 1; yield 2; }
                function f() -> void { var g = gen(); g.resume(); LOG = LOG + "|"; g = null; LOG = LOG + "|"; }
                f(); LOG;
            )", "|a|");
        });

        test("a_coroutine_keeps_its_locals_across_yields", [this]() {
            expect_log(R"(
                coroutine gen() -> int { Token a = Token("a"); yield 1; LOG = LOG + "r"; yield 2; }
                function f() -> void { var g = gen(); g.resume(); LOG = LOG + "|"; g.resume(); LOG = LOG + "|"; g.resume(); LOG = LOG + "|"; }
                f(); LOG;
            )", "|r|a|");
        });
    }
};

}

using destructor_timing_tests = jai::foundry::tests::destructor_timing_tests;
FOUNDRY_REGISTER(destructor_timing_tests)
