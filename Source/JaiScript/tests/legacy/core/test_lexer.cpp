#include "../jai_test.hpp"
#include "jaiscript/detail/lexer.hpp"
#include "jaiscript/jaiscript_fwd.hpp"

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(lexer)

// Basic lexer functionality tests
JAI_TEST(empty_input) {
    lexer lex("", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(1), tokens.size());
    expect_eq(token_type::eof, tokens[0].type);
}

JAI_TEST(single_identifier) {
    lexer lex("foo", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(2), tokens.size());
    expect_eq(token_type::identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(token_type::eof, tokens[1].type);
}

JAI_TEST(keywords) {
    lexer lex("int float bool class", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(5), tokens.size());
    expect_eq(token_type::int_keyword, tokens[0].type);
    expect_eq(token_type::float_keyword, tokens[1].type);
    expect_eq(token_type::bool_keyword, tokens[2].type);
    expect_eq(token_type::class_keyword, tokens[3].type);
}

JAI_TEST(integer_literals) {
    lexer lex("42 0 999", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::integer_literal, tokens[0].type);
    expect_eq(42, tokens[0].int_value);
    expect_eq(token_type::integer_literal, tokens[1].type);
    expect_eq(0, tokens[1].int_value);
    expect_eq(token_type::integer_literal, tokens[2].type);
    expect_eq(999, tokens[2].int_value);
}

JAI_TEST(float_literals) {
    lexer lex("3.14 0.0 42.0", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::float_literal, tokens[0].type);
    expect_eq(3.14, tokens[0].float_value);
    expect_eq(token_type::float_literal, tokens[1].type);
    expect_eq(0.0, tokens[1].float_value);
    expect_eq(token_type::float_literal, tokens[2].type);
    expect_eq(42.0, tokens[2].float_value);
}

JAI_TEST(string_literals) {
    lexer lex(R"("hello" "world" "hello\nworld")", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::string_literal, tokens[0].type);
    expect_eq(std::string("hello"), tokens[0].string_value);
    expect_eq(token_type::string_literal, tokens[1].type);
    expect_eq(std::string("world"), tokens[1].string_value);
    expect_eq(token_type::string_literal, tokens[2].type);
    expect_eq(std::string("hello\nworld"), tokens[2].string_value);
}

JAI_TEST(character_literals) {
    lexer lex("'a' '\\n' '0'", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::char_literal, tokens[0].type);
    expect_eq('a', tokens[0].char_value);
    expect_eq(token_type::char_literal, tokens[1].type);
    expect_eq('\n', tokens[1].char_value);
    expect_eq(token_type::char_literal, tokens[2].type);
    expect_eq('0', tokens[2].char_value);
}

JAI_TEST(boolean_literals) {
    lexer lex("true false", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::true_keyword, tokens[0].type);
    expect_eq(token_type::false_keyword, tokens[1].type);
}

// Operator tests
JAI_TEST(arithmetic_operators) {
    lexer lex("+ - * / %", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(6), tokens.size());
    expect_eq(token_type::plus, tokens[0].type);
    expect_eq(token_type::minus, tokens[1].type);
    expect_eq(token_type::star, tokens[2].type);
    expect_eq(token_type::slash, tokens[3].type);
    expect_eq(token_type::percent, tokens[4].type);
}

JAI_TEST(compound_assignment) {
    lexer lex("+= -= *= /= %=", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(6), tokens.size());
    expect_eq(token_type::plus_equal, tokens[0].type);
    expect_eq(token_type::minus_equal, tokens[1].type);
    expect_eq(token_type::star_equal, tokens[2].type);
    expect_eq(token_type::slash_equal, tokens[3].type);
    expect_eq(token_type::percent_equal, tokens[4].type);
}

JAI_TEST(comparison_operators) {
    lexer lex("== != < > <= >=", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(7), tokens.size());
    expect_eq(token_type::equal_equal, tokens[0].type);
    expect_eq(token_type::bang_equal, tokens[1].type);
    expect_eq(token_type::less, tokens[2].type);
    expect_eq(token_type::greater, tokens[3].type);
    expect_eq(token_type::less_equal, tokens[4].type);
    expect_eq(token_type::greater_equal, tokens[5].type);
}

JAI_TEST(logical_operators) {
    lexer lex("&& || !", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::ampersand_ampersand, tokens[0].type);
    expect_eq(token_type::pipe_pipe, tokens[1].type);
    expect_eq(token_type::bang, tokens[2].type);
}

JAI_TEST(increment_decrement) {
    lexer lex("++ --", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::plus_plus, tokens[0].type);
    expect_eq(token_type::minus_minus, tokens[1].type);
}

JAI_TEST(member_access) {
    lexer lex(". -> ::", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::dot, tokens[0].type);
    expect_eq(token_type::arrow, tokens[1].type);
    expect_eq(token_type::colon_colon, tokens[2].type);
}

JAI_TEST(special_operators) {
    lexer lex("? : & ~", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(5), tokens.size());
    expect_eq(token_type::question, tokens[0].type);
    expect_eq(token_type::colon, tokens[1].type);
    expect_eq(token_type::ampersand, tokens[2].type);
    expect_eq(token_type::tilde, tokens[3].type);
}

JAI_TEST(spaceship_operator_tokenization) {
    lexer lex("a <=> b", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::identifier, tokens[0].type);
    expect_eq("a", tokens[0].lexeme);
    expect_eq(token_type::spaceship, tokens[1].type);
    expect_eq("<=>", tokens[1].lexeme);
    expect_eq(token_type::identifier, tokens[2].type);
    expect_eq("b", tokens[2].lexeme);
}

JAI_TEST(spaceship_operator_single_token) {
    lexer lex("<=>", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(2), tokens.size());
    expect_eq(token_type::spaceship, tokens[0].type);
    expect_eq("<=>", tokens[0].lexeme);
}

JAI_TEST(spaceship_operator_no_spaces) {
    lexer lex("a<=>b", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(token_type::identifier, tokens[0].type);
    expect_eq("a", tokens[0].lexeme);
    expect_eq(token_type::spaceship, tokens[1].type);
    expect_eq("<=>", tokens[1].lexeme);
    expect_eq(token_type::identifier, tokens[2].type);
    expect_eq("b", tokens[2].lexeme);
}

JAI_TEST(spaceship_not_confused_with_other_operators) {
    lexer lex("<= > < => <=>", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(7), tokens.size());
    expect_eq(token_type::less_equal, tokens[0].type);
    expect_eq(token_type::greater, tokens[1].type);
    expect_eq(token_type::less, tokens[2].type);
    expect_eq(token_type::equal, tokens[3].type);
    expect_eq(token_type::greater, tokens[4].type);
    expect_eq(token_type::spaceship, tokens[5].type);
    expect_eq("<=>", tokens[5].lexeme);
}

// Delimiter tests
JAI_TEST(parentheses) {
    lexer lex("()", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::left_paren, tokens[0].type);
    expect_eq(token_type::right_paren, tokens[1].type);
}

JAI_TEST(braces) {
    lexer lex("{}", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::left_brace, tokens[0].type);
    expect_eq(token_type::right_brace, tokens[1].type);
}

JAI_TEST(brackets) {
    lexer lex("[]", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::left_bracket, tokens[0].type);
    expect_eq(token_type::right_bracket, tokens[1].type);
}

JAI_TEST(punctuation) {
    lexer lex("; ,", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::semicolon, tokens[0].type);
    expect_eq(token_type::comma, tokens[1].type);
}

// Comment tests
JAI_TEST(single_line_comment) {
    lexer lex("foo // comment\nbar", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(token_type::identifier, tokens[1].type);
    expect_eq(std::string("bar"), tokens[1].lexeme);
}

JAI_TEST(multi_line_comment) {
    lexer lex("foo /* comment */ bar", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(token_type::identifier, tokens[1].type);
    expect_eq(std::string("bar"), tokens[1].lexeme);
}

JAI_TEST(multi_line_comment_spanning_lines) {
    lexer lex("foo /*\ncomment\n*/ bar", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(token_type::identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(token_type::identifier, tokens[1].type);
    expect_eq(std::string("bar"), tokens[1].lexeme);
}

// Special case tests
JAI_TEST(super_keyword) {
    lexer lex("super::method()", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(5), tokens.size());
    expect_eq(token_type::super_keyword, tokens[0].type);
    expect_eq(std::string("super::"), tokens[0].lexeme);
    expect_eq(token_type::identifier, tokens[1].type);
    expect_eq(std::string("method"), tokens[1].lexeme);
}

JAI_TEST(generic_types) {
    lexer lex("array<int> map<string, float>", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(11), tokens.size()); // 10 tokens + EOF
    expect_eq(token_type::array_keyword, tokens[0].type);
    expect_eq(token_type::less, tokens[1].type);
    expect_eq(token_type::int_keyword, tokens[2].type);
    expect_eq(token_type::greater, tokens[3].type);
    expect_eq(token_type::map_keyword, tokens[4].type);
    expect_eq(token_type::less, tokens[5].type);
    expect_eq(token_type::string_keyword, tokens[6].type);
    expect_eq(token_type::comma, tokens[7].type);
    expect_eq(token_type::float_keyword, tokens[8].type);
    expect_eq(token_type::greater, tokens[9].type);
}

JAI_TEST(smart_pointers) {
    lexer lex("shared_ptr<T> weak_ptr<T>", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(9), tokens.size()); // 8 tokens + EOF
    expect_eq(token_type::shared_ptr_keyword, tokens[0].type);
    expect_eq(token_type::less, tokens[1].type);
    expect_eq(token_type::identifier, tokens[2].type);
    expect_eq(token_type::greater, tokens[3].type);
    expect_eq(token_type::weak_ptr_keyword, tokens[4].type);
}

JAI_TEST(line_counting) {
    lexer lex("line1\nline2\nline3", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(std::size_t(1), tokens[0].location.line);
    expect_eq(std::size_t(2), tokens[1].location.line);
    expect_eq(std::size_t(3), tokens[2].location.line);
}

// Error handling tests
JAI_TEST(invalid_character_error) {
    lexer lex("@", "test.jai");
    auto tokens = lex.tokenize();
    expect_eq(token_type::error, tokens[0].type);
    expect_eq(std::string("Unexpected character"), tokens[0].lexeme);
}

JAI_TEST(unterminated_string_error) {
    lexer lex("\"unterminated", "test.jai");
    auto tokens = lex.tokenize();
    bool foundError = false;
    for (const auto& token : tokens) {
        if (token.type == token_type::error) {
            foundError = true;
            expect_eq(std::string("Unterminated string literal"), token.lexeme);
            break;
        }
    }
    expect_true(foundError, "Expected to find an error token");
}

JAI_TEST(unterminated_char_error) {
    lexer lex("'a", "test.jai");
    auto tokens = lex.tokenize();
    bool foundError = false;
    for (const auto& token : tokens) {
        if (token.type == token_type::error) {
            foundError = true;
            expect_eq(std::string("Unterminated character literal"), token.lexeme);
            break;
        }
    }
    expect_true(foundError, "Expected to find an error token");
}

// Benchmark tests
JAI_BENCHMARK(tokenize_simple_expression) {
    std::string source = "x = 10 + 20 * 30 - 40 / 50;";
    lexer lex(source, "benchmark.jai");
    auto tokens = lex.tokenize();
}

JAI_BENCHMARK(tokenize_complex_code) {
    std::string source = R"(
        class Point {
            float x, y;
            Point(float x, float y) : x(x), y(y) {}
            float distance() { return sqrt(x*x + y*y); }
        }
        
        void main() {
            var p1 = Point(3.0, 4.0);
            var p2 = Point(6.0, 8.0);
            var sum = p1.distance() + p2.distance();
        }
    )";
    lexer lex(source, "benchmark.jai");
    auto tokens = lex.tokenize();
}

JAI_BENCHMARK(tokenize_large_file) {
    // Simulate a large file with many tokens
    std::string source;
    for (int i = 0; i < 1000; ++i) {
        source += "var x" + std::to_string(i) + " = " + std::to_string(i) + " * 2 + 3;\n";
    }
    lexer lex(source, "benchmark.jai");
    auto tokens = lex.tokenize();
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()