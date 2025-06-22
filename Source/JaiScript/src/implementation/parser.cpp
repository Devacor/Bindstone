#include "../../include/jaiscript/detail/parser.hpp"
#include <sstream>
#include <iostream>
#include <optional>

namespace JaiScript {

Parser::Parser(const std::vector<Token>& tokens, const std::string& filename)
    : tokens_(tokens), filename_(filename), current_(0) {}

std::vector<DeclarationPtr> Parser::parse() {
    std::vector<DeclarationPtr> declarations;
    
    while (!isAtEnd()) {
        try {
            auto decl = declaration();
            if (decl) {
                declarations.push_back(decl);
            }
        } catch (const ParseError& e) {
            // Error already reported, synchronize and continue
            synchronize();
        }
    }
    
    return declarations;
}

// Error handling
void Parser::error(const std::string& message, const Token& token) {
    std::stringstream ss;
    ss << token.location.toString() << ": " << message;
    errors_.push_back(ss.str());
    throw ParseError(message, token.location);
}

void Parser::synchronize() {
    advance();
    
    while (!isAtEnd()) {
        if (previous().type == TokenType::Semicolon) return;
        
        switch (peek().type) {
            case TokenType::Class:
            case TokenType::Auto:
            case TokenType::Var:
            case TokenType::Int:
            case TokenType::Float:
            case TokenType::String:
            case TokenType::Bool:
            case TokenType::Char:
            case TokenType::Void:
            case TokenType::If:
            case TokenType::While:
            case TokenType::For:
            case TokenType::Return:
                return;
            default:
                advance();
        }
    }
}

// Token management
Token Parser::peek() const {
    if (pushedBackToken_.has_value()) {
        return pushedBackToken_.value();
    }
    return tokens_[current_];
}

Token Parser::previous() const {
    return tokens_[current_ - 1];
}

Token Parser::advance() {
    if (pushedBackToken_.has_value()) {
        Token token = pushedBackToken_.value();
        pushedBackToken_.reset();
        return token;
    }
    if (!isAtEnd()) current_++;
    return previous();
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::Eof;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(message, peek());
    return Token(TokenType::Error, "", peek().location); // Never reached
}

// Primary expressions
ExpressionPtr Parser::primary() {
    // Literals
    if (match(TokenType::True)) {
        Value val(true);
        return std::make_shared<LiteralExpr>(previous().location, val);
    }
    
    if (match(TokenType::False)) {
        Value val(false);
        return std::make_shared<LiteralExpr>(previous().location, val);
    }
    
    if (match(TokenType::Null)) {
        Value val;  // Default constructor creates null
        return std::make_shared<LiteralExpr>(previous().location, val);
    }
    
    if (match(TokenType::IntegerLiteral)) {
        Token token = previous();
        Value val(token.intValue);
        return std::make_shared<LiteralExpr>(token.location, val);
    }
    
    if (match(TokenType::FloatLiteral)) {
        Token token = previous();
        Value val(token.floatValue);
        return std::make_shared<LiteralExpr>(token.location, val);
    }
    
    if (match(TokenType::StringLiteral)) {
        Token token = previous();
        Value val(token.stringValue);
        return std::make_shared<LiteralExpr>(token.location, val);
    }
    
    if (match(TokenType::CharLiteral)) {
        Token token = previous();
        Value val(token.charValue);
        return std::make_shared<LiteralExpr>(token.location, val);
    }
    
    // Keywords
    if (match(TokenType::This)) {
        return std::make_shared<ThisExpr>(previous().location);
    }
    
    if (match(TokenType::Super)) {
        // super:: must be followed by a method name
        Token superToken = previous();
        Token methodName = consume(TokenType::Identifier, "Expected method name after 'super::'");
        
        // Create a member access on super
        auto superExpr = std::make_shared<SuperExpr>(superToken.location);
        return std::make_shared<MemberExpr>(methodName.location, superExpr, methodName.lexeme, false);
    }
    
    // Identifiers
    if (match(TokenType::Identifier)) {
        return std::make_shared<IdentifierExpr>(previous().location, previous().lexeme);
    }
    
    // Grouped expression
    if (match(TokenType::LeftParen)) {
        ExpressionPtr expr = expression();
        consume(TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }
    
    // Array literal with [] or Lambda expression
    if (match(TokenType::LeftBracket)) {
        auto startLoc = previous().location;
        
        // Check for empty array
        if (check(TokenType::RightBracket)) {
            consume(TokenType::RightBracket, "Expected ']'");
            return std::make_shared<ArrayLiteralExpr>(startLoc, std::vector<ExpressionPtr>());
        }
        
        // Save position for potential backtrack to lambda
        size_t savedPos = current_;
        
        // Try to parse as array literal first
        std::vector<ExpressionPtr> elements;
        bool isArray = true;
        
        // Parse first element/expression
        elements.push_back(expression());
        
        // If we see a comma, it's definitely an array
        if (match(TokenType::Comma)) {
            // Continue parsing array elements
            do {
                elements.push_back(expression());
            } while (match(TokenType::Comma));
            
            consume(TokenType::RightBracket, "Expected ']' after array elements");
            return std::make_shared<ArrayLiteralExpr>(startLoc, std::move(elements));
        }
        // If we see ], it's a single-element array
        else if (match(TokenType::RightBracket)) {
            return std::make_shared<ArrayLiteralExpr>(startLoc, std::move(elements));
        }
        // Otherwise, it might be a lambda capture list
        else {
            // Restore position and parse as lambda
            current_ = savedPos - 1;
            return lambdaExpression();
        }
    }
    
    // Map literal with {} (always a map)
    if (match(TokenType::LeftBrace)) {
        auto startLoc = previous().location;
        
        // Empty map
        if (check(TokenType::RightBrace)) {
            consume(TokenType::RightBrace, "Expected '}'");
            return std::make_shared<MapLiteralExpr>(startLoc, std::vector<std::pair<ExpressionPtr, ExpressionPtr>>());
        }
        
        // Parse map entries: {{"key", value}, {"key2", value2}}
        std::vector<std::pair<ExpressionPtr, ExpressionPtr>> entries;
        entries.reserve(8);
        
        do {
            consume(TokenType::LeftBrace, "Expected '{' for map entry");
            ExpressionPtr key = expression();
            consume(TokenType::Comma, "Expected ',' between key and value in map entry");
            ExpressionPtr value = expression();
            consume(TokenType::RightBrace, "Expected '}' after map entry");
            
            entries.emplace_back(std::move(key), std::move(value));
        } while (match(TokenType::Comma));
        
        consume(TokenType::RightBrace, "Expected '}' after map entries");
        return std::make_shared<MapLiteralExpr>(startLoc, std::move(entries));
    }
    
    // New expression
    if (match(TokenType::New)) {
        Token newToken = previous();
        TypeInfoPtr type = parseType();
        
        consume(TokenType::LeftParen, "Expected '(' after type in new expression");
        
        std::vector<ExpressionPtr> arguments;
        if (!check(TokenType::RightParen)) {
            // Reserve capacity for constructor arguments
            arguments.reserve(4);
            do {
                arguments.push_back(expression());
            } while (match(TokenType::Comma));
        }
        
        consume(TokenType::RightParen, "Expected ')' after arguments");
        
        return std::make_shared<NewExpr>(newToken.location, type, std::move(arguments));
    }
    
    error("Expected expression", peek());
    return nullptr; // Never reached
}

// Type parsing
TypeInfoPtr Parser::parseType() {
    // Handle auto/var/function
    if (match({TokenType::Auto, TokenType::Var, TokenType::Function})) {
        return nullptr; // Type inference or function keyword
    }
    
    // Primitive types
    if (match(TokenType::Int)) {
        return TypeInfo::makeInt();
    }
    if (match(TokenType::Float)) {
        return TypeInfo::makeFloat();
    }
    if (match(TokenType::String)) {
        return TypeInfo::makeString();
    }
    if (match(TokenType::Bool)) {
        return TypeInfo::makeBool();
    }
    if (match(TokenType::Char)) {
        return TypeInfo::makeChar();
    }
    if (match(TokenType::Void)) {
        auto info = std::make_shared<TypeInfo>(ValueType::Null);
        info->typeName = "void";
        return info;
    }
    
    // Generic types
    if (match(TokenType::Array)) {
        consume(TokenType::Less, "Expected '<' after 'array'");
        TypeInfoPtr elementType = parseType();
        consumeGreaterInGeneric("Expected '>' after array element type");
        return TypeInfo::makeArray(elementType);
    }
    
    if (match(TokenType::Map)) {
        consume(TokenType::Less, "Expected '<' after 'map'");
        TypeInfoPtr keyType = parseType();
        consume(TokenType::Comma, "Expected ',' after map key type");
        TypeInfoPtr valueType = parseType();
        consumeGreaterInGeneric("Expected '>' after map value type");
        return TypeInfo::makeMap(keyType, valueType);
    }
    
    if (match(TokenType::SharedPtr)) {
        consume(TokenType::Less, "Expected '<' after 'SharedPtr'");
        TypeInfoPtr pointeeType = parseType();
        consumeGreaterInGeneric("Expected '>' after SharedPtr type");
        return TypeInfo::makeSharedPtr(pointeeType);
    }
    
    if (match(TokenType::WeakPtr)) {
        consume(TokenType::Less, "Expected '<' after 'WeakPtr'");
        TypeInfoPtr pointeeType = parseType();
        consumeGreaterInGeneric("Expected '>' after WeakPtr type");
        return TypeInfo::makeWeakPtr(pointeeType);
    }
    
    // User-defined type
    if (match(TokenType::Identifier)) {
        return TypeInfo::makeObject(previous().lexeme);
    }
    
    error("Expected type", peek());
    return nullptr;
}

// Simple expression for now
ExpressionPtr Parser::expression() {
    return assignment();
}

ExpressionPtr Parser::assignment() {
    ExpressionPtr expr = ternary();
    
    if (match({TokenType::Equal, TokenType::PlusEqual, TokenType::MinusEqual,
               TokenType::StarEqual, TokenType::SlashEqual, TokenType::PercentEqual})) {
        Token op = previous();
        ExpressionPtr right = assignment();
        return std::make_shared<AssignmentExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::ternary() {
    ExpressionPtr expr = logicalOr();
    
    if (match(TokenType::Question)) {
        Token questionLoc = previous();
        ExpressionPtr thenExpr = expression();
        consume(TokenType::Colon, "Expected ':' after then expression in ternary");
        ExpressionPtr elseExpr = ternary();
        return std::make_shared<TernaryExpr>(questionLoc.location, expr, thenExpr, elseExpr);
    }
    
    return expr;
}

// Expression precedence chain implementations
ExpressionPtr Parser::logicalOr() {
    ExpressionPtr expr = logicalAnd();
    
    while (match(TokenType::PipePipe)) {
        Token op = previous();
        ExpressionPtr right = logicalAnd();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::logicalAnd() {
    ExpressionPtr expr = bitwiseOr();
    
    while (match(TokenType::AmpersandAmpersand)) {
        Token op = previous();
        ExpressionPtr right = bitwiseOr();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::bitwiseOr() {
    ExpressionPtr expr = bitwiseXor();
    
    while (match(TokenType::Pipe)) {
        Token op = previous();
        ExpressionPtr right = bitwiseXor();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::bitwiseXor() {
    ExpressionPtr expr = bitwiseAnd();
    
    while (match(TokenType::Caret)) {
        Token op = previous();
        ExpressionPtr right = bitwiseAnd();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::bitwiseAnd() {
    ExpressionPtr expr = equality();
    
    while (match(TokenType::Ampersand)) {
        Token op = previous();
        ExpressionPtr right = equality();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::equality() {
    ExpressionPtr expr = relational();
    
    while (match({TokenType::EqualEqual, TokenType::BangEqual})) {
        Token op = previous();
        ExpressionPtr right = relational();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::relational() {
    ExpressionPtr expr = shift();
    
    while (match({TokenType::Less, TokenType::LessEqual, 
                   TokenType::Greater, TokenType::GreaterEqual, TokenType::Spaceship})) {
        Token op = previous();
        ExpressionPtr right = shift();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::shift() {
    ExpressionPtr expr = additive();
    
    while (match({TokenType::LeftShift, TokenType::RightShift})) {
        Token op = previous();
        ExpressionPtr right = additive();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::additive() {
    ExpressionPtr expr = multiplicative();
    
    while (match({TokenType::Plus, TokenType::Minus})) {
        Token op = previous();
        ExpressionPtr right = multiplicative();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::multiplicative() {
    ExpressionPtr expr = unary();
    
    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        Token op = previous();
        ExpressionPtr right = unary();
        expr = std::make_shared<BinaryExpr>(op.location, expr, op, right);
    }
    
    return expr;
}

ExpressionPtr Parser::unary() {
    if (match({TokenType::Bang, TokenType::Minus, TokenType::PlusPlus, 
               TokenType::MinusMinus, TokenType::Ampersand, TokenType::Tilde})) {
        Token op = previous();
        ExpressionPtr right = unary();
        return std::make_shared<UnaryExpr>(op.location, op, right);
    }
    
    return postfix();
}

ExpressionPtr Parser::postfix() {
    ExpressionPtr expr = primary();
    
    while (true) {
        if (match(TokenType::LeftParen)) {
            expr = finishCall(expr);
        } else if (match(TokenType::LeftBrace)) {
            // Brace initialization: Type{args...}
            // This should only be valid if expr is an identifier (type name)
            if (auto* identExpr = dynamic_cast<IdentifierExpr*>(expr.get())) {
                std::vector<ExpressionPtr> arguments;
                
                if (!check(TokenType::RightBrace)) {
                    // Reserve capacity for constructor arguments
                    arguments.reserve(4);
                    do {
                        arguments.push_back(expression());
                    } while (match(TokenType::Comma));
                }
                
                consume(TokenType::RightBrace, "Expected '}' after constructor arguments");
                
                // Create a NewExpr for object construction
                // The type name is in the identifier
                auto typeInfo = std::make_shared<TypeInfo>(ValueType::Object, identExpr->name);
                expr = std::make_shared<NewExpr>(identExpr->location, typeInfo, std::move(arguments));
            } else {
                error("Brace initialization can only be used with type names", previous());
            }
        } else if (match(TokenType::Dot)) {
            expr = finishMemberAccess(expr, false);
        } else if (match(TokenType::Arrow)) {
            expr = finishMemberAccess(expr, true);
        } else if (match(TokenType::LeftBracket)) {
            // Array subscript
            ExpressionPtr index = expression();
            consume(TokenType::RightBracket, "Expected ']' after array index");
            Token op(TokenType::LeftBracket, "[", previous().location);
            expr = std::make_shared<BinaryExpr>(op.location, expr, op, index);
        } else if (match({TokenType::PlusPlus, TokenType::MinusMinus})) {
            // Postfix increment/decrement
            Token op = previous();
            expr = std::make_shared<UnaryExpr>(op.location, op, expr, true);  // true = postfix
        } else {
            break;
        }
    }
    
    return expr;
}

// Stub for declaration
DeclarationPtr Parser::declaration() {
    if (match(TokenType::Class)) return classDeclaration();
    
    // Check for explicit type keywords that start declarations
    if (match({TokenType::Auto, TokenType::Var, TokenType::Function, TokenType::Int, TokenType::Float, 
               TokenType::String, TokenType::Bool, TokenType::Char, TokenType::Void,
               TokenType::Array, TokenType::Map, TokenType::SharedPtr, TokenType::WeakPtr})) {
        // We already consumed the type keyword, so we need to backtrack
        current_--;
        
        // Look ahead to determine if this is a function or variable declaration
        size_t savedPos = current_;
        parseType(); // consume the type
        
        // Skip optional & for reference types
        if (check(TokenType::Ampersand)) {
            advance();
        }
        
        if (check(TokenType::Identifier)) {
            advance(); // consume identifier
            if (check(TokenType::LeftParen)) {
                // This is a function declaration
                current_ = savedPos;
                return functionDeclaration();
            }
        }
        
        // Otherwise it's a variable declaration
        current_ = savedPos;
        return variableDeclaration();
    }
    
    // Check if this might be a type name followed by an identifier (for custom types)
    // We need to look ahead to see if this is a declaration
    if (check(TokenType::Identifier)) {
        // Save current position
        size_t savedPos = current_;
        
        // Try to parse as type
        advance(); // consume potential type name
        
        // Check for template arguments
        if (check(TokenType::LeftAngle)) {
            // This could be a templated type like array<int>
            // For now, restore position and parse as declaration
            current_ = savedPos;
            return variableDeclaration();
        }
        
        // Check if next token is an identifier (variable name) or function-like
        if (check(TokenType::Identifier)) {
            // This looks like a declaration: TypeName varName
            current_ = savedPos;
            return variableDeclaration();
        }
        
        // Otherwise, restore position and parse as expression
        current_ = savedPos;
    }
    
    // Check for statements that can appear at top level
    // In a scripting language, any statement can appear at top level
    if (check(TokenType::If) || check(TokenType::While) || check(TokenType::For) ||
        check(TokenType::Return) || check(TokenType::Break) || check(TokenType::Continue) ||
        check(TokenType::LeftBrace)) {
        // We need to wrap the statement in a declaration since parse() returns declarations
        // For now, we'll create a special statement declaration
        auto stmt = statement();
        // Create a StatementDecl to wrap statements at the top level
        return std::make_shared<StatementDecl>(stmt->location, stmt);
    }
    
    // Otherwise it's a top-level expression statement
    auto expr = expression();
    
    // Allow semicolon to be optional at end of file for single expressions
    if (!isAtEnd()) {
        consume(TokenType::Semicolon, "Expected ';' after expression");
    }
    
    // Create an ExpressionDecl for top-level expressions
    return std::make_shared<ExpressionDecl>(expr->location, expr);
}

// Helper method implementations
ExpressionPtr Parser::finishCall(ExpressionPtr callee) {
    std::vector<ExpressionPtr> arguments;
    
    if (!check(TokenType::RightParen)) {
        // Reserve capacity for common case of 2-4 arguments
        arguments.reserve(4);
        do {
            arguments.push_back(expression());
        } while (match(TokenType::Comma));
    }
    
    Token paren = consume(TokenType::RightParen, "Expected ')' after arguments");
    
    return std::make_shared<CallExpr>(paren.location, callee, std::move(arguments));
}

ExpressionPtr Parser::finishMemberAccess(ExpressionPtr object, bool isArrow) {
    Token name = consume(TokenType::Identifier, "Expected member name");
    return std::make_shared<MemberExpr>(name.location, object, name.lexeme, isArrow);
}

std::vector<Parameter> Parser::parseParameterList() {
    std::vector<Parameter> params;
    
    if (!check(TokenType::RightParen)) {
        do {
            // Check for const
            bool isConst = match(TokenType::Const);
            
            TypeInfoPtr type = nullptr;
            std::string name;
            bool isReference = false;
            
            // Check for :name syntax (shorthand for auto: name)
            if (match(TokenType::Colon)) {
                // :name means auto type
                type = nullptr; // nullptr means auto
                name = consume(TokenType::Identifier, "Expected parameter name after ':'").lexeme;
            }
            // Check for type: name syntax
            else if (check(TokenType::Identifier) || check(TokenType::Auto) || check(TokenType::Var) ||
                     check(TokenType::Function) || check(TokenType::Int) || check(TokenType::Float) || 
                     check(TokenType::String) || check(TokenType::Bool) || check(TokenType::Char) || 
                     check(TokenType::Void) || check(TokenType::Array) || check(TokenType::Map) ||
                     check(TokenType::SharedPtr) || check(TokenType::WeakPtr)) {
                
                type = parseType();
                
                if (match(TokenType::Colon)) {
                    // type: name syntax
                    name = consume(TokenType::Identifier, "Expected parameter name after ':'").lexeme;
                } else {
                    // Traditional type name syntax
                    // Check for reference
                    isReference = match(TokenType::Ampersand);
                    name = consume(TokenType::Identifier, "Expected parameter name").lexeme;
                }
            }
            // No type specified, error
            else {
                error("Expected parameter type or ':' for auto parameter", peek());
            }
            
            params.push_back(Parameter(type, name, isReference, isConst));
        } while (match(TokenType::Comma));
    }
    
    return params;
}

// Lambda expression parsing
ExpressionPtr Parser::lambdaExpression() {
    auto lambda = std::make_shared<LambdaExpr>(peek().location);
    
    // Parse capture list
    consume(TokenType::LeftBracket, "Expected '[' for lambda");
    lambda->captures = parseCaptureList();
    consume(TokenType::RightBracket, "Expected ']' after capture list");
    
    // Parse parameters
    consume(TokenType::LeftParen, "Expected '(' for lambda parameters");
    lambda->parameters = parseParameterList();
    consume(TokenType::RightParen, "Expected ')' after parameters");
    
    // Parse return type if specified
    if (match(TokenType::Arrow)) {
        // Check if return type is specified or if we go directly to {
        if (check(TokenType::LeftBrace)) {
            // -> { means auto return type
            lambda->returnType = nullptr; // nullptr means auto
        } else {
            lambda->returnType = parseType();
        }
    }
    
    // Parse body
    consume(TokenType::LeftBrace, "Expected '{' for lambda body");
    lambda->body = std::dynamic_pointer_cast<BlockStmt>(blockStatement());
    
    return lambda;
}

std::vector<LambdaExpr::Capture> Parser::parseCaptureList() {
    std::vector<LambdaExpr::Capture> captures;
    
    if (!check(TokenType::RightBracket)) {
        do {
            bool byRef = match(TokenType::Ampersand);
            std::string name = consume(TokenType::Identifier, "Expected capture variable name").lexeme;
            captures.push_back({name, byRef});
        } while (match(TokenType::Comma));
    }
    
    return captures;
}

// Statement parsing implementations
StatementPtr Parser::statement() {
    if (match(TokenType::LeftBrace)) return blockStatement();
    if (match(TokenType::If)) return ifStatement();
    if (match(TokenType::While)) return whileStatement();
    if (match(TokenType::For)) return forStatement();
    if (match(TokenType::Return)) return returnStatement();
    if (match(TokenType::Break)) return breakStatement();
    if (match(TokenType::Continue)) return continueStatement();
    
    return expressionStatement();
}

StatementPtr Parser::blockStatement() {
    Token leftBrace = previous();
    std::vector<DeclarationPtr> declarations;
    
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        declarations.push_back(declaration());
    }
    
    consume(TokenType::RightBrace, "Expected '}' after block");
    
    return std::make_shared<BlockStmt>(leftBrace.location, std::move(declarations));
}

StatementPtr Parser::expressionStatement() {
    ExpressionPtr expr = expression();
    consume(TokenType::Semicolon, "Expected ';' after expression");
    return std::make_shared<ExpressionStmt>(expr->location, expr);
}

StatementPtr Parser::ifStatement() {
    Token ifToken = previous();
    
    consume(TokenType::LeftParen, "Expected '(' after 'if'");
    ExpressionPtr condition = expression();
    consume(TokenType::RightParen, "Expected ')' after if condition");
    
    StatementPtr thenStmt = statement();
    StatementPtr elseStmt = nullptr;
    
    if (match(TokenType::Else)) {
        elseStmt = statement();
    }
    
    return std::make_shared<IfStmt>(ifToken.location, condition, thenStmt, elseStmt);
}

StatementPtr Parser::whileStatement() {
    Token whileToken = previous();
    
    consume(TokenType::LeftParen, "Expected '(' after 'while'");
    ExpressionPtr condition = expression();
    consume(TokenType::RightParen, "Expected ')' after while condition");
    
    StatementPtr body = statement();
    
    return std::make_shared<WhileStmt>(whileToken.location, condition, body);
}

StatementPtr Parser::forStatement() {
    Token forToken = previous();
    
    consume(TokenType::LeftParen, "Expected '(' after 'for'");
    
    // Save position for potential backtracking
    size_t savedPosition = current_;
    
    // Try to parse as range-based for loop first
    // This will be: [const] type [&] identifier : expression
    bool isConst = match(TokenType::Const);
    bool isReference = false;
    TypeInfoPtr elementType = nullptr;
    
    // Check if we have a type followed by optional & and identifier : pattern
    if (check(TokenType::Auto) || check(TokenType::Var) || check(TokenType::Int) || 
        check(TokenType::Float) || check(TokenType::String) || check(TokenType::Bool) || 
        check(TokenType::Char) || check(TokenType::Identifier)) {
        
        // Parse the type
        elementType = parseType();
        
        // Check for reference after type
        if (match(TokenType::Ampersand)) {
            isReference = true;
        }
        
        // Must have an identifier
        if (check(TokenType::Identifier)) {
            Token varName = advance();
            
            // Check for colon - this indicates range-based for
            if (match(TokenType::Colon)) {
                // This is a range-based for loop!
                ExpressionPtr container = expression();
                consume(TokenType::RightParen, "Expected ')' after range expression");
                StatementPtr body = statement();
                
                return std::make_shared<RangeForStmt>(
                    forToken.location, elementType, varName.lexeme, 
                    isReference, isConst, container, body
                );
            }
        }
    }
    
    // Not a range-based for loop, restore position and parse as traditional for
    current_ = savedPosition;
    
    // Init
    DeclarationPtr init = nullptr;
    if (match(TokenType::Semicolon)) {
        // No init
    } else if (check(TokenType::Auto) || check(TokenType::Var) || 
               check(TokenType::Int) || check(TokenType::Float) || 
               check(TokenType::String) || check(TokenType::Bool) || 
               check(TokenType::Char) || check(TokenType::Array) ||
               check(TokenType::Map) || check(TokenType::Identifier)) {
        init = variableDeclaration();
    } else {
        // Expression init - wrap in a variable declaration without a type
        ExpressionPtr expr = expression();
        consume(TokenType::Semicolon, "Expected ';' after for loop initializer");
        // For now, we'll skip expression-only init since it needs to be a Declaration
        // This is a limitation we can address later with a more flexible AST
    }
    
    // Condition
    ExpressionPtr condition = nullptr;
    if (!check(TokenType::Semicolon)) {
        condition = expression();
    }
    consume(TokenType::Semicolon, "Expected ';' after for loop condition");
    
    // Update
    ExpressionPtr update = nullptr;
    if (!check(TokenType::RightParen)) {
        update = expression();
    }
    consume(TokenType::RightParen, "Expected ')' after for loop clauses");
    
    StatementPtr body = statement();
    
    return std::make_shared<ForStmt>(forToken.location, init, condition, update, body);
}

StatementPtr Parser::returnStatement() {
    Token returnToken = previous();
    
    ExpressionPtr value = nullptr;
    if (!check(TokenType::Semicolon)) {
        value = expression();
    }
    
    consume(TokenType::Semicolon, "Expected ';' after return value");
    
    return std::make_shared<ReturnStmt>(returnToken.location, value);
}

StatementPtr Parser::breakStatement() {
    Token breakToken = previous();
    consume(TokenType::Semicolon, "Expected ';' after 'break'");
    return std::make_shared<BreakStmt>(breakToken.location);
}

StatementPtr Parser::continueStatement() {
    Token continueToken = previous();
    consume(TokenType::Semicolon, "Expected ';' after 'continue'");
    return std::make_shared<ContinueStmt>(continueToken.location);
}

// Declaration parsing implementations
DeclarationPtr Parser::classDeclaration() {
    Token className = consume(TokenType::Identifier, "Expected class name");
    
    std::vector<std::string> baseClasses;
    if (match(TokenType::Colon)) {
        do {
            baseClasses.push_back(consume(TokenType::Identifier, "Expected base class name").lexeme);
        } while (match(TokenType::Comma));
    }
    
    consume(TokenType::LeftBrace, "Expected '{' before class body");
    
    auto classDecl = std::make_shared<ClassDecl>(className.location, className.lexeme);
    classDecl->baseClasses = std::move(baseClasses);
    
    // Parse class members
    ClassDecl::MemberVisibility visibility = ClassDecl::Public;
    
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        // Check for visibility specifiers
        if (match(TokenType::Public)) {
            consume(TokenType::Colon, "Expected ':' after 'public'");
            visibility = ClassDecl::Public;
            continue;
        }
        if (match(TokenType::Private)) {
            consume(TokenType::Colon, "Expected ':' after 'private'");
            visibility = ClassDecl::Private;
            continue;
        }
        
        // Parse member declaration
        DeclarationPtr member;
        
        // Constructor/Destructor check
        if (check(TokenType::Identifier) && peek().lexeme == className.lexeme) {
            advance(); // consume class name
            member = parseFunctionBody(className.lexeme, nullptr); // Constructor
        } else if (match(TokenType::Tilde)) {
            consume(TokenType::Identifier, "Expected class name after '~'");
            member = parseFunctionBody("~" + className.lexeme, nullptr); // Destructor
        } else {
            // Regular member (variable or function)
            TypeInfoPtr type = parseType();
            
            if (check(TokenType::Identifier)) {
                Token name = advance();
                
                if (match(TokenType::LeftParen)) {
                    // Function
                    current_--; // Back up to reparse
                    member = parseFunctionBody(name.lexeme, type);
                } else {
                    // Variable
                    ExpressionPtr init = nullptr;
                    if (match(TokenType::Equal)) {
                        init = expression();
                    }
                    consume(TokenType::Semicolon, "Expected ';' after field declaration");
                    member = std::make_shared<VariableDecl>(name.location, type, name.lexeme, init);
                }
            } else {
                error("Expected member name", peek());
            }
        }
        
        classDecl->members.push_back({visibility, member});
    }
    
    consume(TokenType::RightBrace, "Expected '}' after class body");
    
    return classDecl;
}

DeclarationPtr Parser::functionDeclaration() {
    TypeInfoPtr returnType = parseType();
    
    // Check for reference return type
    if (match(TokenType::Ampersand)) {
        // Create a reference type
        auto refType = std::make_shared<TypeInfo>(ValueType::Reference);
        refType->typeName = returnType ? (returnType->typeName + "&") : "auto&";
        refType->typeParams.push_back(returnType);
        returnType = refType;
    }
    
    Token name = consume(TokenType::Identifier, "Expected function name");
    return parseFunctionBody(name.lexeme, returnType);
}

DeclarationPtr Parser::parseFunctionBody(const std::string& name, TypeInfoPtr returnType) {
    auto func = std::make_shared<FunctionDecl>(previous().location, name);
    
    consume(TokenType::LeftParen, "Expected '(' after function name");
    func->parameters = parseParameterList();
    consume(TokenType::RightParen, "Expected ')' after parameters");
    
    // Handle trailing return type
    if (match(TokenType::Arrow)) {
        // Check if return type is specified or if we go directly to {
        if (check(TokenType::LeftBrace)) {
            // -> { means auto return type
            func->returnType = nullptr; // nullptr means auto
        } else {
            func->returnType = parseType();
        }
    } else {
        func->returnType = returnType ? returnType : TypeInfo::makeVoid();
    }
    
    consume(TokenType::LeftBrace, "Expected '{' before function body");
    func->body = std::dynamic_pointer_cast<BlockStmt>(blockStatement());
    
    return func;
}

DeclarationPtr Parser::variableDeclaration() {
    TypeInfoPtr type = parseType();
    
    // Check for reference after type (e.g., int& x or auto& x)
    bool isReference = false;
    if (match(TokenType::Ampersand)) {
        isReference = true;
        // Create a reference type
        auto refType = std::make_shared<TypeInfo>(ValueType::Reference);
        refType->typeName = type ? (type->typeName + "&") : "auto&";
        refType->typeParams.push_back(type);
        type = refType;
    }
    
    Token name = consume(TokenType::Identifier, "Expected variable name");
    
    ExpressionPtr initializer = nullptr;
    if (match(TokenType::Equal)) {
        initializer = expression();
    }
    
    consume(TokenType::Semicolon, "Expected ';' after variable declaration");
    
    auto decl = std::make_shared<VariableDecl>(name.location, type, name.lexeme, initializer);
    return decl;
}

void Parser::consumeGreaterInGeneric(const std::string& message) {
    // Handle the case where >> is tokenized as RightShift when we need >
    if (check(TokenType::RightShift)) {
        // We have >>, but we only want to consume one >
        Token rightShift = advance();
        
        // Push back a synthetic > token for the second >
        // This allows the parser to continue as if there were two separate > tokens
        Token syntheticGreater(TokenType::Greater, ">", rightShift.location);
        pushedBackToken_ = syntheticGreater;
        
        // We've consumed one > (implicitly), the other is pushed back
        return;
    }
    
    // Normal case: just consume a > token
    consume(TokenType::Greater, message);
}

} // namespace JaiScript