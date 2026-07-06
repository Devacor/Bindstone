#include "../../include/jaiscript/detail/lexer.hpp"
#include "../../include/jaiscript/detail/string_symbolizer.hpp"
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
    {"namespace", token_type::namespace_keyword},
    {"null", token_type::null_keyword},
    {"nullptr", token_type::null_keyword},
    {"override", token_type::override_keyword},
    {"private", token_type::private_keyword},
    {"public", token_type::public_keyword},
    {"return", token_type::return_keyword},
    {"static", token_type::static_keyword},
    {"string", token_type::string_keyword},
    {"this", token_type::this_keyword},
    {"true", token_type::true_keyword},
    {"void", token_type::void_keyword},
    {"while", token_type::while_keyword},
    {"auto", token_type::auto_keyword},
    {"var", token_type::var_keyword},
    {"super", token_type::super_keyword},
    {"weak_ptr", token_type::weak_ptr_keyword},
    {"shared_ptr", token_type::shared_ptr_keyword},
    {"try", token_type::try_keyword},
    {"catch", token_type::catch_keyword},
    {"throw", token_type::throw_keyword},
    {"switch", token_type::switch_keyword},
    {"case", token_type::case_keyword},
    {"default", token_type::default_keyword},
    {"fallthrough", token_type::fallthrough_keyword},
    {"include", token_type::include_keyword},
    {"import", token_type::import_keyword},
    {"coroutine", token_type::coroutine_keyword},
    {"yield", token_type::yield_keyword},
    {"enum", token_type::enum_keyword},
    {"new", token_type::new_keyword},
};

lexer::lexer(const std::string& source, string_symbolizer* symbolizer, const std::string& filename)
    : source_(source), filename_(filename), symbolizer_(symbolizer), current_(0), line_(1), column_(1) {}

lexer::lexer(const std::string& source, string_symbolizer* symbolizer, const std::unordered_set<std::string>& registeredTypes, const std::string& filename)
    : source_(source), filename_(filename), symbolizer_(symbolizer), current_(0), line_(1), column_(1), registered_types_(registeredTypes) {}

std::vector<token> lexer::tokenize() {
    std::vector<token> tokens;
    // Loop until next_token() actually produces EOF, rather than gating on
    // is_at_end(). A template-string literal (`...`) desugars into a queue of
    // pending tokens; scan_template_string() consumes the source up to and
    // including the closing backtick, so if the literal is the LAST content in
    // the source, is_at_end() becomes true after the first pending token is
    // returned. Gating the loop on !is_at_end() would then drop the remaining
    // pending tokens. next_token() drains the pending queue first and only
    // returns EOF once it is empty AND the source is exhausted.
    while (true) {
        token token = next_token();
        tokens.push_back(token);
        if (token.type == token_type::eof) {
            break;
        }
    }
    return tokens;
}

token lexer::next_token() {
    if (pending_index_ < pending_tokens_.size()) {
        return pending_tokens_[pending_index_++];
    }
    if (!pending_tokens_.empty()) {
        pending_tokens_.clear();
        pending_index_ = 0;
    }

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
    
    if (c == '`') {
        scan_template_string();
        if (pending_index_ < pending_tokens_.size()) {
            return pending_tokens_[pending_index_++];
        }
        return make_token(token_type::eof);
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
        case '?':
            if (match('.')) return make_token(token_type::question_dot);
            return make_token(token_type::question);
        case '~': return make_token(token_type::tilde);
        case '^': return make_token(token_type::caret);
        
        // Handle #include and #import (# is optional/ignored)
        case '#':
            // Skip the # and process the next token
            skip_whitespace();
            return next_token();
        
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
    // Use static string_views for operators (string literals have static storage duration)
    std::string_view lexeme;

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

token lexer::make_token(token_type type, std::string_view lexeme) {
    return token(type, lexeme, current_location());
}

token lexer::error_token(const std::string& message) {
    auto [id, view] = symbolizer_->intern_with_view(message);
    token token(token_type::error, view, current_location());
    return token;
}

token lexer::scan_number() {
    size_t start = current_;
    size_t startColumn = column_;

    bool is_float = false;
    bool is_hex = false;
    bool is_binary = false;

    if (peek() == '0' && !is_at_end()) {
        advance(); // consume '0'
        if (peek() == 'x' || peek() == 'X') {
            is_hex = true;
            advance(); // consume 'x'/'X'
            while (!is_at_end() && (is_digit(peek()) || (peek() >= 'a' && peek() <= 'f') || (peek() >= 'A' && peek() <= 'F'))) {
                advance();
            }
        } else if (peek() == 'b' || peek() == 'B') {
            is_binary = true;
            advance(); // consume 'b'/'B'
            while (!is_at_end() && (peek() == '0' || peek() == '1')) {
                advance();
            }
        } else {
            // Octal or plain 0 — continue with normal digit scanning
            while (is_digit(peek())) {
                advance();
            }
        }
    } else {
        while (is_digit(peek())) {
            advance();
        }
    }

    if (!is_hex && !is_binary) {
        if (peek() == '.' && is_digit(peek_next())) {
            is_float = true;
            advance(); // Consume '.'
            while (is_digit(peek())) {
                advance();
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (!is_digit(peek())) {
                std::string_view lexeme_view(source_.data() + start, current_ - start);
                return error_token("Invalid number literal: exponent has no digits in '" + std::string(lexeme_view) + "'");
            }
            while (is_digit(peek())) {
                advance();
            }
        }
    }

    // Use string_view into source_ (permanent storage)
    std::string_view lexeme_view(source_.data() + start, current_ - start);
    source_location loc = current_location();
    loc.column = startColumn;

    token tok(is_float ? token_type::float_literal : token_type::integer_literal, lexeme_view, loc);

    // Need temporary string for parsing (stod/stoll need null-terminated)
    std::string lexeme_str(lexeme_view);

    try {
        if (is_float) {
            tok.float_value = std::stod(lexeme_str);
        } else if (is_hex) {
            tok.int_value = std::stoll(lexeme_str, nullptr, 16);
        } else if (is_binary) {
            tok.int_value = std::stoll(lexeme_str.substr(2), nullptr, 2);
        } else if (lexeme_str.size() > 1 && lexeme_str[0] == '0' && is_digit(lexeme_str[1])) {
            // C-style octal (leading 0). std::stoll(base 8) stops at the first
            // non-octal digit WITHOUT erroring (e.g. "08" -> 0, "019" -> 1), so
            // verify the whole literal was consumed; otherwise it is malformed.
            size_t consumed = 0;
            tok.int_value = std::stoll(lexeme_str, &consumed, 8);
            if (consumed != lexeme_str.size()) {
                return error_token("Invalid octal literal (digits 8/9 not allowed): '" + lexeme_str + "'");
            }
        } else {
            tok.int_value = std::stoll(lexeme_str);
        }
    } catch (const std::out_of_range&) {
        return error_token("Number literal out of range: '" + lexeme_str + "'");
    } catch (const std::invalid_argument&) {
        return error_token("Invalid number literal: '" + lexeme_str + "'");
    }

    return tok;
}

void lexer::scan_template_string() {
    // Desugar `hello ${expr} world` into: ("" + "hello " + (expr) + " world")
    // The leading "" ensures string context so + auto-converts non-strings.
    // IMPORTANT: Build into a local vector, not pending_tokens_, because
    // next_token() is called recursively for ${} expressions and would
    // drain pending_tokens_ instead of scanning the source.
    std::vector<token> result;

    source_location start_loc = current_location();

    auto emit_string_part = [&](const std::string& part) {
        auto [id, view] = symbolizer_->intern_with_view(part);
        token tok(token_type::string_literal, view, current_location());
        tok.string_value = part;
        result.push_back(tok);
    };

    auto emit_plus = [&]() {
        result.push_back(token(token_type::plus, symbolizer_->intern_with_view("+").second, current_location()));
    };

    // Opening paren and empty string to establish string context
    result.push_back(token(token_type::left_paren, symbolizer_->intern_with_view("(").second, start_loc));
    emit_string_part("");

    std::string current_part;

    while (!is_at_end() && peek() != '`') {
        if (peek() == '$' && peek_next() == '{') {
            emit_plus();
            emit_string_part(current_part);
            current_part.clear();

            emit_plus();
            result.push_back(token(token_type::left_paren, symbolizer_->intern_with_view("(").second, current_location()));

            advance(); // skip $
            advance(); // skip {

            int brace_depth = 1;
            while (!is_at_end() && brace_depth > 0) {
                // Recursive next_token() is safe here because pending_tokens_
                // is not being used (we're building into local `result`).
                // Depth bookkeeping happens on the LEXED brace tokens, not raw peeks:
                // whitespace before a brace made next_token() consume it without a
                // depth adjustment (`${ x }` was a parse error, and a space-prefixed
                // '{' swallowed the splice terminator).
                token expr_tok = next_token();
                if (expr_tok.type == token_type::eof || expr_tok.type == token_type::error) break;
                if (expr_tok.type == token_type::left_brace) {
                    ++brace_depth;
                } else if (expr_tok.type == token_type::right_brace) {
                    --brace_depth;
                    if (brace_depth == 0) {
                        break;   // splice terminator: not part of the expression
                    }
                }
                result.push_back(expr_tok);
            }

            result.push_back(token(token_type::right_paren, symbolizer_->intern_with_view(")").second, current_location()));

        } else if (peek() == '\\') {
            advance(); // skip backslash
            if (!is_at_end()) {
                char escaped = advance();
                switch (escaped) {
                    case 'n': current_part += '\n'; break;
                    case 't': current_part += '\t'; break;
                    case 'r': current_part += '\r'; break;
                    case '\\': current_part += '\\'; break;
                    case '`': current_part += '`'; break;
                    case '$': current_part += '$'; break;
                    default: current_part += '\\'; current_part += escaped; break;
                }
            }
        } else {
            if (peek() == '\n') { line_++; column_ = 0; }
            current_part += advance();
        }
    }

    if (!current_part.empty()) {
        emit_plus();
        emit_string_part(current_part);
    }

    if (!is_at_end() && peek() == '`') {
        advance();
    }

    result.push_back(token(token_type::right_paren, symbolizer_->intern_with_view(")").second, current_location()));

    pending_tokens_ = std::move(result);
    pending_index_ = 0;
}

token lexer::scan_string() {
    size_t start = current_ - 1;  // Include opening quote
    size_t startColumn = column_ - 1;
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

    // Use string_view into source_ for the raw lexeme (includes quotes)
    std::string_view lexeme_view(source_.data() + start, current_ - start);
    token tok(token_type::string_literal, lexeme_view, loc);
    tok.string_value = std::move(value);
    return tok;
}

token lexer::scan_char() {
    size_t start = current_ - 1;  // Include opening quote
    size_t startColumn = column_ - 1;

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

    // Use string_view into source_ for the raw lexeme (includes quotes)
    std::string_view lexeme_view(source_.data() + start, current_ - start);
    token tok(token_type::char_literal, lexeme_view, loc);
    tok.char_value = value;
    return tok;
}

token lexer::scan_identifier() {
    size_t start = current_;
    size_t startColumn = column_;

    while (is_alpha_numeric(peek()) || peek() == '_') {
        advance();
    }

    // Create string_view into source for comparisons (no allocation)
    std::string_view lexeme_view(source_.data() + start, current_ - start);

    source_location loc = current_location();
    loc.column = startColumn;

    // Check for 'super::' special case BEFORE keyword lookup
    if (lexeme_view == "super" && peek() == ':' && peek_next() == ':') {
        advance(); // :
        advance(); // :
        return token(token_type::super_keyword, "super::", loc);
    }

    // Check if it's a keyword (keywords use static string storage)
    // Need temporary std::string for map lookup (keyword map uses std::string keys)
    std::string lexeme_str(lexeme_view);
    auto it = keywords_.find(lexeme_str);
    if (it != keywords_.end()) {
        // For keywords, use string_view to the keyword string in the map (permanent storage)
        token tok(it->second, it->first, loc);

        // Handle boolean literals
        if (it->second == token_type::true_keyword) {
            tok.bool_value = true;
        } else if (it->second == token_type::false_keyword) {
            tok.bool_value = false;
        }

        return tok;
    }

    // Check if it's a registered template type base name (e.g., "Point" for Point<int>)
    auto reg_it = registered_types_.find(lexeme_str);
    if (reg_it != registered_types_.end()) {
        // Point to the registered type string (permanent storage in the set)
        return token(token_type::user_template_type, *reg_it, loc);
    }

    // For regular identifiers - symbolizer is always required
    // Intern the identifier so lexeme points to permanent symbolizer storage
    uint64_t sym_id = symbolizer_->intern(lexeme_view);
    std::string_view interned = symbolizer_->get_string(sym_id);
    return token(token_type::identifier, interned, sym_id, loc);
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
    return type >= token_type::bool_keyword && type <= token_type::enum_keyword;
}

bool token::is_operator() const {
    return (type >= token_type::plus && type <= token_type::tilde) ||
           type == token_type::left_shift || type == token_type::right_shift ||
           type == token_type::spaceship;
}

bool token::is_literal() const {
    return type >= token_type::integer_literal && type <= token_type::null_literal;
}

} // namespace jai