#pragma once

#include "../core/types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace JaiScript {

    // Token types
    enum class TokenType {
        // Literals
        IntegerLiteral,
        FloatLiteral,
        StringLiteral,
        CharLiteral,
        BooleanLiteral,
        NullLiteral,
        
        // Identifiers and Keywords
        Identifier,
        
        // Keywords
        Bool, Break, Char, Class, Const, Continue, Else, False, Float, For,
        Function, If, Int, Map, Array, New, Null, Private, Public, Return, String,
        This, True, Void, While, Auto, Var, Super, SharedPtr, WeakPtr,
        
        // Operators
        Plus, Minus, Star, Slash, Percent,
        PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
        Equal, EqualEqual, BangEqual,
        Less, Greater, LessEqual, GreaterEqual,
        AmpersandAmpersand, PipePipe, Bang,
        PlusPlus, MinusMinus,
        Dot, Arrow,
        Ampersand, Pipe, Caret,  // Bitwise operators &, |, ^
        ColonColon,
        Question, Colon,
        Tilde,  // For bitwise NOT and destructors: ~ClassName()
        Spaceship,  // C++20 three-way comparison operator <=>
        LeftShift, RightShift,  // Shift operators << and >>
        
        // Delimiters
        LeftParen, RightParen,
        LeftBrace, RightBrace,
        LeftBracket, RightBracket,
        LeftAngle, RightAngle,
        Semicolon, Comma,
        
        // Special
        Eof,
        Error
    };
    
    // Token structure
    struct Token {
        TokenType type;
        std::string lexeme;
        SourceLocation location;
        
        // Value for literals
        union {
            Int intValue;
            Float floatValue;
            Bool boolValue;
            Char charValue;
        };
        std::string stringValue;  // For string literals (can't be in union)
        
        Token(TokenType t, const std::string& lex, const SourceLocation& loc)
            : type(t), lexeme(lex), location(loc) {}
            
        std::string toString() const;
        bool isKeyword() const;
        bool isOperator() const;
        bool isLiteral() const;
    };
    
    // Lexer class
    class Lexer {
    public:
        Lexer(const std::string& source, const std::string& filename = "<script>");
        
        // Get all tokens
        std::vector<Token> tokenize();
        
        // Get next token
        Token nextToken();
        
        // Peek at next token without consuming
        Token peekToken();
        
        // Check if at end
        bool isAtEnd() const { return current_ >= source_.length(); }
        
    private:
        std::string source_;
        std::string filename_;
        size_t current_ = 0;
        size_t line_ = 1;
        size_t column_ = 1;
        
        // Keyword map
        static const std::unordered_map<std::string, TokenType> keywords_;
        
        // Helper methods
        char advance();
        char peek() const;
        char peekNext() const;
        bool match(char expected);
        void skipWhitespace();
        void skipComment();
        
        // Token creators
        Token makeToken(TokenType type);
        Token makeToken(TokenType type, const std::string& lexeme);
        Token errorToken(const std::string& message);
        
        // Scanners for different token types
        Token scanNumber();
        Token scanString();
        Token scanChar();
        Token scanIdentifier();
        
        // Utilities
        bool isDigit(char c) const;
        bool isAlpha(char c) const;
        bool isAlphaNumeric(char c) const;
        SourceLocation currentLocation() const;
    };
    
} // namespace JaiScript