#pragma once

#include <jaiscript/core/types.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace jai {

    // token types
    enum class token_type {
        // Literals
        integer_literal,
        float_literal,
        string_literal,
        char_literal,
        boolean_literal,
        null_literal,
        
        // Identifiers and Keywords
        identifier,
        user_template_type,  // User-defined template types (Point, SafeComponent, etc.) to disambiguate from < operator
        
        // Keywords
        bool_keyword, break_keyword, char_keyword, class_keyword, const_keyword, continue_keyword, else_keyword, false_keyword, float_keyword, for_keyword,
        function_keyword, if_keyword, int_keyword, map_keyword, array_keyword, namespace_keyword, null_keyword, override_keyword, private_keyword, public_keyword, return_keyword, static_keyword, string_keyword,
        this_keyword, true_keyword, void_keyword, while_keyword, auto_keyword, var_keyword, super_keyword, weak_ptr_keyword, shared_ptr_keyword,
        try_keyword, catch_keyword, throw_keyword,
        switch_keyword, case_keyword, default_keyword, fallthrough_keyword,
        include_keyword, import_keyword,
        coroutine_keyword, yield_keyword,
        enum_keyword,
        
        // Operators
        plus, minus, star, slash, percent,
        plus_equal, minus_equal, star_equal, slash_equal, percent_equal,
        equal, equal_equal, bang_equal,
        less, greater, less_equal, greater_equal,
        ampersand_ampersand, pipe_pipe, bang,
        plus_plus, minus_minus,
        dot, arrow,
        ampersand, pipe, caret,  // Bitwise operators &, |, ^
        colon_colon,
        question, question_dot, colon,
        tilde,  // For bitwise NOT and destructors: ~ClassName()
        spaceship,  // C++20 three-way comparison operator <=>
        left_shift, right_shift,  // Shift operators << and >>
        
        // Delimiters
        left_paren, right_paren,
        left_brace, right_brace,
        left_bracket, right_bracket,
        left_angle, right_angle,
        semicolon, comma,
        
        // Special
        eof,
        error
    };
    
    // Forward declaration
    class string_symbolizer;

    // token structure
    struct token {
        token_type type;
        std::string_view lexeme;  // Points to symbolizer storage (permanent) or static strings
        uint64_t symbol_id = 0;   // Pre-interned symbol ID for identifiers (0 = not interned)
        source_location location;

        // value for literals
        union {
            script_int int_value;
            script_float float_value;
            script_bool bool_value;
            script_char char_value;
        };
        std::string string_value;  // For string literals (can't be in union)

        token() : type(token_type::eof), lexeme(), location(), int_value(0) {}  // Default for storage

        token(token_type t, std::string_view lex, const source_location& loc)
            : type(t), lexeme(lex), location(loc), int_value(0) {}

        // Constructor with pre-interned symbol (for identifiers)
        token(token_type t, std::string_view lex, uint64_t sym_id, const source_location& loc)
            : type(t), lexeme(lex), symbol_id(sym_id), location(loc), int_value(0) {}

        std::string to_string() const;
        bool is_keyword() const;
        bool is_operator() const;
        bool is_literal() const;
    };
    
    // lexer class
    class lexer {
    public:
        // Constructors require symbolizer for proper identifier interning
        lexer(const std::string& source, string_symbolizer* symbolizer, const std::string& filename = "<script>");
        lexer(const std::string& source, string_symbolizer* symbolizer, const std::unordered_set<std::string>& registeredTypes, const std::string& filename = "<script>");

        // Get all tokens
        std::vector<token> tokenize();

        // Get next token
        token next_token();

        // Peek at next token without consuming
        token peek_token();

        // Check if at end
        bool is_at_end() const { return current_ >= source_.length(); }

    private:
        std::string source_;
        std::string filename_;
        string_symbolizer* symbolizer_;  // Required: for interning identifiers to permanent storage
        size_t current_ = 0;
        size_t line_ = 1;
        size_t column_ = 1;

        // Registered user-defined types (including template types)
        std::unordered_set<std::string> registered_types_;

        // Keyword map
        static const std::unordered_map<std::string, token_type> keywords_;

        // Helper methods
        char advance();
        char peek() const;
        char peek_next() const;
        bool match(char expected);
        void skip_whitespace();
        void skip_comment();

        // token creators
        token make_token(token_type type);
        token make_token(token_type type, std::string_view lexeme);
        token error_token(const std::string& message);
        
        // Scanners for different token types
        token scan_number();
        token scan_string();
        token scan_char();
        token scan_identifier();
        void scan_template_string();

        // Pending token queue for template string desugaring
        std::vector<token> pending_tokens_;
        size_t pending_index_ = 0;

        // Utilities
        bool is_digit(char c) const;
        bool is_alpha(char c) const;
        bool is_alpha_numeric(char c) const;
        source_location current_location() const;
    };
    
} // namespace jai