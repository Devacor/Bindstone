#pragma once

#include "lexer.hpp"
#include "ast.hpp"
#include "../core/types.hpp"
#include <vector>
#include <memory>
#include <optional>

namespace JaiScript {

    class Parser {
    public:
        Parser(const std::vector<Token>& tokens, const std::string& filename = "<script>");
        
        // Parse the entire program
        std::vector<DeclarationPtr> parse();
        
        // Check if parsing had errors
        bool hasErrors() const { return !errors_.empty(); }
        const std::vector<std::string>& getErrors() const { return errors_; }
        
    private:
        const std::vector<Token>& tokens_;
        std::string filename_;
        size_t current_ = 0;
        std::vector<std::string> errors_;
        
        // Token buffer for handling >> splitting in generic contexts
        std::optional<Token> pushedBackToken_;
        
        // Error handling
        void error(const std::string& message, const Token& token);
        void synchronize();  // Error recovery
        
        // Token management
        Token peek() const;
        Token previous() const;
        Token advance();
        bool isAtEnd() const;
        bool check(TokenType type) const;
        bool match(TokenType type);
        bool match(std::initializer_list<TokenType> types);
        Token consume(TokenType type, const std::string& message);
        
        // Declaration parsing
        DeclarationPtr declaration();
        DeclarationPtr classDeclaration();
        DeclarationPtr functionDeclaration();
        DeclarationPtr variableDeclaration();
        
        // Statement parsing
        StatementPtr statement();
        StatementPtr expressionStatement();
        StatementPtr blockStatement();
        StatementPtr ifStatement();
        StatementPtr whileStatement();
        StatementPtr forStatement();
        StatementPtr returnStatement();
        StatementPtr breakStatement();
        StatementPtr continueStatement();
        
        // Expression parsing (precedence climbing)
        ExpressionPtr expression();
        ExpressionPtr assignment();
        ExpressionPtr ternary();
        ExpressionPtr logicalOr();
        ExpressionPtr logicalAnd();
        ExpressionPtr bitwiseOr();
        ExpressionPtr bitwiseXor();
        ExpressionPtr bitwiseAnd();
        ExpressionPtr equality();
        ExpressionPtr relational();
        ExpressionPtr shift();
        ExpressionPtr additive();
        ExpressionPtr multiplicative();
        ExpressionPtr unary();
        ExpressionPtr postfix();
        ExpressionPtr primary();
        
        // Helper parsers
        ExpressionPtr finishCall(ExpressionPtr callee);
        ExpressionPtr finishMemberAccess(ExpressionPtr object, bool isArrow);
        TypeInfoPtr parseType();
        std::vector<Parameter> parseParameterList();
        
        // Lambda parsing
        ExpressionPtr lambdaExpression();
        std::vector<LambdaExpr::Capture> parseCaptureList();
        
        // Helper for parsing function bodies
        DeclarationPtr parseFunctionBody(const std::string& name, TypeInfoPtr returnType);
        
        // Helper for parsing > in generic contexts (handles >> token splitting)
        void consumeGreaterInGeneric(const std::string& message);
    };

} // namespace JaiScript