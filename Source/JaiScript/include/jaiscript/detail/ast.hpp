#pragma once

#include "../core/types.hpp"
#include "../core/type_info.hpp"
#include "../core/value.hpp"
#include "lexer.hpp"
#include <memory>
#include <vector>
#include <string>

namespace JaiScript {

    // Forward declarations
    class ASTNode;
    class Expression;
    class Statement;
    class Declaration;
    
    using ASTNodePtr = std::shared_ptr<ASTNode>;
    using ExpressionPtr = std::shared_ptr<Expression>;
    using StatementPtr = std::shared_ptr<Statement>;
    using DeclarationPtr = std::shared_ptr<Declaration>;
    
    // Parameter information for functions and lambdas
    struct Parameter {
        TypeInfoPtr type;
        std::string name;
        bool isReference = false;
        bool isConst = false;
        
        Parameter(TypeInfoPtr t, const std::string& n, bool ref = false, bool c = false)
            : type(t), name(n), isReference(ref), isConst(c) {}
    };
    
    // Visitor pattern for AST traversal
    class ASTVisitor {
    public:
        virtual ~ASTVisitor() = default;
        // Expression visitors
        virtual void visitLiteralExpr(class LiteralExpr* expr) = 0;
        virtual void visitIdentifierExpr(class IdentifierExpr* expr) = 0;
        virtual void visitBinaryExpr(class BinaryExpr* expr) = 0;
        virtual void visitUnaryExpr(class UnaryExpr* expr) = 0;
        virtual void visitAssignmentExpr(class AssignmentExpr* expr) = 0;
        virtual void visitCallExpr(class CallExpr* expr) = 0;
        virtual void visitMemberExpr(class MemberExpr* expr) = 0;
        virtual void visitLambdaExpr(class LambdaExpr* expr) = 0;
        virtual void visitNewExpr(class NewExpr* expr) = 0;
        virtual void visitTernaryExpr(class TernaryExpr* expr) = 0;
        virtual void visitArrayLiteralExpr(class ArrayLiteralExpr* expr) = 0;
        virtual void visitMapLiteralExpr(class MapLiteralExpr* expr) = 0;
        virtual void visitThisExpr(class ThisExpr* expr) = 0;
        virtual void visitSuperExpr(class SuperExpr* expr) = 0;
        
        // Statement visitors
        virtual void visitExpressionStmt(class ExpressionStmt* stmt) = 0;
        virtual void visitBlockStmt(class BlockStmt* stmt) = 0;
        virtual void visitIfStmt(class IfStmt* stmt) = 0;
        virtual void visitWhileStmt(class WhileStmt* stmt) = 0;
        virtual void visitForStmt(class ForStmt* stmt) = 0;
        virtual void visitRangeForStmt(class RangeForStmt* stmt) = 0;
        virtual void visitReturnStmt(class ReturnStmt* stmt) = 0;
        virtual void visitBreakStmt(class BreakStmt* stmt) = 0;
        virtual void visitContinueStmt(class ContinueStmt* stmt) = 0;
        
        // Declaration visitors
        virtual void visitVariableDecl(class VariableDecl* decl) = 0;
        virtual void visitFunctionDecl(class FunctionDecl* decl) = 0;
        virtual void visitClassDecl(class ClassDecl* decl) = 0;
        virtual void visitExpressionDecl(class ExpressionDecl* decl) = 0;
    };
    
    // Base AST node
    class ASTNode {
    public:
        SourceLocation location;
        
        ASTNode(const SourceLocation& loc) : location(loc) {}
        virtual ~ASTNode() = default;
        virtual void accept(ASTVisitor* visitor) = 0;
    };
    
    // Base expression node
    class Expression : public ASTNode {
    public:
        TypeInfoPtr resultType;  // Type of the expression result
        
        Expression(const SourceLocation& loc) : ASTNode(loc) {}
    };
    
    // Literal expression
    class LiteralExpr : public Expression {
    public:
        Value value;
        
        LiteralExpr(const SourceLocation& loc, const Value& val) 
            : Expression(loc), value(val) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitLiteralExpr(this);
        }
    };
    
    // Identifier expression
    class IdentifierExpr : public Expression {
    public:
        std::string name;
        
        IdentifierExpr(const SourceLocation& loc, const std::string& n)
            : Expression(loc), name(n) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitIdentifierExpr(this);
        }
    };
    
    // Binary expression
    class BinaryExpr : public Expression {
    public:
        ExpressionPtr left;
        Token op;
        ExpressionPtr right;
        
        BinaryExpr(const SourceLocation& loc, ExpressionPtr l, const Token& o, ExpressionPtr r)
            : Expression(loc), left(l), op(o), right(r) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitBinaryExpr(this);
        }
    };
    
    // Unary expression
    class UnaryExpr : public Expression {
    public:
        Token op;
        ExpressionPtr operand;
        bool isPostfix;
        
        UnaryExpr(const SourceLocation& loc, const Token& o, ExpressionPtr expr, bool postfix = false)
            : Expression(loc), op(o), operand(expr), isPostfix(postfix) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitUnaryExpr(this);
        }
    };
    
    // Assignment expression
    class AssignmentExpr : public Expression {
    public:
        ExpressionPtr target;
        Token op;
        ExpressionPtr value;
        
        AssignmentExpr(const SourceLocation& loc, ExpressionPtr t, const Token& o, ExpressionPtr v)
            : Expression(loc), target(t), op(o), value(v) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitAssignmentExpr(this);
        }
    };
    
    // Function call expression
    class CallExpr : public Expression {
    public:
        ExpressionPtr callee;
        std::vector<ExpressionPtr> arguments;
        
        CallExpr(const SourceLocation& loc, ExpressionPtr c, std::vector<ExpressionPtr> args)
            : Expression(loc), callee(c), arguments(std::move(args)) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitCallExpr(this);
        }
    };
    
    // Member access expression
    class MemberExpr : public Expression {
    public:
        ExpressionPtr object;
        std::string member;
        bool isArrow;  // true for ->, false for .
        
        MemberExpr(const SourceLocation& loc, ExpressionPtr obj, const std::string& mem, bool arrow)
            : Expression(loc), object(obj), member(mem), isArrow(arrow) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitMemberExpr(this);
        }
    };
    
    // Lambda expression
    class LambdaExpr : public Expression {
    public:
        struct Capture {
            std::string name;
            bool byReference;
        };
        
        std::vector<Capture> captures;
        std::vector<Parameter> parameters;
        TypeInfoPtr returnType;
        StatementPtr body;
        
        LambdaExpr(const SourceLocation& loc) : Expression(loc) {}
        
        void accept(ASTVisitor* visitor) override {
            visitor->visitLambdaExpr(this);
        }
    };
    
    // New expression
    class NewExpr : public Expression {
    public:
        TypeInfoPtr type;
        std::vector<ExpressionPtr> arguments;
        
        NewExpr(const SourceLocation& loc, TypeInfoPtr t, std::vector<ExpressionPtr> args)
            : Expression(loc), type(t), arguments(std::move(args)) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitNewExpr(this);
        }
    };
    
    // Ternary expression
    class TernaryExpr : public Expression {
    public:
        ExpressionPtr condition;
        ExpressionPtr thenExpr;
        ExpressionPtr elseExpr;
        
        TernaryExpr(const SourceLocation& loc, ExpressionPtr c, ExpressionPtr t, ExpressionPtr e)
            : Expression(loc), condition(c), thenExpr(t), elseExpr(e) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitTernaryExpr(this);
        }
    };
    
    // Array literal expression
    class ArrayLiteralExpr : public Expression {
    public:
        std::vector<ExpressionPtr> elements;
        
        ArrayLiteralExpr(const SourceLocation& loc, std::vector<ExpressionPtr> elems)
            : Expression(loc), elements(std::move(elems)) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitArrayLiteralExpr(this);
        }
    };
    
    // Map literal expression
    class MapLiteralExpr : public Expression {
    public:
        std::vector<std::pair<ExpressionPtr, ExpressionPtr>> entries;
        
        MapLiteralExpr(const SourceLocation& loc, std::vector<std::pair<ExpressionPtr, ExpressionPtr>> e)
            : Expression(loc), entries(std::move(e)) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitMapLiteralExpr(this);
        }
    };
    
    // This expression
    class ThisExpr : public Expression {
    public:
        ThisExpr(const SourceLocation& loc) : Expression(loc) {}
        
        void accept(ASTVisitor* visitor) override {
            visitor->visitThisExpr(this);
        }
    };
    
    // Super expression
    class SuperExpr : public Expression {
    public:
        SuperExpr(const SourceLocation& loc) : Expression(loc) {}
        
        void accept(ASTVisitor* visitor) override {
            visitor->visitSuperExpr(this);
        }
    };
    
    // Base statement node
    class Statement : public ASTNode {
    public:
        Statement(const SourceLocation& loc) : ASTNode(loc) {}
    };
    
    // Expression statement
    class ExpressionStmt : public Statement {
    public:
        ExpressionPtr expression;
        
        ExpressionStmt(const SourceLocation& loc, ExpressionPtr expr)
            : Statement(loc), expression(expr) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitExpressionStmt(this);
        }
    };
    
    // Block statement
    class BlockStmt : public Statement {
    public:
        std::vector<DeclarationPtr> declarations;
        
        BlockStmt(const SourceLocation& loc, std::vector<DeclarationPtr> decls)
            : Statement(loc), declarations(std::move(decls)) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitBlockStmt(this);
        }
    };
    
    // If statement
    class IfStmt : public Statement {
    public:
        ExpressionPtr condition;
        StatementPtr thenStmt;
        StatementPtr elseStmt;  // Can be null
        
        IfStmt(const SourceLocation& loc, ExpressionPtr c, StatementPtr t, StatementPtr e = nullptr)
            : Statement(loc), condition(c), thenStmt(t), elseStmt(e) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitIfStmt(this);
        }
    };
    
    // While statement
    class WhileStmt : public Statement {
    public:
        ExpressionPtr condition;
        StatementPtr body;
        
        WhileStmt(const SourceLocation& loc, ExpressionPtr c, StatementPtr b)
            : Statement(loc), condition(c), body(b) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitWhileStmt(this);
        }
    };
    
    // For statement
    class ForStmt : public Statement {
    public:
        DeclarationPtr init;     // Can be null
        ExpressionPtr condition;  // Can be null
        ExpressionPtr update;     // Can be null
        StatementPtr body;
        
        ForStmt(const SourceLocation& loc, DeclarationPtr i, ExpressionPtr c, ExpressionPtr u, StatementPtr b)
            : Statement(loc), init(i), condition(c), update(u), body(b) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitForStmt(this);
        }
    };
    
    // Range-based for statement (C++11 style)
    class RangeForStmt : public Statement {
    public:
        TypeInfoPtr elementType;      // Type of loop variable (auto, int, etc.)
        std::string variableName;     // Name of loop variable
        bool isReference;             // true for auto&, false for auto
        bool isConst;                 // true for const auto&
        ExpressionPtr container;      // The container to iterate over
        StatementPtr body;            // Loop body
        
        RangeForStmt(const SourceLocation& loc, TypeInfoPtr type, const std::string& varName, 
                     bool ref, bool constRef, ExpressionPtr cont, StatementPtr b)
            : Statement(loc), elementType(type), variableName(varName), 
              isReference(ref), isConst(constRef), container(cont), body(b) {}
              
        void accept(ASTVisitor* visitor) override {
            visitor->visitRangeForStmt(this);
        }
    };
    
    // Return statement
    class ReturnStmt : public Statement {
    public:
        ExpressionPtr value;  // Can be null
        
        ReturnStmt(const SourceLocation& loc, ExpressionPtr v = nullptr)
            : Statement(loc), value(v) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitReturnStmt(this);
        }
    };
    
    // Break statement
    class BreakStmt : public Statement {
    public:
        BreakStmt(const SourceLocation& loc) : Statement(loc) {}
        
        void accept(ASTVisitor* visitor) override {
            visitor->visitBreakStmt(this);
        }
    };
    
    // Continue statement
    class ContinueStmt : public Statement {
    public:
        ContinueStmt(const SourceLocation& loc) : Statement(loc) {}
        
        void accept(ASTVisitor* visitor) override {
            visitor->visitContinueStmt(this);
        }
    };
    
    // Base declaration node
    class Declaration : public Statement {
    public:
        Declaration(const SourceLocation& loc) : Statement(loc) {}
    };
    
    // Variable declaration
    class VariableDecl : public Declaration {
    public:
        TypeInfoPtr type;
        std::string name;
        ExpressionPtr initializer;  // Can be null
        
        VariableDecl(const SourceLocation& loc, TypeInfoPtr t, const std::string& n, ExpressionPtr init = nullptr)
            : Declaration(loc), type(t), name(n), initializer(init) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitVariableDecl(this);
        }
    };
    
    // Function declaration
    class FunctionDecl : public Declaration {
    public:
        std::string name;
        std::vector<Parameter> parameters;
        TypeInfoPtr returnType;
        std::shared_ptr<BlockStmt> body;
        
        FunctionDecl(const SourceLocation& loc, const std::string& n)
            : Declaration(loc), name(n) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitFunctionDecl(this);
        }
    };
    
    // Class declaration
    class ClassDecl : public Declaration {
    public:
        enum MemberVisibility {
            Public,
            Private
        };
        
        struct Member {
            MemberVisibility visibility;
            DeclarationPtr declaration;
        };
        
        std::string name;
        std::vector<std::string> baseClasses;
        std::vector<Member> members;
        
        ClassDecl(const SourceLocation& loc, const std::string& n)
            : Declaration(loc), name(n) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitClassDecl(this);
        }
    };
    
    // Expression as declaration (for top-level expressions)
    class ExpressionDecl : public Declaration {
    public:
        ExpressionPtr expression;
        
        ExpressionDecl(const SourceLocation& loc, ExpressionPtr expr)
            : Declaration(loc), expression(expr) {}
            
        void accept(ASTVisitor* visitor) override {
            visitor->visitExpressionDecl(this);
        }
    };
    
    // Statement as declaration (for top-level statements in scripting context)
    class StatementDecl : public Declaration {
    public:
        StatementPtr statement;
        
        StatementDecl(const SourceLocation& loc, StatementPtr stmt)
            : Declaration(loc), statement(stmt) {}
            
        void accept(ASTVisitor* visitor) override {
            // Just visit the wrapped statement
            statement->accept(visitor);
        }
    };
    
} // namespace JaiScript