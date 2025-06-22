#include "../jai_test.hpp"
#include "jaiscript/detail/lexer.hpp"
#include "jaiscript/jaiscript_fwd.hpp"

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(Lexer)

// Basic lexer functionality tests
JAI_TEST(empty_input) {
    Lexer lexer("", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(1), tokens.size());
    expect_eq(TokenType::Eof, tokens[0].type);
}

JAI_TEST(single_identifier) {
    Lexer lexer("foo", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(2), tokens.size());
    expect_eq(TokenType::Identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(TokenType::Eof, tokens[1].type);
}

JAI_TEST(keywords) {
    Lexer lexer("int float bool class new", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(6), tokens.size());
    expect_eq(TokenType::Int, tokens[0].type);
    expect_eq(TokenType::Float, tokens[1].type);
    expect_eq(TokenType::Bool, tokens[2].type);
    expect_eq(TokenType::Class, tokens[3].type);
    expect_eq(TokenType::New, tokens[4].type);
}

JAI_TEST(integer_literals) {
    Lexer lexer("42 0 999", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::IntegerLiteral, tokens[0].type);
    expect_eq(42, tokens[0].intValue);
    expect_eq(TokenType::IntegerLiteral, tokens[1].type);
    expect_eq(0, tokens[1].intValue);
    expect_eq(TokenType::IntegerLiteral, tokens[2].type);
    expect_eq(999, tokens[2].intValue);
}

JAI_TEST(float_literals) {
    Lexer lexer("3.14 0.0 42.0", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::FloatLiteral, tokens[0].type);
    expect_eq(3.14, tokens[0].floatValue);
    expect_eq(TokenType::FloatLiteral, tokens[1].type);
    expect_eq(0.0, tokens[1].floatValue);
    expect_eq(TokenType::FloatLiteral, tokens[2].type);
    expect_eq(42.0, tokens[2].floatValue);
}

JAI_TEST(string_literals) {
    Lexer lexer(R"("hello" "world" "hello\nworld")", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::StringLiteral, tokens[0].type);
    expect_eq(std::string("hello"), tokens[0].stringValue);
    expect_eq(TokenType::StringLiteral, tokens[1].type);
    expect_eq(std::string("world"), tokens[1].stringValue);
    expect_eq(TokenType::StringLiteral, tokens[2].type);
    expect_eq(std::string("hello\nworld"), tokens[2].stringValue);
}

JAI_TEST(character_literals) {
    Lexer lexer("'a' '\\n' '0'", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::CharLiteral, tokens[0].type);
    expect_eq('a', tokens[0].charValue);
    expect_eq(TokenType::CharLiteral, tokens[1].type);
    expect_eq('\n', tokens[1].charValue);
    expect_eq(TokenType::CharLiteral, tokens[2].type);
    expect_eq('0', tokens[2].charValue);
}

JAI_TEST(boolean_literals) {
    Lexer lexer("true false", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::True, tokens[0].type);
    expect_eq(TokenType::False, tokens[1].type);
}

// Operator tests
JAI_TEST(arithmetic_operators) {
    Lexer lexer("+ - * / %", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(6), tokens.size());
    expect_eq(TokenType::Plus, tokens[0].type);
    expect_eq(TokenType::Minus, tokens[1].type);
    expect_eq(TokenType::Star, tokens[2].type);
    expect_eq(TokenType::Slash, tokens[3].type);
    expect_eq(TokenType::Percent, tokens[4].type);
}

JAI_TEST(compound_assignment) {
    Lexer lexer("+= -= *= /= %=", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(6), tokens.size());
    expect_eq(TokenType::PlusEqual, tokens[0].type);
    expect_eq(TokenType::MinusEqual, tokens[1].type);
    expect_eq(TokenType::StarEqual, tokens[2].type);
    expect_eq(TokenType::SlashEqual, tokens[3].type);
    expect_eq(TokenType::PercentEqual, tokens[4].type);
}

JAI_TEST(comparison_operators) {
    Lexer lexer("== != < > <= >=", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(7), tokens.size());
    expect_eq(TokenType::EqualEqual, tokens[0].type);
    expect_eq(TokenType::BangEqual, tokens[1].type);
    expect_eq(TokenType::Less, tokens[2].type);
    expect_eq(TokenType::Greater, tokens[3].type);
    expect_eq(TokenType::LessEqual, tokens[4].type);
    expect_eq(TokenType::GreaterEqual, tokens[5].type);
}

JAI_TEST(logical_operators) {
    Lexer lexer("&& || !", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::AmpersandAmpersand, tokens[0].type);
    expect_eq(TokenType::PipePipe, tokens[1].type);
    expect_eq(TokenType::Bang, tokens[2].type);
}

JAI_TEST(increment_decrement) {
    Lexer lexer("++ --", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::PlusPlus, tokens[0].type);
    expect_eq(TokenType::MinusMinus, tokens[1].type);
}

JAI_TEST(member_access) {
    Lexer lexer(". -> ::", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::Dot, tokens[0].type);
    expect_eq(TokenType::Arrow, tokens[1].type);
    expect_eq(TokenType::ColonColon, tokens[2].type);
}

JAI_TEST(special_operators) {
    Lexer lexer("? : & ~", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(5), tokens.size());
    expect_eq(TokenType::Question, tokens[0].type);
    expect_eq(TokenType::Colon, tokens[1].type);
    expect_eq(TokenType::Ampersand, tokens[2].type);
    expect_eq(TokenType::Tilde, tokens[3].type);
}

JAI_TEST(spaceship_operator_tokenization) {
    Lexer lexer("a <=> b", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::Identifier, tokens[0].type);
    expect_eq("a", tokens[0].lexeme);
    expect_eq(TokenType::Spaceship, tokens[1].type);
    expect_eq("<=>", tokens[1].lexeme);
    expect_eq(TokenType::Identifier, tokens[2].type);
    expect_eq("b", tokens[2].lexeme);
}

JAI_TEST(spaceship_operator_single_token) {
    Lexer lexer("<=>", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(2), tokens.size());
    expect_eq(TokenType::Spaceship, tokens[0].type);
    expect_eq("<=>", tokens[0].lexeme);
}

JAI_TEST(spaceship_operator_no_spaces) {
    Lexer lexer("a<=>b", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(TokenType::Identifier, tokens[0].type);
    expect_eq("a", tokens[0].lexeme);
    expect_eq(TokenType::Spaceship, tokens[1].type);
    expect_eq("<=>", tokens[1].lexeme);
    expect_eq(TokenType::Identifier, tokens[2].type);
    expect_eq("b", tokens[2].lexeme);
}

JAI_TEST(spaceship_not_confused_with_other_operators) {
    Lexer lexer("<= > < => <=>", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(7), tokens.size());
    expect_eq(TokenType::LessEqual, tokens[0].type);
    expect_eq(TokenType::Greater, tokens[1].type);
    expect_eq(TokenType::Less, tokens[2].type);
    expect_eq(TokenType::Equal, tokens[3].type);
    expect_eq(TokenType::Greater, tokens[4].type);
    expect_eq(TokenType::Spaceship, tokens[5].type);
    expect_eq("<=>", tokens[5].lexeme);
}

// Delimiter tests
JAI_TEST(parentheses) {
    Lexer lexer("()", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::LeftParen, tokens[0].type);
    expect_eq(TokenType::RightParen, tokens[1].type);
}

JAI_TEST(braces) {
    Lexer lexer("{}", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::LeftBrace, tokens[0].type);
    expect_eq(TokenType::RightBrace, tokens[1].type);
}

JAI_TEST(brackets) {
    Lexer lexer("[]", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::LeftBracket, tokens[0].type);
    expect_eq(TokenType::RightBracket, tokens[1].type);
}

JAI_TEST(punctuation) {
    Lexer lexer("; ,", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::Semicolon, tokens[0].type);
    expect_eq(TokenType::Comma, tokens[1].type);
}

// Comment tests
JAI_TEST(single_line_comment) {
    Lexer lexer("foo // comment\nbar", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::Identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(TokenType::Identifier, tokens[1].type);
    expect_eq(std::string("bar"), tokens[1].lexeme);
}

JAI_TEST(multi_line_comment) {
    Lexer lexer("foo /* comment */ bar", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::Identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(TokenType::Identifier, tokens[1].type);
    expect_eq(std::string("bar"), tokens[1].lexeme);
}

JAI_TEST(multi_line_comment_spanning_lines) {
    Lexer lexer("foo /*\ncomment\n*/ bar", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(3), tokens.size());
    expect_eq(TokenType::Identifier, tokens[0].type);
    expect_eq(std::string("foo"), tokens[0].lexeme);
    expect_eq(TokenType::Identifier, tokens[1].type);
    expect_eq(std::string("bar"), tokens[1].lexeme);
}

// Special case tests
JAI_TEST(super_keyword) {
    Lexer lexer("super::method()", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(5), tokens.size());
    expect_eq(TokenType::Super, tokens[0].type);
    expect_eq(std::string("super::"), tokens[0].lexeme);
    expect_eq(TokenType::Identifier, tokens[1].type);
    expect_eq(std::string("method"), tokens[1].lexeme);
}

JAI_TEST(generic_types) {
    Lexer lexer("array<int> map<string, float>", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(11), tokens.size()); // 10 tokens + EOF
    expect_eq(TokenType::Array, tokens[0].type);
    expect_eq(TokenType::Less, tokens[1].type);
    expect_eq(TokenType::Int, tokens[2].type);
    expect_eq(TokenType::Greater, tokens[3].type);
    expect_eq(TokenType::Map, tokens[4].type);
    expect_eq(TokenType::Less, tokens[5].type);
    expect_eq(TokenType::String, tokens[6].type);
    expect_eq(TokenType::Comma, tokens[7].type);
    expect_eq(TokenType::Float, tokens[8].type);
    expect_eq(TokenType::Greater, tokens[9].type);
}

JAI_TEST(smart_pointers) {
    Lexer lexer("SharedPtr<T> WeakPtr<T>", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(9), tokens.size()); // 8 tokens + EOF
    expect_eq(TokenType::SharedPtr, tokens[0].type);
    expect_eq(TokenType::Less, tokens[1].type);
    expect_eq(TokenType::Identifier, tokens[2].type);
    expect_eq(TokenType::Greater, tokens[3].type);
    expect_eq(TokenType::WeakPtr, tokens[4].type);
}

JAI_TEST(line_counting) {
    Lexer lexer("line1\nline2\nline3", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(std::size_t(4), tokens.size());
    expect_eq(std::size_t(1), tokens[0].location.line);
    expect_eq(std::size_t(2), tokens[1].location.line);
    expect_eq(std::size_t(3), tokens[2].location.line);
}

// Error handling tests
JAI_TEST(invalid_character_error) {
    Lexer lexer("@", "test.jai");
    auto tokens = lexer.tokenize();
    expect_eq(TokenType::Error, tokens[0].type);
    expect_eq(std::string("Unexpected character"), tokens[0].lexeme);
}

JAI_TEST(unterminated_string_error) {
    Lexer lexer("\"unterminated", "test.jai");
    auto tokens = lexer.tokenize();
    bool foundError = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::Error) {
            foundError = true;
            expect_eq(std::string("Unterminated string literal"), token.lexeme);
            break;
        }
    }
    expect_true(foundError, "Expected to find an error token");
}

JAI_TEST(unterminated_char_error) {
    Lexer lexer("'a", "test.jai");
    auto tokens = lexer.tokenize();
    bool foundError = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::Error) {
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
    Lexer lexer(source, "benchmark.jai");
    auto tokens = lexer.tokenize();
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
    Lexer lexer(source, "benchmark.jai");
    auto tokens = lexer.tokenize();
}

JAI_BENCHMARK(tokenize_large_file) {
    // Simulate a large file with many tokens
    std::string source;
    for (int i = 0; i < 1000; ++i) {
        source += "var x" + std::to_string(i) + " = " + std::to_string(i) + " * 2 + 3;\n";
    }
    Lexer lexer(source, "benchmark.jai");
    auto tokens = lexer.tokenize();
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()