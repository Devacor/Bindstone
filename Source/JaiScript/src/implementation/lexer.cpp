#include "../../include/jaiscript/detail/lexer.hpp"
#include <cctype>
#include <sstream>

namespace jai {

// Initialize keyword map
const std::unordered_map<std::string, token_type> lexer::keywords_ = {
    {"bool", token_type::bool_keyword},
    {"break", token_type::break_keyword},
    {"char", token_type::char_keyword},
    {"class", token_type::class_keyword},
    {"const", token_type::const_keyword},
    {"continue", token_type::continue_keyword},
    {"else", token_type::else_keyword},
    {"false", token_type::false_keyword},
    {"float", token_type::float_keyword},
    {"for", token_type::for_keyword},
    {"function", token_type::function_keyword},
    {"if", token_type::if_keyword},
    {"int", token_type::int_keyword},
    {"map", token_type::map_keyword},
    {"array", token_type::array_keyword},
    {"null", token_type::null_keyword},
    {"nullptr", token_type::null_keyword},
    {"private", token_type::private_keyword},
    {"public", token_type::public_keyword},
    {"return", token_type::return_keyword},
    {"string", token_type::string_keyword},
    {"this", token_type::this_keyword},
    {"true", token_type::true_keyword},
    {"void", token_type::void_keyword},
    {"while", token_type::while_keyword},
    {"auto", token_type::auto_keyword},
    {"var", token_type::var_keyword},
    {"super", token_type::super_keyword},
    {"weak_ptr", token_type::weak_ptr_keyword},
    {"try", token_type::try_keyword},
    {"catch", token_type::catch_keyword},
    {"throw", token_type::throw_keyword},
};

lexer::lexer(const std::string& source, const std::string& filename)
    : source_(source), filename_(filename), current_(0), line_(1), column_(1) {}

lexer::lexer(const std::string& source, const std::unordered_set<std::string>& registeredTypes, const std::string& filename)
    : source_(source), filename_(filename), current_(0), line_(1), column_(1), registered_types_(registeredTypes) {}

std::vector<token> lexer::tokenize() {
    std::vector<token> tokens;
    while (!is_at_end()) {
        token token = next_token();
        tokens.push_back(token);
        if (token.type == token_type::eof) {
            break;
        }
    }
    // Always ensure we have an EOF token at the end
    if (tokens.empty() || tokens.back().type != token_type::eof) {
        tokens.push_back(make_token(token_type::eof));
    }
    return tokens;
}

token lexer::next_token() {
    skip_whitespace();
    
    if (is_at_end()) {
        return make_token(token_type::eof);
    }
    
    char c = advance();
    
    // Numbers
    if (is_digit(c)) {
        current_--;  // Back up to rescan
        column_--;
        return scan_number();
    }
    
    // Identifiers and keywords
    if (is_alpha(c) || c == '_') {
        current_--;  // Back up to rescan
        column_--;
        return scan_identifier();
    }
    
    // script_string literals
    if (c == '"') {
        return scan_string();
    }
    
    // Character literals
    if (c == '\'') {
        return scan_char();
    }
    
    // Single character tokens
    switch (c) {
        case '(': return make_token(token_type::left_paren);
        case ')': return make_token(token_type::right_paren);
        case '{': return make_token(token_type::left_brace);
        case '}': return make_token(token_type::right_brace);
        case '[': return make_token(token_type::left_bracket);
        case ']': return make_token(token_type::right_bracket);
        case ';': return make_token(token_type::semicolon);
        case ',': return make_token(token_type::comma);
        case '?': return make_token(token_type::question);
        case '~': return make_token(token_type::tilde);
        case '^': return make_token(token_type::caret);
        
        // Operators that might be compound
        case '+':
            if (match('+')) return make_token(token_type::plus_plus);
            if (match('=')) return make_token(token_type::plus_equal);
            return make_token(token_type::plus);
            
        case '-':
            if (match('-')) return make_token(token_type::minus_minus);
            if (match('=')) return make_token(token_type::minus_equal);
            if (match('>')) return make_token(token_type::arrow);
            return make_token(token_type::minus);
            
        case '*':
            if (match('=')) return make_token(token_type::star_equal);
            return make_token(token_type::star);
            
        case '/':
            if (match('/')) {
                // Single line comment
                skip_comment();
                return next_token();
            }
            if (match('*')) {
                // Multi-line comment
                while (!is_at_end()) {
                    if (peek() == '*' && peek_next() == '/') {
                        advance(); // *
                        advance(); // /
                        break;
                    }
                    if (advance() == '\n') {
                        line_++;
                        column_ = 1;
                    }
                }
                return next_token();
            }
            if (match('=')) return make_token(token_type::slash_equal);
            return make_token(token_type::slash);
            
        case '%':
            if (match('=')) return make_token(token_type::percent_equal);
            return make_token(token_type::percent);
            
        case '&':
            if (match('&')) return make_token(token_type::ampersand_ampersand);
            return make_token(token_type::ampersand);
            
        case '|':
            if (match('|')) return make_token(token_type::pipe_pipe);
            return make_token(token_type::pipe);
            
        case '!':
            if (match('=')) return make_token(token_type::bang_equal);
            return make_token(token_type::bang);
            
        case '=':
            if (match('=')) return make_token(token_type::equal_equal);
            return make_token(token_type::equal);
            
        case '<':
            if (match('=')) {
                if (match('>')) return make_token(token_type::spaceship); // <=>
                return make_token(token_type::less_equal); // <=
            }
            if (match('<')) return make_token(token_type::left_shift); // <<
            return make_token(token_type::less);
            
        case '>':
            if (match('=')) return make_token(token_type::greater_equal);
            if (match('>')) return make_token(token_type::right_shift); // >>
            return make_token(token_type::greater);
            
        case '.':
            return make_token(token_type::dot);
            
        case ':':
            if (match(':')) return make_token(token_type::colon_colon);
            return make_token(token_type::colon);
    }
    
    return error_token("Unexpected character");
}

token lexer::peek_token() {
    size_t savedCurrent = current_;
    size_t savedLine = line_;
    size_t savedColumn = column_;
    
    token token = next_token();
    
    current_ = savedCurrent;
    line_ = savedLine;
    column_ = savedColumn;
    
    return token;
}

char lexer::advance() {
    column_++;
    return source_[current_++];
}

char lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[current_];
}

char lexer::peek_next() const {
    if (current_ + 1 >= source_.length()) return '\0';
    return source_[current_ + 1];
}

bool lexer::match(char expected) {
    if (is_at_end()) return false;
    if (source_[current_] != expected) return false;
    advance();
    return true;
}

void lexer::skip_whitespace() {
    while (!is_at_end()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                advance();
                line_++;
                column_ = 1;
                break;
            default:
                return;
        }
    }
}

void lexer::skip_comment() {
    while (!is_at_end() && peek() != '\n') {
        advance();
    }
}

token lexer::make_token(token_type type) {
    // For single-character tokens, include the character as lexeme
    std::string lexeme;
    
    switch (type) {
        case token_type::left_paren: lexeme = "("; break;
        case token_type::right_paren: lexeme = ")"; break;
        case token_type::left_brace: lexeme = "{"; break;
        case token_type::right_brace: lexeme = "}"; break;
        case token_type::left_bracket: lexeme = "["; break;
        case token_type::right_bracket: lexeme = "]"; break;
        case token_type::less: lexeme = "<"; break;
        case token_type::greater: lexeme = ">"; break;
        case token_type::semicolon: lexeme = ";"; break;
        case token_type::comma: lexeme = ","; break;
        case token_type::dot: lexeme = "."; break;
        case token_type::question: lexeme = "?"; break;
        case token_type::colon: lexeme = ":"; break;
        case token_type::plus: lexeme = "+"; break;
        case token_type::minus: lexeme = "-"; break;
        case token_type::star: lexeme = "*"; break;
        case token_type::slash: lexeme = "/"; break;
        case token_type::percent: lexeme = "%"; break;
        case token_type::ampersand: lexeme = "&"; break;
        case token_type::pipe: lexeme = "|"; break;
        case token_type::caret: lexeme = "^"; break;
        case token_type::tilde: lexeme = "~"; break;
        case token_type::bang: lexeme = "!"; break;
        case token_type::equal: lexeme = "="; break;
        
        // Multi-character operators
        case token_type::plus_plus: lexeme = "++"; break;
        case token_type::minus_minus: lexeme = "--"; break;
        case token_type::plus_equal: lexeme = "+="; break;
        case token_type::minus_equal: lexeme = "-="; break;
        case token_type::star_equal: lexeme = "*="; break;
        case token_type::slash_equal: lexeme = "/="; break;
        case token_type::percent_equal: lexeme = "%="; break;
        case token_type::arrow: lexeme = "->"; break;
        case token_type::colon_colon: lexeme = "::"; break;
        case token_type::equal_equal: lexeme = "=="; break;
        case token_type::bang_equal: lexeme = "!="; break;
        case token_type::less_equal: lexeme = "<="; break;
        case token_type::greater_equal: lexeme = ">="; break;
        case token_type::ampersand_ampersand: lexeme = "&&"; break;
        case token_type::pipe_pipe: lexeme = "||"; break;
        case token_type::spaceship: lexeme = "<=>"; break;
        case token_type::left_shift: lexeme = "<<"; break;
        case token_type::right_shift: lexeme = ">>"; break;
        
        default: 
            // For other tokens like EOF, just leave empty
            break;
    }
    
    return token(type, lexeme, current_location());
}

token lexer::make_token(token_type type, const std::string& lexeme) {
    return token(type, lexeme, current_location());
}

token lexer::error_token(const std::string& message) {
    token token(token_type::error, message, current_location());
    return token;
}

token lexer::scan_number() {
    size_t start = current_;
    size_t startColumn = column_;
    
    // Integer part
    while (is_digit(peek())) {
        advance();
    }
    
    bool is_float = false;
    
    // Look for decimal part
    if (peek() == '.' && is_digit(peek_next())) {
        is_float = true;
        advance(); // Consume '.'
        while (is_digit(peek())) {
            advance();
        }
    }
    
    // Look for exponent
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        while (is_digit(peek())) {
            advance();
        }
    }
    
    std::string lexeme = source_.substr(start, current_ - start);
    source_location loc = current_location();
    loc.column = startColumn;
    
    token token(is_float ? token_type::float_literal : token_type::integer_literal, lexeme, loc);
    
    if (is_float) {
        token.float_value = std::stod(lexeme);
    } else {
        // Handle different integer formats
        if (lexeme.size() > 2 && lexeme[0] == '0') {
            if (lexeme[1] == 'x' || lexeme[1] == 'X') {
                // Hexadecimal
                token.int_value = std::stoll(lexeme.substr(2), nullptr, 16);
            } else if (lexeme[1] == 'b' || lexeme[1] == 'B') {
                // Binary
                token.int_value = std::stoll(lexeme.substr(2), nullptr, 2);
            } else {
                // Octal
                token.int_value = std::stoll(lexeme, nullptr, 8);
            }
        } else {
            // Decimal
            token.int_value = std::stoll(lexeme);
        }
    }
    
    return token;
}

token lexer::scan_string() {
    size_t startColumn = column_ - 1; // Account for opening quote
    size_t startLine = line_;
    std::string value;
    
    while (!is_at_end() && peek() != '"') {
        if (peek() == '\n') {
            return error_token("Unterminated string literal");
        }
        
        if (peek() == '\\') {
            advance();
            if (is_at_end()) {
                return error_token("Unterminated string literal");
            }
            
            char escaped = advance();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '\'': value += '\''; break;
                default:
                    return error_token("Invalid escape sequence");
            }
        } else {
            value += advance();
        }
    }
    
    if (is_at_end()) {
        return error_token("Unterminated string literal");
    }
    
    advance(); // Consume closing quote
    
    source_location loc = current_location();
    loc.line = startLine;
    loc.column = startColumn;
    
    token token(token_type::string_literal, "\"" + value + "\"", loc);
    token.string_value = value;
    return token;
}

token lexer::scan_char() {
    size_t startColumn = column_ - 1; // Account for opening quote
    
    if (is_at_end()) {
        return error_token("Unterminated character literal");
    }
    
    char value;
    if (peek() == '\\') {
        advance();
        if (is_at_end()) {
            return error_token("Unterminated character literal");
        }
        
        char escaped = advance();
        switch (escaped) {
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case '\\': value = '\\'; break;
            case '"': value = '"'; break;
            case '\'': value = '\''; break;
            default:
                return error_token("Invalid escape sequence in character literal");
        }
    } else {
        value = advance();
    }
    
    if (is_at_end() || peek() != '\'') {
        return error_token("Unterminated character literal");
    }
    
    advance(); // Consume closing quote
    
    source_location loc = current_location();
    loc.column = startColumn;
    
    token token(token_type::char_literal, std::string("'") + value + "'", loc);
    token.char_value = value;
    return token;
}

token lexer::scan_identifier() {
    size_t start = current_;
    size_t startColumn = column_;
    
    while (is_alpha_numeric(peek()) || peek() == '_') {
        advance();
    }
    
    std::string lexeme = source_.substr(start, current_ - start);
    
    source_location loc = current_location();
    loc.column = startColumn;
    
    // Check for 'super::' special case BEFORE keyword lookup
    if (lexeme == "super" && peek() == ':' && peek_next() == ':') {
        advance(); // :
        advance(); // :
        return token(token_type::super_keyword, "super::", loc);
    }
    
    // Check if it's a keyword
    auto it = keywords_.find(lexeme);
    if (it != keywords_.end()) {
        token token(it->second, lexeme, loc);
        
        // Handle boolean literals
        if (it->second == token_type::true_keyword) {
            token.bool_value = true;
        } else if (it->second == token_type::false_keyword) {
            token.bool_value = false;
        }
        
        return token;
    }
    
    // Check if it's a registered template type base name (e.g., "Point" for Point<int>)
    if (registered_types_.find(lexeme) != registered_types_.end()) {
        return token(token_type::user_template_type, lexeme, loc);
    }
    
    return token(token_type::identifier, lexeme, loc);
}

bool lexer::is_digit(char c) const {
    return c >= '0' && c <= '9';
}

bool lexer::is_alpha(char c) const {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

bool lexer::is_alpha_numeric(char c) const {
    return is_alpha(c) || is_digit(c);
}

source_location lexer::current_location() const {
    source_location loc;
    loc.filename = filename_;
    loc.line = line_;
    loc.column = column_;
    return loc;
}

// token method implementations
std::string token::to_string() const {
    std::stringstream ss;
    ss << location.to_string() << " " << static_cast<int>(type) << " '" << lexeme << "'";
    return ss.str();
}

bool token::is_keyword() const {
    return type >= token_type::bool_keyword && type <= token_type::weak_ptr_keyword;
}

bool token::is_operator() const {
    return type >= token_type::plus && type <= token_type::tilde;
}

bool token::is_literal() const {
    return type >= token_type::integer_literal && type <= token_type::null_literal;
}

} // namespace jai