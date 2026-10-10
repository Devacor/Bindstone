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

        // Assignment into a slot holding a class instance by value is C++'s copy assignment: the
        // slot keeps its object, the declared class's members take the source's, then a temporary source dies
        test("assignment_copy_assigns_and_the_temporary_dies", [this]() {
            expect_log(R"(
                function f() -> void { Token x = Token("a"); x = Token("b"); LOG = LOG + "|"; }
                f(); LOG;
            )", "b|b");
            expect_log(R"(
                function f() -> void { Token x = Token("a"); Token y = Token("c"); x = y; LOG = LOG + "|"; }
                f(); LOG;
            )", "|cc");
            expect_log(R"(
                function f() -> void { Token x = Token("a"); Token y = (x = Token("b")); LOG = LOG + to_string(same_object(x, y)) + "|"; }
                f(); LOG;
            )", "bfalse|bb");
        });

        test("assignment_keeps_the_object_references_and_handles_name", [this]() {
            expect_log(R"(
                function f() -> string { Token x = Token("a"); Token& r = x; shared_ptr<Token> h = x; x = Token("b"); return r.name + h.name + to_string(same_object(h, x)); }
                string out = f(); out + ":" + LOG;
            )", "bbtrue:bb");
            expect_log(R"(
                function f() -> string { Token x = Token("a"); Token& r = x; r = Token("r"); LOG = LOG + "|"; return x.name; }
                string out = f(); out + ":" + LOG;
            )", "r:r|r");
        });

        // No slicing: a source of another class the slot admits re-classes the slot's object in place:
        // the levels it loses die (destructor body, then their members), it takes the source's class, every member assigns
        // from the source; references, handles and same_object keep seeing the one object
        test("assignment_from_a_subclass_source_reclasses_the_object", [this]() {
            expect_log(std::string(k_family) + R"(
                function f() -> string { Base x = Base(); shared_ptr<Base> h = x; D2 s = D2(); s.a = 5; s.d2 = 6; x = s; LOG = LOG + "|";
                    return to_string(x.a) + type_of(h) + to_string(same_object(h, x)) + to_string(cast<D2&>(x).d2); }
                string out = f(); out + ":" + LOG;
            )", "5D2true6:|2B2B");
        });

        test("a_sibling_source_destroys_only_the_levels_the_object_loses", [this]() {
            expect_log(std::string(k_family) + R"(
                function f() -> string { Base x = D1(); shared_ptr<Base> h = x; D2 s = D2(); s.a = 7; LOG = LOG + "|"; x = s; LOG = LOG + "|";
                    return to_string(x.a) + to_string(cast<D2&>(h).d2) + to_string(same_object(h, x)); }
                string out = f(); out + ":" + LOG;
            )", "73true:|1t|2B2B");
        });

        test("a_base_source_drops_the_derived_level_and_keeps_the_base_members", [this]() {
            expect_log(std::string(k_family) + R"(
                function f() -> string { Base x = D1(); shared_ptr<Base> h = x; Base b = Base(); b.a = 9; LOG = LOG + "|"; x = b; LOG = LOG + "|";
                    string r = to_string(x.a) + type_of(h) + to_string(same_object(h, x));
                    try { int k = cast<D1&>(x).d1; r = r + "no"; } catch (e) { r = r + "!"; }
                    return r; }
                string out = f(); out + ":" + LOG;
            )", "9Basetrue!:|1t|BB");
        });

        test("a_reference_to_a_dropped_member_raises", [this]() {
            expect_log(std::string(k_family) + R"(
                function f() -> string { Base x = D1(); D1& dx = cast<D1&>(x); int& r = dx.d1; Token& t = dx.t1; x = Base(); string out = "";
                    try { int k = r; out = out + "read"; } catch (e) { out = out + e; }
                    out = out + "/";
                    try { r = 4; out = out + "write"; } catch (e) { out = out + e; }
                    out = out + "/";
                    try { string n = t.name; out = out + "member"; } catch (e) { out = out + e; }
                    LOG = "";
                    return out; }
                f();
            )", "Reference to a removed field/Reference to a removed field/Reference to a removed field");
            expect_log(std::string(k_family) + R"(
                function f() -> string { Base x = D1(); D1& dx = cast<D1&>(x); int& r = dx.d1; string out = "";
                    for (int i = 0; i < 3; i++) { x = D2(); D1 back = D1(); back.d1 = 10 + i; x = back; out = out + to_string(r); }
                    LOG = "";
                    return out; }
                f();
            )", "101112");
        });

        test("an_unrelated_source_still_raises", [this]() {
            expect_log(std::string(k_family) + R"(
                [[warn_mode]]
                class Other { int a = 1; Other() {} }
                function f() -> string { Base x = Base(); shared_ptr<Base> h = x; var o = Other(); string r = "";
                    try { x = o; r = "no"; } catch (e) { r = e; }
                    return r + "|" + type_of(h) + to_string(same_object(h, x)); }
                string out = f(); LOG = ""; out;
            )", "Cannot assign Other to 'x' (declared Base)|Basetrue");
        });

        test("every_by_value_slot_reclasses_in_place", [this]() {
            const std::string family = std::string(k_family);
            expect_log(family + R"(
                function f() -> string { array<Base> a; a.push(Base()); shared_ptr<Base> h = a[0]; a[0] = D2(); return type_of(a[0]) + to_string(same_object(h, a[0])) + to_string(cast<D2&>(h).d2); }
                string out = f(); LOG = ""; out;
            )", "D2true3");
            expect_log(family + R"(
                class Holder { Base b; }
                function f() -> string { Holder o; shared_ptr<Base> h = o.b; o.b = D2(); return type_of(h) + to_string(same_object(h, o.b)) + to_string(cast<D2&>(o.b).d2); }
                string out = f(); LOG = ""; out;
            )", "D2true3");
            expect_log(family + R"(
                class S { static Base st; }
                function f() -> string { shared_ptr<Base> h = S::st; S::st = D2(); return type_of(h) + to_string(same_object(h, S::st)) + to_string(cast<D2&>(S::st).d2); }
                string out = f(); LOG = ""; out;
            )", "D2true3");
            expect_log(family + R"(
                function f() -> string { map<string, Base> m; m["k"] = Base(); shared_ptr<Base> h = m["k"]; m["k"] = D2(); return type_of(h) + to_string(same_object(h, m["k"])) + to_string(cast<D2&>(m["k"]).d2); }
                string out = f(); LOG = ""; out;
            )", "D2true3");
            expect_log(family + R"(
                function put(Base& p) -> void { p = D2(); }
                function f() -> string { Base x = Base(); shared_ptr<Base> h = x; Base& r = x; r = D1(); string first = type_of(h); put(x);
                    return first + type_of(h) + to_string(same_object(h, x)); }
                string out = f(); LOG = ""; out;
            )", "D1D2true");
            expect_log(family + R"(
                class Acc { int n = 0; Acc() {} Acc(int v) { n = v; } function operator+(Acc o) -> Acc { return Sum(n + o.n); } }
                class Sum : Acc { int parts = 2; Sum(int v) { n = v; } }
                function f() -> string { Acc x = Acc(1); shared_ptr<Acc> h = x; Acc two = Acc(2); x += two; return type_of(h) + to_string(h.n) + to_string(same_object(h, x)) + to_string(cast<Sum&>(x).parts); }
                string out = f(); LOG = ""; out;
            )", "Sum3true2");
        });

        test("a_reference_declared_narrower_than_its_storage_keeps_its_class", [this]() {
            expect_log(std::string(k_family) + R"(
                [[warn_mode]]
                function f() -> string { Base x = D1(); D1& r = cast<D1&>(x); string out = "";
                    try { var s = D2(); r = s; out = "no"; } catch (e) { out = e; }
                    return out + "|" + type_of(x); }
                string out = f(); LOG = ""; out;
            )", "Cannot assign D2 to 'r' (declared D1&)|D1");
        });

        test("a_reclassed_object_dies_as_its_new_class", [this]() {
            expect_log(R"(
                class P { int a = 0; P() {} }
                class Q : P { Q() {} ~Q() { LOG = LOG + "Q"; } }
                function f() -> void { P x = P(); x = Q(); LOG = LOG + "|"; }
                f(); LOG;
            )", "Q|Q");
            expect_log(R"(
                class P { int a = 0; P() {} ~P() { LOG = LOG + "P"; } }
                class Q : P { Q() {} ~Q() { LOG = LOG + "Q"; } }
                function f() -> void { P x = Q(); x = P(); LOG = LOG + "|"; }
                f(); LOG;
            )", "QP|P");
        });

        test("field_element_map_and_static_stores_copy_assign", [this]() {
            expect_log(R"(
                class Holder { Token t; }
                function f() -> string { Holder o; shared_ptr<Token> h = o.t; o.t = Token("n"); LOG = LOG + "|"; return h.name + to_string(same_object(h, o.t)); }
                string out = f(); out + ":" + LOG;
            )", "ntrue:n|n");
            expect_log(R"(
                function f() -> string { array<Token> a; Token p = Token("p"); a.push(p); shared_ptr<Token> h = a[0]; a[0] = Token("q"); LOG = LOG + "|"; return h.name + to_string(same_object(h, a[0])); }
                string out = f(); out + ":" + LOG;
            )", "qtrue:q|pq");
            expect_log(R"(
                function f() -> string { map<string, Token> m; m["k"] = Token("m"); LOG = LOG + "/"; shared_ptr<Token> h = m["k"]; m["k"] = Token("n"); LOG = LOG + "|"; return h.name + to_string(same_object(h, m["k"])); }
                string out = f(); out + ":" + LOG;
            )", "ntrue:/n|n");
            expect_log(R"(
                class S { static Token st; }
                function f() -> string { shared_ptr<Token> h = S::st; S::st = Token("s"); LOG = LOG + "|"; return h.name + to_string(same_object(h, S::st)); }
                string out = f(); out + ":" + LOG;
            )", "strue:s|");
        });

        test("a_fresh_element_takes_the_temporary_and_a_pushed_one_copies", [this]() {
            expect_log(R"(
                function f() -> void { map<string, Token> m; m["k"] = Token("m"); LOG = LOG + "|"; }
                f(); LOG;
            )", "|m");
            expect_log(R"(
                function f() -> void { array<Token> a; a.push(Token("p")); LOG = LOG + "/"; a[0] = Token("q"); LOG = LOG + "|"; }
                f(); LOG;
            )", "p/q|q");
        });

        test("a_compound_assignment_copy_assigns_the_operator_result", [this]() {
            expect_log(R"(
                class Acc { int n = 0; Acc(int v) { n = v; } ~Acc() { LOG = LOG + to_string(n); } function operator+(Acc o) -> Acc { return Acc(n + o.n); } }
                function f() -> string { Acc x = Acc(1); shared_ptr<Acc> h = x; Acc two = Acc(2); x += two; LOG = LOG + "|"; return to_string(h.n) + to_string(same_object(h, x)); }
                string out = f(); out + ":" + LOG;
            )", "3true:23|23");
        });

        test("a_var_slot_replaces_its_value", [this]() {
            expect_log(R"(
                function f() -> string { var v = Token("a"); shared_ptr<Token> h = v; v = Token("b"); LOG = LOG + "|"; return h.name + v.name; }
                string out = f(); out + ":" + LOG;
            )", "ab:|ab");
        });

        test("a_hot_reloaded_instance_copy_assigns_with_the_new_layout", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(std::string(k_prelude) + R"(
                Token held = Token("x");
                shared_ptr<Token> alias = held;
            )");
            const std::string got = eng->execute(R"(
                class Token { string name = ""; int hp = 7; Token() {} Token(string n) { name = n; } ~Token() { LOG = LOG + name; } }
                LOG = "";
                Token next = Token("y");
                next.hp = 9;
                held = next;
                alias.name + to_string(alias.hp) + to_string(same_object(alias, held)) + ":" + LOG;
            )").as_string();
            check_eq(std::string("y9true:"), got);
        });

        test("a_reclassed_instance_migrates_with_its_new_class", [this]() {
            auto eng = make_engine();
            jai::stdlib::register_all(eng);
            eng->execute(std::string(k_prelude) + k_family + R"(
                Base held = Base();
                shared_ptr<Base> alias = held;
                held = D2();
            )");
            const std::string got = eng->execute(R"(
                class D2 : Base { int d2 = 3; int extra = 5; D2() {} ~D2() { LOG = LOG + "2"; } }
                LOG = "";
                type_of(alias) + to_string(cast<D2&>(held).extra) + to_string(same_object(alias, held)) + ":" + LOG;
            )").as_string();
            check_eq(std::string("D25true:"), got);
        });

        test("a_literal_element_copied_before_a_later_call_is_copied_once", [this]() {
            expect_log(R"(
                class Box { Token t; }
                function one() -> int { return 1; }
                function f() -> void {
                    var q = Box(); q.t = Token("q");
                    LOG = LOG + "1";
                    var arr = [q, Token("a")];
                    LOG = LOG + "2";
                    var m = {"k": q, "j": one()};
                    LOG = LOG + "3";
                    arr = [one(), q, one()];
                    LOG = LOG + "4";
                }
                f(); LOG;
            )", "q123qa4qqq");
        });

        test("push_takes_a_temporary_only_where_no_destructor_sees_it", [this]() {
            expect_log(R"(
                class Plain { int v = 0; array<int> xs; }
                class Holder { Token t; Holder(string n) { t = Token(n); } }
                function make(int v) -> Plain { Plain p; p.v = v; return p; }
                function f() -> string {
                    array<Plain> a; Plain p; p.v = 3; a.push(p); p.v = 4; a.push(make(7)); a[1].xs.push(5);
                    var b = []; b.push(make(8)); b[0].v = 9;
                    array<Holder> h; h.push(Holder("h"));
                    LOG = LOG + "|";
                    return to_string(a[0].v) + to_string(p.v) + to_string(a[1].v) + to_string(a[1].xs.size()) + to_string(b[0].v);
                }
                LOG = LOG + f() + LOG; LOG;
            )", "34719hh|h");
        });

        test("long_chains_release_without_native_recursion", [this]() {
            for (const char* drop : {"function f(int n) -> int { shared_ptr<Node> head = build(n); return head.x; } f(N);",
                                     "shared_ptr<Node> head = build(N); head = null;",
                                     "var v = build(N); v = 0;",
                                     "shared_ptr<Holder> t = null; for (int i = 0; i < N; i++) { shared_ptr<Holder> h = new Holder(); h.items = [t, {\"k\": i}]; t = h; } t = null;",
                                     "shared_ptr<Holder> t = null; for (int i = 0; i < N; i++) { shared_ptr<Holder> h = new Holder(); h.items = {\"n\": t}; t = h; } t = null;"}) {
                for (int n : {10000, 100000}) {
                    auto eng = make_engine();
                    jai::stdlib::register_all(eng);
                    eng->execution_budget(0);
                    std::string got;
                    try {
                        got = eng->execute("int N = " + std::to_string(n) + R"(;
                            class Node { int x = 0; shared_ptr<Node> next = null; }
                            class Holder { var items = null; }
                            function build(int n) -> shared_ptr<Node> {
                                shared_ptr<Node> head = null;
                                for (int i = 0; i < n; i++) { shared_ptr<Node> c = new Node(); c.x = i; c.next = head; head = c; }
                                return head;
                            }
                        )" + std::string(drop) + " \"ok\";").as_string();
                    } catch (const std::exception& e) {
                        got = e.what();
                    }
                    check_eq(std::string("ok"), got, std::string(drop) + " n=" + std::to_string(n));
                }
            }
        });

        test("nested_containers_release_without_native_recursion", [this]() {
            auto eng = make_engine();
            jai::script_value nested = jai::script_value::make_array(nullptr, eng.get());
            for (int i = 0; i < 1000000; ++i) {
                jai::script_value outer = jai::script_value::make_array(nullptr, eng.get());
                outer.get_array_storage()->values().push_back(jai::script_value(static_cast<jai::script_int>(i), eng.get()));
                outer.get_array_storage()->values().push_back(std::move(nested));
                nested = std::move(outer);
            }
            nested = jai::script_value(std::monostate{}, eng.get());
            jai::script_value maps = jai::script_value::make_map(nullptr, nullptr, eng.get());
            for (int i = 0; i < 1000000; ++i) {
                jai::script_value outer = jai::script_value::make_map(nullptr, nullptr, eng.get());
                outer.get_map_storage()->insert_or_assign(jai::script_value("next", eng.get()), std::move(maps));
                maps = std::move(outer);
            }
            maps = jai::script_value(std::monostate{}, eng.get());
            check(true);
        });

        test("a_long_chain_at_teardown_releases_without_native_recursion", [this]() {
            for (const char* program : {"shared_ptr<Node> head = build(100000);",
                                        "class Holder { static shared_ptr<Node> head = null; } Holder::head = build(100000);",
                                        "class Holder { static var bag = null; } Holder::bag = [build(50000), {\"k\": build(50000)}];",
                                        "class Tail { ~Tail() {} } shared_ptr<Node> head = build(100000); Tail t;"}) {
                auto eng = make_engine();
                eng->execution_budget(0);
                eng->execute(std::string(R"(
                    class Node { int x = 0; shared_ptr<Node> next = null; }
                    function build(int n) -> shared_ptr<Node> {
                        shared_ptr<Node> head = null;
                        for (int i = 0; i < n; i++) { shared_ptr<Node> c = new Node(); c.x = i; c.next = head; head = c; }
                        return head;
                    }
                )") + program);
                eng.reset();
                check(true);
            }
        });

        // The release stack keeps C++ order exactly: every limit, the stack always (0) to never in practice, logs alike
        test("the_release_stack_keeps_destruction_order", [this]() {
            const std::string shapes = R"(
                class Tag { string n = ""; Tag() {} Tag(string s) { n = s; } ~Tag() { LOG = LOG + n; } }
                class Base { string id = ""; ~Base() { LOG = LOG + "b" + id; } }
                class Link : Base { Tag first; shared_ptr<Link> next = null; Tag last; ~Link() { LOG = LOG + "<" + id; } }
                class Plain { Tag t; shared_ptr<Link> link = null; var bag = null; }
                class Dropper { shared_ptr<Dropper> other = null; string id = ""; ~Dropper() { LOG = LOG + "d" + id; other = null; LOG = LOG + "/" + id; } }
                function chain(int n) -> shared_ptr<Link> {
                    shared_ptr<Link> head = null;
                    for (int i = 0; i < n; i++) {
                        shared_ptr<Link> c = new Link();
                        c.id = to_string(i); c.first = Tag("f" + to_string(i)); c.last = Tag("l" + to_string(i)); c.next = head; head = c;
                    }
                    return head;
                }
                function run() -> void {
                    { shared_ptr<Link> h = chain(12); }
                    LOG = LOG + "|";
                    { var p = Plain(); p.t = Tag("pt"); p.link = chain(3); p.bag = [Tag("x"), chain(2), {"k": chain(2), "j": Tag("y")}, [[Tag("z")]]]; }
                    LOG = LOG + "|";
                    { var arr = []; for (int i = 0; i < 6; i++) { var q = Plain(); q.t = Tag("q" + to_string(i)); q.bag = arr; arr = [q, Tag("a" + to_string(i))]; } }
                    LOG = LOG + "|";
                    { shared_ptr<Dropper> d = null; for (int i = 0; i < 5; i++) { shared_ptr<Dropper> e = new Dropper(); e.id = to_string(i); e.other = d; d = e; } }
                }
                run(); LOG;
            )";
            std::string oracle;
            for (uint32_t limit : {1000000u, 0u, 1u, 2u, 5u}) {
                auto eng = make_engine();
                jai::stdlib::register_all(eng);
                eng->release_recursion_limit(limit);
                std::string got;
                try {
                    got = eng->execute("string LOG = \"\";" + shapes).as_string();
                } catch (const std::exception& e) {
                    got = std::string("threw: ") + e.what();
                }
                if (oracle.empty()) {
                    oracle = got;
                    check(oracle.find("threw") == std::string::npos && oracle.size() > 100, oracle);
                } else {
                    check_eq(oracle, got, "limit " + std::to_string(limit));
                }
            }
        });
    }

    static constexpr const char* k_family = R"(
        class Base { int a = 1; Base() {} ~Base() { LOG = LOG + "B"; } }
        class D1 : Base { int d1 = 2; Token t1 = Token("t"); D1() {} ~D1() { LOG = LOG + "1"; } }
        class D2 : Base { int d2 = 3; D2() {} ~D2() { LOG = LOG + "2"; } }
    )";
};

}

using destructor_timing_tests = jai::foundry::tests::destructor_timing_tests;
FOUNDRY_REGISTER(destructor_timing_tests)
