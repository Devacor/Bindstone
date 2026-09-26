#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>

using namespace jai::foundry;

namespace jai::foundry::tests {

class string_stdlib_tests : public suite {
public:
    string_stdlib_tests() : suite("String Stdlib Tests") {}

    void forge_tests() override {
        // ============================================================
        // Observer methods (non-mutating)
        // ============================================================

        test("length_empty", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"\"; s.length();");
            check_eq(result.as<script_int>(), 0);
        });

        test("length_non_empty", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.length();");
            check_eq(result.as<script_int>(), 5);
        });

        test("size_same_as_length", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"test\"; s.size();");
            check_eq(result.as<script_int>(), 4);
        });

        test("empty_true", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"\"; s.empty();");
            check_eq(result.as<bool>(), true);
        });

        test("empty_false", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"a\"; s.empty();");
            check_eq(result.as<bool>(), false);
        });

        test("at_positive_index", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.at(1);");
            check_eq(result.as<std::string>(), "e");
        });

        test("at_negative_index", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.at(-1);");
            check_eq(result.as<std::string>(), "o");
        });

        test("at_negative_index_second_last", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.at(-2);");
            check_eq(result.as<std::string>(), "l");
        });

        test("front", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.front();");
            check_eq(result.as<std::string>(), "h");
        });

        test("back", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.back();");
            check_eq(result.as<std::string>(), "o");
        });

        test("substr_two_args", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.substr(0, 5);");
            check_eq(result.as<std::string>(), "hello");
        });

        test("substr_one_arg", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.substr(6);");
            check_eq(result.as<std::string>(), "world");
        });

        test("substr_negative_start", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.substr(-3);");
            check_eq(result.as<std::string>(), "llo");
        });

        // ============================================================
        // Search methods
        // ============================================================

        test("find_exists", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.find(\"wor\");");
            check_eq(result.as<script_int>(), 6);
        });

        test("find_not_exists", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.find(\"xyz\");");
            check_eq(result.as<script_int>(), -1);
        });

        test("find_with_start", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello hello\"; s.find(\"ell\", 3);");
            check_eq(result.as<script_int>(), 7);
        });

        test("rfind_exists", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello hello\"; s.rfind(\"ell\");");
            check_eq(result.as<script_int>(), 7);
        });

        test("contains_true", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.contains(\"wor\");");
            check_eq(result.as<bool>(), true);
        });

        test("contains_false", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.contains(\"xyz\");");
            check_eq(result.as<bool>(), false);
        });

        test("starts_with_true", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.starts_with(\"hello\");");
            check_eq(result.as<bool>(), true);
        });

        test("starts_with_false", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.starts_with(\"world\");");
            check_eq(result.as<bool>(), false);
        });

        test("ends_with_true", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.ends_with(\"world\");");
            check_eq(result.as<bool>(), true);
        });

        test("ends_with_false", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.ends_with(\"hello\");");
            check_eq(result.as<bool>(), false);
        });

        test("count_occurrences", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"abcabcabc\"; s.count(\"abc\");");
            check_eq(result.as<script_int>(), 3);
        });

        test("find_first_of", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.find_first_of(\"aeiou\");");
            check_eq(result.as<script_int>(), 1); // 'e' at position 1
        });

        test("find_last_of", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.find_last_of(\"aeiou\");");
            check_eq(result.as<script_int>(), 7); // 'o' at position 7
        });

        // ============================================================
        // Parsing methods
        // ============================================================

        test("to_int_valid", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"42\"; s.to_int();");
            check_eq(result.as<script_int>(), 42);
        });

        test("to_int_with_default", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"not_a_number\"; s.to_int(-1);");
            check_eq(result.as<script_int>(), -1);
        });

        test("to_int_hex", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"FF\"; s.to_int(0, 16);");
            check_eq(result.as<script_int>(), 255);
        });

        test("to_float_valid", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"3.14\"; s.to_float();");
            check(std::abs(result.as<double>() - 3.14) < 0.001);
        });

        test("to_float_with_default", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"not_a_number\"; s.to_float(-1.0);");
            check_eq(result.as<double>(), -1.0);
        });

        // ============================================================
        // Mutating methods (modify in place, return self for chaining)
        // ============================================================

        test("to_lower", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"HELLO\"; s.to_lower();");
            check_eq(result.as<std::string>(), "hello");
        });

        test("to_upper", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.to_upper();");
            check_eq(result.as<std::string>(), "HELLO");
        });

        // ============================================================
        // string:: namespace (NON-mutating: returns a new string, argument untouched)
        // ============================================================

        test("ns_to_lower_non_mutating", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"AbC 123!\"; var t = string::to_lower(s); t + \"|\" + s;");
            check_eq(result.as<std::string>(), "abc 123!|AbC 123!");
        });

        test("ns_to_upper_non_mutating", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"AbC 123!\"; var t = string::to_upper(s); t + \"|\" + s;");
            check_eq(result.as<std::string>(), "ABC 123!|AbC 123!");
        });

        test("ns_to_lower_literal_and_expr", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("string::to_lower(\"HeLLo\" + \" WoRLD\");");
            check_eq(result.as<std::string>(), "hello world");
        });

        test("ns_case_transform_type_errors", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            // Host-function errors surface at the engine boundary (like other stdlib
            // host bindings), not as script-catchable exceptions - assert the error
            // surfaces and the engine stays usable afterwards
            auto expect_error = [&](const char* src) {
                try {
                    auto r = engine->execute(src);
                    check(!r.is_string(), std::string("expected an error result for: ") + src);
                } catch (const std::exception&) {
                    // engine-boundary throw is the other legal surface
                }
            };
            expect_error("string::to_lower(42);");
            expect_error("string::to_upper(\"a\", \"b\");");
            check_eq(engine->execute("string::to_upper(\"ok\");").as<std::string>(), "OK");
        });

        test("trim_both_sides", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"  hello  \"; s.trim();");
            check_eq(result.as<std::string>(), "hello");
        });

        test("trim_with_chars", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"***hello***\"; s.trim(\"*\");");
            check_eq(result.as<std::string>(), "hello");
        });

        test("trim_left", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"  hello  \"; s.trim_left();");
            check_eq(result.as<std::string>(), "hello  ");
        });

        test("trim_right", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"  hello  \"; s.trim_right();");
            check_eq(result.as<std::string>(), "  hello");
        });

        test("pad_left", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hi\"; s.pad_left(5, '.');");
            check_eq(result.as<std::string>(), "...hi");
        });

        test("pad_right", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hi\"; s.pad_right(5, '.');");
            check_eq(result.as<std::string>(), "hi...");
        });

        test("pad_center", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hi\"; s.pad_center(6, '.');");
            check_eq(result.as<std::string>(), "..hi..");
        });

        test("replace_first", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello hello\"; s.replace_first(\"hello\", \"hi\");");
            check_eq(result.as<std::string>(), "hi hello");
        });

        test("replace_last", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello hello\"; s.replace_last(\"hello\", \"hi\");");
            check_eq(result.as<std::string>(), "hello hi");
        });

        test("replace_all", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello hello hello\"; s.replace_all(\"hello\", \"hi\");");
            check_eq(result.as<std::string>(), "hi hi hi");
        });

        test("replace_all_non_overlapping", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"aaaa\"; s.replace_all(\"aa\", \"b\");");
            check_eq(result.as<std::string>(), "bb");
        });

        test("insert", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"helloworld\"; s.insert(5, \" \");");
            check_eq(result.as<std::string>(), "hello world");
        });

        test("erase", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.erase(5, 1);");
            check_eq(result.as<std::string>(), "helloworld");
        });

        test("remove_prefix", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.remove_prefix(6);");
            check_eq(result.as<std::string>(), "world");
        });

        test("remove_suffix", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.remove_suffix(6);");
            check_eq(result.as<std::string>(), "hello");
        });

        test("reverse", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello\"; s.reverse();");
            check_eq(result.as<std::string>(), "olleh");
        });

        test("repeat", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"ab\"; s.repeat(3);");
            check_eq(result.as<std::string>(), "ababab");
        });

        // ============================================================
        // Chaining tests
        // ============================================================

        test("chaining_trim_to_lower", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"  HELLO  \"; s.trim().to_lower();");
            check_eq(result.as<std::string>(), "hello");
        });

        test("chaining_multiple_operations", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"  Hello World  \"; s.trim().to_lower().replace_all(\" \", \"_\");");
            check_eq(result.as<std::string>(), "hello_world");
        });

        // ============================================================
        // Split and Join
        // ============================================================

        test("split_by_delimiter", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"a,b,c\"; var arr = s.split(\",\"); arr.length();");
            check_eq(result.as<script_int>(), 3);
        });

        test("split_into_chars", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"abc\"; var arr = s.split(); arr.length();");
            check_eq(result.as<script_int>(), 3);
        });

        test("split_first_element", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"a,b,c\"; var arr = s.split(\",\"); arr[0];");
            check_eq(result.as<std::string>(), "a");
        });

        test("join_with_delimiter", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var arr = [\"a\", \"b\", \"c\"]; arr.join(\",\");");
            check_eq(result.as<std::string>(), "a,b,c");
        });

        test("join_no_delimiter", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var arr = [\"a\", \"b\", \"c\"]; arr.join();");
            check_eq(result.as<std::string>(), "abc");
        });

        test("split_and_join_roundtrip", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"hello world\"; s.split(\" \").join(\"-\");");
            check_eq(result.as<std::string>(), "hello-world");
        });

        // ============================================================
        // Mutation verification (ensure original is modified in place)
        // ============================================================

        test("mutation_to_lower_modifies_original", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"HELLO\"; s.to_lower(); s;");
            check_eq(result.as<std::string>(), "hello");
        });

        test("mutation_trim_modifies_original", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"  hello  \"; s.trim(); s;");
            check_eq(result.as<std::string>(), "hello");
        });

        test("mutation_replace_all_modifies_original", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("var s = \"aaa\"; s.replace_all(\"a\", \"b\"); s;");
            check_eq(result.as<std::string>(), "bbb");
        });

        test("template_string_basic", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(auto name = "world"; `hello ${name}`;)");
            check_eq(result.as<std::string>(), "hello world");
        });

        test("template_string_expression", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(auto x = 10; auto y = 20; `${x} + ${y} = ${x + y}`;)");
            check_eq(result.as<std::string>(), "10 + 20 = 30");
        });

        test("template_string_no_interpolation", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(`just a plain string`;)");
            check_eq(result.as<std::string>(), "just a plain string");
        });

        test("template_string_only_expression", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(auto val = 42; `${val}`;)");
            check_eq(result.as<std::string>(), "42");
        });

        test("template_string_nested_expressions", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(auto hp = 80; auto max_hp = 100; `Health: ${hp}/${max_hp} (${hp * 100 / max_hp}%)`;)");
            check_eq(result.as<std::string>(), "Health: 80/100 (80%)");
        });

        test("template_string_escape_backtick", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(`hello\`world`;)");
            check_eq(result.as<std::string>(), "hello`world");
        });

        // Splice depth is tracked on LEXED brace tokens (2026-07): whitespace before a
        // brace used to skip the depth bookkeeping - `${ x }` was a parse error and a
        // space-prefixed '{' swallowed the splice terminator (runaway template scan).
        test("template_string_splice_whitespace", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var x = 5; `${ x }`;)").as<std::string>(), "5");
            check_eq(engine->execute(R"(var y = 6; `${y }`;)").as<std::string>(), "6");
            check_eq(engine->execute(R"(var z = 7; `${ z}`;)").as<std::string>(), "7");
        });

        test("template_string_splice_map_literal", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(`${ {"k": 1}["k"] }`;)").as<std::string>(), "1");
            check_eq(engine->execute(R"(`${ {"a": {"b": 2}}["a"]["b"] }`;)").as<std::string>(), "2");
        });

        test("template_string_splice_no_runaway", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            // A following backtick string must stay a SEPARATE template (the broken
            // depth count used to swallow the terminator and chain into it)
            auto result = engine->execute(R"(var a = `${ {"k": 9}["k"] }`; var b = `ok`; a + b;)");
            check_eq(result.as<std::string>(), "9ok");
        });

        test("map_literal_postfix_index", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            // Map literals are primaries now: postfix applies directly
            check_eq((int64_t)1, engine->execute(R"({"k": 1}["k"];)").as_int());
            check_eq((int64_t)2, engine->execute(R"({"a": 1, "b": 2}.size();)").as_int());
        });

        test("template_string_escape_dollar", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(`price: \$${99}`;)");
            check_eq(result.as<std::string>(), "price: $99");
        });

        test("template_string_multiline", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute("`line1\nline2`;");
            check_eq(result.as<std::string>(), "line1\nline2");
        });

        // ============================================================
        // Template-string format specs: ${expr:spec}
        // spec = [[fill]align][width][.precision][f|x|X|b] (docs/grammar.md)
        // ============================================================

        test("template_spec_float_precision", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var pi = 3.14159; `${pi:.2f}`;)").as<std::string>(), "3.14");
            check_eq(engine->execute(R"(var pi = 3.14159; `${pi:.0f}`;)").as<std::string>(), "3");
            check_eq(engine->execute(R"(var pi = 3.14159; `${pi:.3}`;)").as<std::string>(), "3.142");  // bare .N is fixed
            check_eq(engine->execute(R"(var h = 80.0; `${h:f}`;)").as<std::string>(), "80.000000");    // :f default precision 6
        });

        test("template_spec_width_align_int", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var gold = 1234; `${gold:6}`;)").as<std::string>(), "  1234");  // numbers right-align
            check_eq(engine->execute(R"(var gold = 1234; `${gold:<6}`;)").as<std::string>(), "1234  ");
            check_eq(engine->execute(R"(var gold = 1234; `${gold:^6}`;)").as<std::string>(), " 1234 ");
        });

        test("template_spec_width_align_string", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var name = "Bob"; `${name:6}`;)").as<std::string>(), "Bob   ");  // strings left-align
            check_eq(engine->execute(R"(var name = "Bob"; `${name:>6}`;)").as<std::string>(), "   Bob");
            check_eq(engine->execute(R"(var name = "Bob"; `${name:^7}`;)").as<std::string>(), "  Bob  ");
        });

        test("template_spec_fill_char", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var gold = 1234; `${gold:*>8}`;)").as<std::string>(), "****1234");
            check_eq(engine->execute(R"(var name = "Bob"; `${name:-<6}`;)").as<std::string>(), "Bob---");
            check_eq(engine->execute(R"(var n = 7; `${n:0>4}`;)").as<std::string>(), "0007");  // '0' fill with explicit align
        });

        test("template_spec_hex_binary", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var n = 255; `${n:x}`;)").as<std::string>(), "ff");
            check_eq(engine->execute(R"(var n = 255; `${n:X}`;)").as<std::string>(), "FF");
            check_eq(engine->execute(R"(var n = 5; `${n:b}`;)").as<std::string>(), "101");
            check_eq(engine->execute(R"(var n = -255; `${n:x}`;)").as<std::string>(), "-ff");
        });

        test("template_spec_int_as_fixed", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var gold = 1234; `${gold:.1f}`;)").as<std::string>(), "1234.0");
        });

        test("template_spec_width_precision_fill_combo", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var pi = 3.14159; `${pi:>8.2f}`;)").as<std::string>(), "    3.14");
            check_eq(engine->execute(R"(var pi = 3.14159; `${pi:*^9.1f}`;)").as<std::string>(), "***3.1***");
        });

        test("template_spec_char_bool", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var c = 'a'; `${c:3}`;)").as<std::string>(), "a  ");
            check_eq(engine->execute(R"(var f = true; `${f:6}`;)").as<std::string>(), "true  ");
        });

        // The ambiguity rule: a ternary's top-level ':' closes its '?' and stays part of
        // the expression; only a top-level ':' that closes NO ternary starts a spec.
        test("template_spec_ternary_still_expression", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var a = true; `${a ? 1 : 2}`;)").as<std::string>(), "1");
            check_eq(engine->execute(R"(var a = false; var b = true; `${a ? 1 : b ? 2 : 3}`;)").as<std::string>(), "2");
            check_eq(engine->execute(R"(var a = true; `${(a ? 1 : 2):3}`;)").as<std::string>(), "  1");  // spec after parenthesized ternary
        });

        test("template_spec_after_subscript", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var m = {"k": 42}; `${m["k"]:6}`;)").as<std::string>(), "    42");
            check_eq(engine->execute(R"(`${ {"k": 3}["k"]:4}`;)").as<std::string>(), "   3");  // map literal's ':' is nested
        });

        test("template_spec_nested_template", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(engine->execute(R"(var pi = 3.14159; `${`${pi:.1f}`:>8}`;)").as<std::string>(), "     3.1");
        });

        test("template_spec_core_no_stdlib", [this]() {
            // Engine-core feature: works without stdlib::register_all
            auto engine = make_engine();
            check_eq(engine->execute(R"(var x = 3.5; `${x:.1f}`;)").as<std::string>(), "3.5");
            check_eq(engine->execute(R"(format_value(255, "X");)").as<std::string>(), "FF");  // desugar target, directly callable
        });

        test("template_spec_bound_global", [this]() {
            auto engine = make_engine();
            float hp = 87.5f;
            engine->add_global_ref("hp", hp);
            check_eq(engine->execute(R"(`${hp:>7.1f}`;)").as<std::string>(), "   87.5");
        });

        test("template_spec_malformed_is_lex_error", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto expect_lex_error = [&](const std::string& source, const std::string& fragment) {
                try {
                    engine->execute(source);
                    check(false, "expected a parse error for: " + source);
                } catch (const std::exception& e) {
                    check(std::string(e.what()).find(fragment) != std::string::npos,
                          "error for '" + source + "' should mention \"" + fragment + "\", got: " + e.what());
                }
            };
            expect_lex_error(R"(var x = 1; `${x:q}`;)", "Unsupported format spec ':q' in template string");
            expect_lex_error(R"(var x = 1; `${x:}`;)", "Unsupported format spec ':' in template string");
            expect_lex_error(R"(var x = 1; `${x:#x}`;)", "Unsupported format spec ':#x' in template string");  // no alternate form
            expect_lex_error(R"(var x = 1; `${x:.f}`;)", "Unsupported format spec ':.f' in template string");
            expect_lex_error(R"(var x = 1; `${x:5.}`;)", "Unsupported format spec ':5.' in template string");
            expect_lex_error(R"(var x = 1; `${x:-5}`;)", "Unsupported format spec ':-5' in template string");  // '+' and ' ' are the signs
        });

        test("template_spec_type_mismatch_is_runtime_error", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            // Catchable in-script, with identical text on both backends (one shared kernel)
            check_eq(engine->execute(R"(var name = "Bob"; var msg = ""; try { `${name:.2f}`; } catch (e) { msg = e; } msg;)").as<std::string>(),
                     "format spec ':.2f' does not apply to string value");
            check_eq(engine->execute(R"(var f = true; var msg = ""; try { `${f:x}`; } catch (e) { msg = e; } msg;)").as<std::string>(),
                     "format spec ':x' does not apply to bool value");
            check_eq(engine->execute(R"(var pi = 3.5; var msg = ""; try { `${pi:b}`; } catch (e) { msg = e; } msg;)").as<std::string>(),
                     "format spec ':b' does not apply to float value");
        });

        test("template_spec_hud_line", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(
                var hp = 73.5; var maxhp = 100.0; var gold = 1234; var name = "Grubwell";
                `HP ${hp:>6.1f}/${maxhp:<6.1f} G${gold:>7} ${name:<12}|`;
            )");
            check_eq(result.as<std::string>(), "HP   73.5/100.0  G   1234 Grubwell    |");
        });

        // Crash regression: a subscript read in argument position mints an element
        // reference; string builtins gate on is_string() (derefs) then read
        // unchecked_as_string() (doesn't) - s.find(parts[0]) was an access violation
        // on BOTH backends before deref_builtin_args (builtin_methods.hpp). Covers the
        // identifier-receiver direct path AND the minted bound-method path.
        test("subscript_arg_to_string_builtins", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(
                var s = "the quick brown fox";
                var parts = ["quick", "fox", "wolf"];
                s.find(parts[0]) == 4 &&
                s.count(parts[1]) == 1 &&
                s.find(parts[2]) == -1 &&
                ("prefix " + s).contains(parts[0]) &&
                s.starts_with(["the", "a"][0]);
            )");
            check_eq(result.as<bool>(), true);
        });

        // ============================================================
        // UTF-8 characters, everyday operations, the string:: namespace, format pieces
        // ============================================================

        // each helper reads the result while its engine is alive (a value holds its engine)
        auto eval = [this](const char* src) { auto engine = make_engine(); stdlib::register_all(*engine); engine->execute(src); };
        auto es = [this](const char* src) { auto engine = make_engine(); stdlib::register_all(*engine); return engine->execute(src).as<std::string>(); };
        auto ei = [this](const char* src) { auto engine = make_engine(); stdlib::register_all(*engine); return engine->execute(src).as<script_int>(); };
        auto eb = [this](const char* src) { auto engine = make_engine(); stdlib::register_all(*engine); return engine->execute(src).as<bool>(); };

        test("utf8_chars_walk_characters_and_keep_bad_bytes", [this, es, ei]() {
            check_eq(std::string("[h][\xC3\xA9][\xF0\x9F\x99\x82]"), es("string acc = \"\"; for (string ch : \"h\xC3\xA9\xF0\x9F\x99\x82\".utf8_chars()) { acc += \"[\" + ch + \"]\"; } acc;"));
            check_eq(script_int(3), ei("(\"a\" + \"\\xff\" + \"b\").utf8_chars().size();"));   // an invalid byte is its own element
            check_eq(script_int(3), ei("\"h\xC3\xA9\xF0\x9F\x99\x82\".utf8_length();"));
            check_eq(script_int(7), ei("\"h\xC3\xA9\xF0\x9F\x99\x82\".size();"));   // size stays bytes
            check_eq(script_int(7), ei("\"h\xC3\xA9\xF0\x9F\x99\x82\".byte_size();"));
        });

        test("utf8_validity_code_and_from_code", [this, eval, es, ei, eb]() {
            check_eq(true, eb("\"h\xC3\xA9\".is_valid_utf8();"));
            check_eq(false, eb("(\"a\" + \"\\xff\").is_valid_utf8();"));
            check_eq(false, eb("string::is_valid_utf8(\"\\xc3\");"));   // a truncated sequence
            check_eq(script_int(233), ei("\"\xC3\xA9\".code();"));
            check_eq(std::string("\xC3\xA9\xF0\x9F\x99\x82"), es("string::from_code(233) + string::from_code(128578);"));
            check_throws([&]() { eval("\"ab\".code();"); });
            check_throws([&]() { eval("string::from_code(55296);"); });   // a surrogate
        });

        test("lines_partition_compare_and_classes", [this, es, ei, eb]() {
            check_eq(std::string("a|b|c"), es("\"a\\nb\\r\\nc\\n\".lines().join(\"|\");"));
            check_eq(std::string("key|=|val=x"), es("\"key=val=x\".partition(\"=\").join(\"|\");"));
            check_eq(std::string("nope||"), es("\"nope\".partition(\"=\").join(\"|\");"));
            check_eq(script_int(-1), ei("\"abc\".compare(\"abd\");"));
            check_eq(script_int(0), ei("\"x\".compare(\"x\");"));
            check_eq(true, eb("\"123\".is_digit() && \"Ab\".is_alpha() && \"a1\".is_alnum() && \" \\t\".is_space() && \"AB\".is_upper() && \"ab\".is_lower();"));
            check_eq(false, eb("\"\".is_digit() || \"1a\".is_digit();"));
            check_eq(std::string("a|b|c,d"), es("\"a,b,c,d\".split(\",\", 2).join(\"|\");"));
        });

        test("string_namespace_free_functions", [this, eval, es]() {
            check_eq(std::string("a-b-c"), es("string::join([\"a\", \"b\", \"c\"], \"-\");"));
            check_eq(std::string("[hi][  hi  ]"), es("string t = \"  hi  \"; \"[\" + string::trim(t) + \"][\" + t + \"]\";"));   // the argument is untouched
            check_eq(std::string("a+b+c"), es("string::replace_all(\"a-b-c\", \"-\", \"+\");"));
            check_eq(std::string("ababab"), es("string::repeat(\"ab\", 3);"));
            check_eq(std::string("007x.."), es("string::pad_left(\"7\", 3, \"0\") + string::pad_right(\"x\", 3, '.');"));
            check_eq(std::string("HI!"), es("namespace string { function shout(string s) -> string { return string::to_upper(s) + \"!\"; } } string::shout(\"hi\");"));   // the namespace stays script-extensible
            check_throws([&]() { eval("string::nope(\"x\");"); });
        });

        test("format_sign_zero_and_float_types", [this, eval, es]() {
            check_eq(std::string("[00042] [-0042] [+7] [+3.5] [1.234500e+03] [0.00012] [255] [ 42] [0003.142]"),
                es("`[${42:05}] [${-42:05}] [${7:+}] [${3.5:+.1f}] [${1234.5:e}] [${0.00012:g}] [${255:d}] [${42: }] [${3.14159:08.3f}]`;"));
            check_eq(std::string("    ab|003.14|+5"), es("format(\"{0:>6}|{1:06.2f}|{2:+d}\", \"ab\", 3.14159, 5);"));
            check_eq(std::string("  x00"), es("`${\"x00\":>5}`;"));   // an explicit align on a string keeps its fill
            check_throws([&]() { eval("`${\"s\":05}`;"); });   // zero padding is numeric only
        });

        test("utf8_string_counts_characters", [this, es, ei, eb]() {
            check_eq(script_int(4), ei("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u.size();"));
            check_eq(script_int(4), ei("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u.length();"));
            check_eq(script_int(7), ei("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u.byte_size();"));
            check_eq(std::string("\xE6\x97\xA5"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u[2];"));
            check_eq(std::string("utf8_string"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; type_of(u[2]);"));
            check_eq(std::string("b|a|\xC3\xB1\xE6\x97\xA5" "b|\xC3\xB1\xE6\x97\xA5"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u.at(-1) + \"|\" + u.front() + \"|\" + u.substr(1) + \"|\" + u.substr(1, 2);"));
            check_eq(std::string("2|3|-1|true|true"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; to_string(u.find(\"\xE6\x97\xA5\")) + \"|\" + to_string(u.rfind(\"b\")) + \"|\" + to_string(u.find(\"z\")) + \"|\" + to_string(u.contains(\"\xC3\xB1\")) + \"|\" + to_string(u.starts_with(\"a\xC3\xB1\"));"));
            check_eq(std::string("b\xE6\x97\xA5\xC3\xB1" "a|4|aZ\xC3\xB1\xE6\x97\xA5" "b"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; utf8_string r = u; r.reverse(); utf8_string z = u; z.insert(1, \"Z\"); r + \"|\" + to_string(u.split(\"\").size()) + \"|\" + z;"));
            check_eq(std::string("\xC3\xA9\xC3\xA9\xE6\x97\xA5|\xE6\x97\xA5\xC3\xA9\xC3\xA9"), es("utf8_string a = \"\xE6\x97\xA5\"; a.pad_left(3, \"\xC3\xA9\"); utf8_string b = \"\xE6\x97\xA5\"; b.pad_right(3, \"\xC3\xA9\"); a + \"|\" + b;"));
            check_eq(std::string("utf8_string|utf8_string|utf8_string"), es("utf8_string u = \"a b\"; type_of(u.split(\" \")[0]) + \"|\" + type_of(u.to_upper()) + \"|\" + type_of(u.substr(0, 1));"));
            check_eq(std::string("H\xC3\xA9LLO"), es("utf8_string u = \"h\xC3\xA9llo\"; u.to_upper(); u;"));
        });

        test("utf8_string_operators_and_slots", [this, es, ei, eb]() {
            check_eq(std::string("utf8_string|utf8_string|utf8_string|string"), es("utf8_string u = \"\xC3\xA9\"; type_of(u + \"x\") + \"|\" + type_of(\"x\" + u) + \"|\" + type_of(u + 5) + \"|\" + type_of(\"x\" + \"y\");"));
            check_eq(true, eb("utf8_string u = \"a\xC3\xB1\"; u == \"a\xC3\xB1\" && \"a\xC3\xB1\" == u && !(u != \"a\xC3\xB1\") && u < \"b\" && \"\xC3\xA9\" > u;"));
            check_eq(true, eb("utf8_string u = \"\"; utf8_string v = \"x\"; !u && v;"));
            check_eq(std::string("string|3|utf8_string|2"), es("utf8_string u = \"a\xC3\xB1\"; string s = u; utf8_string back = s; type_of(s) + \"|\" + to_string(s.size()) + \"|\" + type_of(back) + \"|\" + to_string(back.size());"));
            check_eq(std::string("utf8_string|12"), es("utf8_string n = 12; type_of(n) + \"|\" + n;"));
            check_eq(script_int(2), ei("function f(utf8_string t) -> utf8_string { return t + \"x\"; } f(\"\xC3\xBC\").size();"));
            check_eq(std::string("utf8_string|utf8_string|2|3"), es("var w = utf8_string(\"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\"); class H { utf8_string t = \"\xC3\xBC\"; } H h = H(); h.t += \"x\"; array<var> a = [w]; a[0] += \"!\"; type_of(h.t) + \"|\" + type_of(a[0]) + \"|\" + to_string(h.t.size()) + \"|\" + to_string(w.size());"));
            check_eq(std::string("utf8_string|string|utf8_string"), es("type_of(string::join([\"a\".to_utf8(), \"b\"], \"-\")) + \"|\" + type_of(string::join([\"a\", \"b\"], \"-\")) + \"|\" + type_of(string::trim(\"  \xC3\xBC \".to_utf8()));"));
            check_eq(script_int(2), ei("map<var, int> m = {}; m[\"a\xC3\xB1\".to_utf8()] = 1; m[\"a\xC3\xB1\"] = 2; m[utf8_string(\"a\xC3\xB1\")] = 3; m.size();"));
            check_eq(std::string("[\"\xC3\xA9\",\"x\"]"), es("to_json([\"\xC3\xA9\".to_utf8(), \"x\"]);"));
            check_eq(std::string("utf8_string|3"), es("var r = from_binary(to_binary(\"a\xC3\xB1\".to_utf8())); type_of(r) + \"|\" + to_string(r.byte_size());"));
        });

        test("utf8_string_iteration_and_errors", [this, eval, es, ei]() {
            check_eq(std::string("[a][\xC3\xB1][\xE6\x97\xA5][b]"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; string out = \"\"; for (auto c : u) { out += \"[\" + c + \"]\"; } out;"));
            check_eq(script_int(7), ei("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; int n = 0; for (int i = 0; i < u.size(); ++i) { n += u[i].byte_size(); } n;"));
            check_eq(std::string("a\xC3\xB1\xE6\x97\xA5" "b|aZ\xC3\xB1\xE6\x97\xA5" "b"), es("utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; var c = u; c.insert(1, \"Z\"); u + \"|\" + c;"));
            check_eq(std::string("String index 4 out of bounds for string of size 4"), es("string m = \"\"; try { utf8_string u = \"a\xC3\xB1\xE6\x97\xA5" "b\"; u[4]; } catch (e) { m = e; } m;"));
            check_eq(std::string("Cannot iterate a utf8_string by reference: its characters are not lvalues (use for (auto c : s))"), es("string m = \"\"; try { utf8_string u = \"ab\"; for (auto& c : u) {} } catch (e) { m = e; } m;"));
            check_eq(std::string("Strings are read-only through subscript: use substr()/+ to build a new string"), es("string m = \"\"; try { utf8_string u = \"ab\"; u[0] = \"x\"; } catch (e) { m = e; } m;"));
            check_eq(std::string("utf8_string has no member 'to_utf8'"), es("string m = \"\"; try { utf8_string u = \"ab\"; u.to_utf8(); } catch (e) { m = e; } m;"));
        });

        test("utf8_string_positions_over_a_long_text", [this, ei]() {
            check_eq(script_int(0), ei("string s = \"\"; for (int i = 0; i < 1000; ++i) { s += \"a\xC3\xA9\xE6\x97\xA5\"; } utf8_string u = s; array<var> ch = s.utf8_chars(); int bad = 0; for (int k = 0; k < 3000; k += 7) { int i = (k * 1237) % 3000; if (u[i] != ch[i]) { ++bad; } } for (int i = 2999; i >= 0; i -= 3) { if (u[i] != ch[i]) { ++bad; } } u.insert(0, \"\xE6\x97\xA5\"); if (u[1] != \"a\" || u.size() != 3001 || u[3000] != \"\xE6\x97\xA5\") { ++bad; } bad;"));
        });

        test("utf8_string_declarations_survive_the_jaibite_cache", [this]() {
            namespace fs = std::filesystem;
            fs::path dir = jai::foundry::test_temp_directory() / "jai_utf8_bite";
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir);
            std::ofstream(dir / "main.jai") << "utf8_string u = \"a\xC3\xB1\"; var n = u.size(); var k = type_of(u);\n";
            for (int run = 0; run < 2; ++run) {   // the first run writes the sibling, the second loads it
                auto engine = make_engine(); stdlib::register_all(*engine);
                engine->execute_file((dir / "main.jai").string());
                check_eq(script_int(2), engine->execute("n;").as<script_int>());
                check_eq(std::string("utf8_string"), engine->execute("k;").as<std::string>());
                check_true(fs::exists(dir / "main.jaibite"));
            }
            fs::remove_all(dir, ec);
        });

        test("proven_builtin_calls_keep_the_general_semantics", [this, es]() {
            // the builtin-call instruction's inline paths and its fallbacks: results, error texts, argument kinds, the receiver
            // re-read after a reassigning argument
            check_eq(std::string("4|-1|17|true|false|true|true|uick|ick brown fox|q|f|3|1|true|7"), es(R"(
                string s = "the quick brown fox";
                string out = "" + s.find("quick") + "|" + s.find("zz") + "|" + s.find("o", 13) + "|" + s.contains("fox") + "|" +
                    s.contains("cat") + "|" + s.starts_with("the") + "|" + s.ends_with("fox") + "|" + s.substr(5, 4) + "|" +
                    s.substr(-13) + "|" + s.at(4) + "|" + s.at(-3);
                map<string, int> m = {"a": 3};
                array<int> xs = [];
                out = out + "|" + m.get("a", 9) + "|" + m.size() + "|" + m.has("a") + "|" + (s.size() - 12);
                out;)"));
            check_eq(std::string("find() first argument must be a string|at() index out of bounds|substr() start argument must be an integer|05"), es(R"(
                string s = "hello"; var n = 3; var t = "x"; string out = "";
                try { s.find(n); } catch (e) { out = out + e; }
                try { s.at(9); } catch (e) { out = out + "|" + e; }
                try { s.substr(t); } catch (e) { out = out + "|" + e; }
                out = out + "|" + s.substr(99).size() + s.size();
                out;)"));
            check_eq(std::string("3"), es(R"(
                string s = "abc";
                function swap() -> string { s = "xyzw"; return "w"; }
                "" + s.find(swap());)"));
        });

        test("template_chains_append_in_order", [this, es]() {
            check_eq(std::string("a1b2.5ctruedx|AB:A,B|utf8_string|3|string"), es(R"(
                string trail = "";
                class P { string tag; P(string t) { tag = t; } function to_string() -> string { trail = trail + tag; return tag; } }
                int i = 1; float f = 2.5; bool b = true; char c = 'x';
                string first = `a${i}b${f}c${b}d${c}`;
                string order = `${P("A")},${P("B")}`;
                var u = `${"é".to_utf8()}!`;
                string typed = "q"; string joined = `${typed}${typed}${typed}`;
                first + "|" + trail + ":" + order + "|" + type_of(u) + "|" + joined.size() + "|" + type_of(joined);)"));
        });

        test("floats_print_shortest_and_round_trip", [this, es, eb]() {
            check_eq(std::string("0.30000000000000004|2.0|0.3333333333333333|1e+20|-0.0|1e-07|123456789.0"),
                es("to_string(0.1 + 0.2) + \"|\" + to_string(2.0) + \"|\" + to_string(1.0 / 3.0) + \"|\" + to_string(1e20) + \"|\" + to_string(-0.0) + \"|\" + to_string(1e-7) + \"|\" + to_string(123456789.0);"));
            check_eq(std::string("x2.5 [1.5] 1.0,2.5"), es("array<float> a = [1.0, 2.5]; \"x\" + 2.5 + ` [${1.5}] ` + a.join(\",\");"));
            check_eq(true, eb("array<float> v = [0.1, 0.1 + 0.2, 1.0 / 3.0, 2.0, 1e20, 1e-300, 5e-324, 1.7976931348623157e308]; bool ok = true; "
                              "for (int i = 0; i < v.size(); ++i) { float x = v[i]; float t = to_string(x).to_float(); var j = from_json(to_json([x])); float y = j[0]; if (t != x || y != x) { ok = false; } } ok;"));
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::string_stdlib_tests)
