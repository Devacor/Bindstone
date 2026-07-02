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
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::string_stdlib_tests)
