#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

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
            expect_lex_error(R"(var x = 1; `${x:06}`;)", "Unsupported format spec ':06' in template string");  // no zero-pad flag
            expect_lex_error(R"(var x = 1; `${x:.f}`;)", "Unsupported format spec ':.f' in template string");
            expect_lex_error(R"(var x = 1; `${x:5.}`;)", "Unsupported format spec ':5.' in template string");
            expect_lex_error(R"(var x = 1; `${x: 5}`;)", "Unsupported format spec ': 5' in template string");  // spec is raw text after ':'
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
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::string_stdlib_tests)
