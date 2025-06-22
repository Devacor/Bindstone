#include "../../include/jaiscript/detail/lexer.hpp"
#include <cctype>
#include <sstream>

namespace JaiScript {

// Initialize keyword map
const std::unordered_map<std::string, TokenType> Lexer::keywords_ = {
    {"bool", TokenType::Bool},
    {"break", TokenType::Break},
    {"char", TokenType::Char},
    {"class", TokenType::Class},
    {"const", TokenType::Const},
    {"continue", TokenType::Continue},
    {"else", TokenType::Else},
    {"false", TokenType::False},
    {"float", TokenType::Float},
    {"for", TokenType::For},
    {"function", TokenType::Function},
    {"if", TokenType::If},
    {"int", TokenType::Int},
    {"map", TokenType::Map},
    {"array", TokenType::Array},
    {"new", TokenType::New},
    {"null", TokenType::Null},
    {"private", TokenType::Private},
    {"public", TokenType::Public},
    {"return", TokenType::Return},
    {"string", TokenType::String},
    {"this", TokenType::This},
    {"true", TokenType::True},
    {"void", TokenType::Void},
    {"while", TokenType::While},
    {"auto", TokenType::Auto},
    {"var", TokenType::Var},
    {"super", TokenType::Super},
    {"SharedPtr", TokenType::SharedPtr},
    {"WeakPtr", TokenType::WeakPtr},
};

Lexer::Lexer(const std::string& source, const std::string& filename)
    : source_(source), filename_(filename), current_(0), line_(1), column_(1) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!isAtEnd()) {
        Token token = nextToken();
        tokens.push_back(token);
        if (token.type == TokenType::Eof) {
            break;
        }
    }
    // Always ensure we have an EOF token at the end
    if (tokens.empty() || tokens.back().type != TokenType::Eof) {
        tokens.push_back(makeToken(TokenType::Eof));
    }
    return tokens;
}

Token Lexer::nextToken() {
    skipWhitespace();
    
    if (isAtEnd()) {
        return makeToken(TokenType::Eof);
    }
    
    char c = advance();
    
    // Numbers
    if (isDigit(c)) {
        current_--;  // Back up to rescan
        column_--;
        return scanNumber();
    }
    
    // Identifiers and keywords
    if (isAlpha(c) || c == '_') {
        current_--;  // Back up to rescan
        column_--;
        return scanIdentifier();
    }
    
    // String literals
    if (c == '"') {
        return scanString();
    }
    
    // Character literals
    if (c == '\'') {
        return scanChar();
    }
    
    // Single character tokens
    switch (c) {
        case '(': return makeToken(TokenType::LeftParen);
        case ')': return makeToken(TokenType::RightParen);
        case '{': return makeToken(TokenType::LeftBrace);
        case '}': return makeToken(TokenType::RightBrace);
        case '[': return makeToken(TokenType::LeftBracket);
        case ']': return makeToken(TokenType::RightBracket);
        case ';': return makeToken(TokenType::Semicolon);
        case ',': return makeToken(TokenType::Comma);
        case '?': return makeToken(TokenType::Question);
        case '~': return makeToken(TokenType::Tilde);
        case '^': return makeToken(TokenType::Caret);
        
        // Operators that might be compound
        case '+':
            if (match('+')) return makeToken(TokenType::PlusPlus);
            if (match('=')) return makeToken(TokenType::PlusEqual);
            return makeToken(TokenType::Plus);
            
        case '-':
            if (match('-')) return makeToken(TokenType::MinusMinus);
            if (match('=')) return makeToken(TokenType::MinusEqual);
            if (match('>')) return makeToken(TokenType::Arrow);
            return makeToken(TokenType::Minus);
            
        case '*':
            if (match('=')) return makeToken(TokenType::StarEqual);
            return makeToken(TokenType::Star);
            
        case '/':
            if (match('/')) {
                // Single line comment
                skipComment();
                return nextToken();
            }
            if (match('*')) {
                // Multi-line comment
                while (!isAtEnd()) {
                    if (peek() == '*' && peekNext() == '/') {
                        advance(); // *
                        advance(); // /
                        break;
                    }
                    if (advance() == '\n') {
                        line_++;
                        column_ = 1;
                    }
                }
                return nextToken();
            }
            if (match('=')) return makeToken(TokenType::SlashEqual);
            return makeToken(TokenType::Slash);
            
        case '%':
            if (match('=')) return makeToken(TokenType::PercentEqual);
            return makeToken(TokenType::Percent);
            
        case '&':
            if (match('&')) return makeToken(TokenType::AmpersandAmpersand);
            return makeToken(TokenType::Ampersand);
            
        case '|':
            if (match('|')) return makeToken(TokenType::PipePipe);
            return makeToken(TokenType::Pipe);
            
        case '!':
            if (match('=')) return makeToken(TokenType::BangEqual);
            return makeToken(TokenType::Bang);
            
        case '=':
            if (match('=')) return makeToken(TokenType::EqualEqual);
            return makeToken(TokenType::Equal);
            
        case '<':
            if (match('=')) {
                if (match('>')) return makeToken(TokenType::Spaceship); // <=>
                return makeToken(TokenType::LessEqual); // <=
            }
            if (match('<')) return makeToken(TokenType::LeftShift); // <<
            return makeToken(TokenType::Less);
            
        case '>':
            if (match('=')) return makeToken(TokenType::GreaterEqual);
            if (match('>')) return makeToken(TokenType::RightShift); // >>
            return makeToken(TokenType::Greater);
            
        case '.':
            return makeToken(TokenType::Dot);
            
        case ':':
            if (match(':')) return makeToken(TokenType::ColonColon);
            return makeToken(TokenType::Colon);
    }
    
    return errorToken("Unexpected character");
}

Token Lexer::peekToken() {
    size_t savedCurrent = current_;
    size_t savedLine = line_;
    size_t savedColumn = column_;
    
    Token token = nextToken();
    
    current_ = savedCurrent;
    line_ = savedLine;
    column_ = savedColumn;
    
    return token;
}

char Lexer::advance() {
    column_++;
    return source_[current_++];
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[current_];
}

char Lexer::peekNext() const {
    if (current_ + 1 >= source_.length()) return '\0';
    return source_[current_ + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source_[current_] != expected) return false;
    advance();
    return true;
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
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

void Lexer::skipComment() {
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

Token Lexer::makeToken(TokenType type) {
    // For single-character tokens, include the character as lexeme
    std::string lexeme;
    
    switch (type) {
        case TokenType::LeftParen: lexeme = "("; break;
        case TokenType::RightParen: lexeme = ")"; break;
        case TokenType::LeftBrace: lexeme = "{"; break;
        case TokenType::RightBrace: lexeme = "}"; break;
        case TokenType::LeftBracket: lexeme = "["; break;
        case TokenType::RightBracket: lexeme = "]"; break;
        case TokenType::Less: lexeme = "<"; break;
        case TokenType::Greater: lexeme = ">"; break;
        case TokenType::Semicolon: lexeme = ";"; break;
        case TokenType::Comma: lexeme = ","; break;
        case TokenType::Dot: lexeme = "."; break;
        case TokenType::Question: lexeme = "?"; break;
        case TokenType::Colon: lexeme = ":"; break;
        case TokenType::Plus: lexeme = "+"; break;
        case TokenType::Minus: lexeme = "-"; break;
        case TokenType::Star: lexeme = "*"; break;
        case TokenType::Slash: lexeme = "/"; break;
        case TokenType::Percent: lexeme = "%"; break;
        case TokenType::Ampersand: lexeme = "&"; break;
        case TokenType::Pipe: lexeme = "|"; break;
        case TokenType::Caret: lexeme = "^"; break;
        case TokenType::Tilde: lexeme = "~"; break;
        case TokenType::Bang: lexeme = "!"; break;
        case TokenType::Equal: lexeme = "="; break;
        
        // Multi-character operators
        case TokenType::PlusPlus: lexeme = "++"; break;
        case TokenType::MinusMinus: lexeme = "--"; break;
        case TokenType::PlusEqual: lexeme = "+="; break;
        case TokenType::MinusEqual: lexeme = "-="; break;
        case TokenType::StarEqual: lexeme = "*="; break;
        case TokenType::SlashEqual: lexeme = "/="; break;
        case TokenType::PercentEqual: lexeme = "%="; break;
        case TokenType::Arrow: lexeme = "->"; break;
        case TokenType::ColonColon: lexeme = "::"; break;
        case TokenType::EqualEqual: lexeme = "=="; break;
        case TokenType::BangEqual: lexeme = "!="; break;
        case TokenType::LessEqual: lexeme = "<="; break;
        case TokenType::GreaterEqual: lexeme = ">="; break;
        case TokenType::AmpersandAmpersand: lexeme = "&&"; break;
        case TokenType::PipePipe: lexeme = "||"; break;
        case TokenType::Spaceship: lexeme = "<=>"; break;
        case TokenType::LeftShift: lexeme = "<<"; break;
        case TokenType::RightShift: lexeme = ">>"; break;
        
        default: 
            // For other tokens like EOF, just leave empty
            break;
    }
    
    return Token(type, lexeme, currentLocation());
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme) {
    return Token(type, lexeme, currentLocation());
}

Token Lexer::errorToken(const std::string& message) {
    Token token(TokenType::Error, message, currentLocation());
    return token;
}

Token Lexer::scanNumber() {
    size_t start = current_;
    size_t startColumn = column_;
    
    // Integer part
    while (isDigit(peek())) {
        advance();
    }
    
    bool isFloat = false;
    
    // Look for decimal part
    if (peek() == '.' && isDigit(peekNext())) {
        isFloat = true;
        advance(); // Consume '.'
        while (isDigit(peek())) {
            advance();
        }
    }
    
    // Look for exponent
    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        while (isDigit(peek())) {
            advance();
        }
    }
    
    std::string lexeme = source_.substr(start, current_ - start);
    SourceLocation loc = currentLocation();
    loc.column = startColumn;
    
    Token token(isFloat ? TokenType::FloatLiteral : TokenType::IntegerLiteral, lexeme, loc);
    
    if (isFloat) {
        token.floatValue = std::stod(lexeme);
    } else {
        // Handle different integer formats
        if (lexeme.size() > 2 && lexeme[0] == '0') {
            if (lexeme[1] == 'x' || lexeme[1] == 'X') {
                // Hexadecimal
                token.intValue = std::stoll(lexeme.substr(2), nullptr, 16);
            } else if (lexeme[1] == 'b' || lexeme[1] == 'B') {
                // Binary
                token.intValue = std::stoll(lexeme.substr(2), nullptr, 2);
            } else {
                // Octal
                token.intValue = std::stoll(lexeme, nullptr, 8);
            }
        } else {
            // Decimal
            token.intValue = std::stoll(lexeme);
        }
    }
    
    return token;
}

Token Lexer::scanString() {
    size_t startColumn = column_ - 1; // Account for opening quote
    size_t startLine = line_;
    std::string value;
    
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            return errorToken("Unterminated string literal");
        }
        
        if (peek() == '\\') {
            advance();
            if (isAtEnd()) {
                return errorToken("Unterminated string literal");
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
                    return errorToken("Invalid escape sequence");
            }
        } else {
            value += advance();
        }
    }
    
    if (isAtEnd()) {
        return errorToken("Unterminated string literal");
    }
    
    advance(); // Consume closing quote
    
    SourceLocation loc = currentLocation();
    loc.line = startLine;
    loc.column = startColumn;
    
    Token token(TokenType::StringLiteral, "\"" + value + "\"", loc);
    token.stringValue = value;
    return token;
}

Token Lexer::scanChar() {
    size_t startColumn = column_ - 1; // Account for opening quote
    
    if (isAtEnd()) {
        return errorToken("Unterminated character literal");
    }
    
    char value;
    if (peek() == '\\') {
        advance();
        if (isAtEnd()) {
            return errorToken("Unterminated character literal");
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
                return errorToken("Invalid escape sequence in character literal");
        }
    } else {
        value = advance();
    }
    
    if (isAtEnd() || peek() != '\'') {
        return errorToken("Unterminated character literal");
    }
    
    advance(); // Consume closing quote
    
    SourceLocation loc = currentLocation();
    loc.column = startColumn;
    
    Token token(TokenType::CharLiteral, std::string("'") + value + "'", loc);
    token.charValue = value;
    return token;
}

Token Lexer::scanIdentifier() {
    size_t start = current_;
    size_t startColumn = column_;
    
    while (isAlphaNumeric(peek()) || peek() == '_') {
        advance();
    }
    
    std::string lexeme = source_.substr(start, current_ - start);
    
    SourceLocation loc = currentLocation();
    loc.column = startColumn;
    
    // Check for 'super::' special case BEFORE keyword lookup
    if (lexeme == "super" && peek() == ':' && peekNext() == ':') {
        advance(); // :
        advance(); // :
        return Token(TokenType::Super, "super::", loc);
    }
    
    // Check if it's a keyword
    auto it = keywords_.find(lexeme);
    if (it != keywords_.end()) {
        Token token(it->second, lexeme, loc);
        
        // Handle boolean literals
        if (it->second == TokenType::True) {
            token.boolValue = true;
        } else if (it->second == TokenType::False) {
            token.boolValue = false;
        }
        
        return token;
    }
    
    return Token(TokenType::Identifier, lexeme, loc);
}

bool Lexer::isDigit(char c) const {
    return c >= '0' && c <= '9';
}

bool Lexer::isAlpha(char c) const {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

bool Lexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || isDigit(c);
}

SourceLocation Lexer::currentLocation() const {
    SourceLocation loc;
    loc.filename = filename_;
    loc.line = line_;
    loc.column = column_;
    return loc;
}

// Token method implementations
std::string Token::toString() const {
    std::stringstream ss;
    ss << location.toString() << " " << static_cast<int>(type) << " '" << lexeme << "'";
    return ss.str();
}

bool Token::isKeyword() const {
    return type >= TokenType::Bool && type <= TokenType::WeakPtr;
}

bool Token::isOperator() const {
    return type >= TokenType::Plus && type <= TokenType::Tilde;
}

bool Token::isLiteral() const {
    return type >= TokenType::IntegerLiteral && type <= TokenType::NullLiteral;
}

} // namespace JaiScript