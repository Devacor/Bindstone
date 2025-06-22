#pragma once

#include "ast.hpp"
#include "../core/value.hpp"
#include "../core/types.hpp"
#include <unordered_map>
#include <memory>
#include <vector>

namespace JaiScript {

    // String symbolizer for faster variable lookups (like FName in Unreal Engine)
    // IMPORTANT: This is a LOCAL-ONLY optimization. String IDs are NOT deterministic
    // across sessions/machines. Always serialize actual string names, never IDs!
    // For network sync or save/load, use the original string keys, not symbolized IDs.
    class StringSymbolizer {
    private:
        std::unordered_map<std::string, uint32_t> string_id_map_;
        std::vector<std::string> strings_;
        
    public:
        StringSymbolizer() {
            // Reserve capacity for typical script usage
            strings_.reserve(256);
            string_id_map_.reserve(256);
        }
        
        uint32_t intern(std::string_view str) {
            std::string key(str);
            if (auto it = string_id_map_.find(key); it != string_id_map_.end()) {
                return it->second;
            }
            uint32_t id = static_cast<uint32_t>(strings_.size());
            strings_.emplace_back(key);
            string_id_map_[key] = id;
            return id;
        }
        
        const std::string& getString(uint32_t id) const {
            return strings_[id];
        }
    };

    // Environment for storing variables in a scope
    class Environment {
    public:
        Environment(StringSymbolizer* symbolizer) : symbolizer_(symbolizer) {}
        Environment(std::shared_ptr<Environment> parent, StringSymbolizer* symbolizer) 
            : parent_(parent), symbolizer_(symbolizer) {}
        
        // Define a variable in the current scope
        void define(const std::string& name, const Value& value);
        void define(const std::string& name, Value&& value);
        
        // Get a variable value (searches parent scopes)
        Value get(const std::string& name) const;
        
        // Assign to an existing variable (searches parent scopes)
        void assign(const std::string& name, const Value& value);
        void assign(const std::string& name, Value&& value);
        
        // Check if variable exists in current or parent scopes
        bool contains(const std::string& name) const;
        
        // Get all variables in this scope (not including parent scopes)  
        // Returns a new map with string keys for compatibility
        std::unordered_map<std::string, Value> getLocalVariables() const;
        
        // Get all variables including parent scopes
        std::unordered_map<std::string, Value> getAllVariables() const;
        
    private:
        std::unordered_map<uint32_t, Value> values_;  // Use symbolized string IDs
        std::shared_ptr<Environment> parent_;
        StringSymbolizer* symbolizer_;
        
        friend class Interpreter;
    };
    
    // The interpreter implements the visitor pattern to execute the AST
    class Interpreter : public ASTVisitor {
    public:
        Interpreter();
        Interpreter(StringSymbolizer* externalSymbolizer);
        ~Interpreter() = default;
        
        // Add global variables before execution
        void addGlobals(const std::unordered_map<std::string, Value>& globals);
        
        // Execute a list of declarations and return the last value
        Value execute(const std::vector<DeclarationPtr>& declarations);
        
        // Execute with typed return value (throws if no return statement or type doesn't match)
        template<typename T>
        T execute(const std::vector<DeclarationPtr>& declarations) {
            execute(declarations);  // Execute the script
            
            // Require a return statement when requesting typed result
            if (!hasReturnValue_) {
                throw RuntimeError("Script must have a return statement when requesting typed result");
            }
            
            // Try to convert the return value to the requested type
            return returnValue_.as<T>();
        }
        
        // Execute a single expression and return its value
        Value evaluate(ExpressionPtr expr);
        
        // Return value access (for global scope return statements)
        bool hasReturnValue() const { return hasReturnValue_; }
        Value getReturnValue() const { return returnValue_; }
        
        // Variable access methods
        Value getVariable(const std::string& name) const;
        bool hasVariable(const std::string& name) const;
        std::unordered_map<std::string, Value> getAllVariables() const;
        
        // Expression visitors
        void visitLiteralExpr(LiteralExpr* expr) override;
        void visitIdentifierExpr(IdentifierExpr* expr) override;
        void visitBinaryExpr(BinaryExpr* expr) override;
        void visitUnaryExpr(UnaryExpr* expr) override;
        void visitAssignmentExpr(AssignmentExpr* expr) override;
        void visitCallExpr(CallExpr* expr) override;
        void visitMemberExpr(MemberExpr* expr) override;
        void visitLambdaExpr(LambdaExpr* expr) override;
        void visitNewExpr(NewExpr* expr) override;
        void visitTernaryExpr(TernaryExpr* expr) override;
        void visitArrayLiteralExpr(ArrayLiteralExpr* expr) override;
        void visitMapLiteralExpr(MapLiteralExpr* expr) override;
        void visitThisExpr(ThisExpr* expr) override;
        void visitSuperExpr(SuperExpr* expr) override;
        
        // Statement visitors
        void visitExpressionStmt(ExpressionStmt* stmt) override;
        void visitBlockStmt(BlockStmt* stmt) override;
        void visitIfStmt(IfStmt* stmt) override;
        void visitWhileStmt(WhileStmt* stmt) override;
        void visitForStmt(ForStmt* stmt) override;
        void visitRangeForStmt(RangeForStmt* stmt) override;
        void visitReturnStmt(ReturnStmt* stmt) override;
        void visitBreakStmt(BreakStmt* stmt) override;
        void visitContinueStmt(ContinueStmt* stmt) override;
        
        // Declaration visitors
        void visitVariableDecl(VariableDecl* decl) override;
        void visitFunctionDecl(FunctionDecl* decl) override;
        void visitClassDecl(ClassDecl* decl) override;
        void visitExpressionDecl(ExpressionDecl* decl) override;
        
    private:
        // Script-defined function storage
        struct ScriptDefinedFunction {
            std::string name;
            std::vector<Parameter> parameters;
            TypeInfoPtr returnType;
            std::shared_ptr<BlockStmt> body;
            std::shared_ptr<Environment> closureEnv;  // Capture environment for closures
            
            ScriptDefinedFunction(const std::string& n, const std::vector<Parameter>& params,
                                TypeInfoPtr retType, std::shared_ptr<BlockStmt> b, 
                                std::shared_ptr<Environment> env = nullptr)
                : name(n), parameters(params), returnType(retType), body(b), closureEnv(env) {}
        };
        
        // String symbolizer for variable names
        std::unique_ptr<StringSymbolizer> ownedSymbolizer_;  // Only used if we own it
        StringSymbolizer* stringSymbolizer_;  // Points to either owned or external
        
        // Current environment for variable storage
        std::shared_ptr<Environment> environment_;
        
        // Optimized stack for expression evaluation results
        class ValueStack {
        private:
            std::vector<Value> values_;
            size_t top_ = 0;
            
        public:
            ValueStack() { 
                values_.reserve(512);  // Increased capacity for complex expressions
            }
            
            void push(Value&& v) {
                if (top_ >= values_.size()) {
                    values_.resize(values_.size() == 0 ? 256 : values_.size() * 2);
                }
                values_[top_++] = std::move(v);
            }
            
            void push(const Value& v) {
                if (top_ >= values_.size()) {
                    values_.resize(values_.size() == 0 ? 256 : values_.size() * 2);
                }
                values_[top_++] = v;
            }
            
            Value& top() { 
                if (top_ == 0) {
                    throw RuntimeError("Internal error: empty value stack");
                }
                return values_[top_ - 1]; 
            }
            
            Value pop() {
                if (top_ == 0) {
                    throw RuntimeError("Internal error: empty value stack");
                }
                return std::move(values_[--top_]);
            }
            
            bool empty() const { return top_ == 0; }
            size_t size() const { return top_; }
            
            void clear() { top_ = 0; }
        };
        
        ValueStack valueStack_;
        
        // Return value storage for global scope returns
        Value returnValue_;
        bool hasReturnValue_ = false;
        
        // Helper to get the last evaluated value (inlined for performance)
        inline Value popValue() {
            return valueStack_.pop();
        }
        
        inline void pushValue(const Value& value) {
            valueStack_.push(value);
        }
        
        inline void pushValue(Value&& value) {
            valueStack_.push(std::move(value));
        }
        
        // Binary operation helpers
        Value evaluateArithmetic(const Value& left, TokenType op, const Value& right);
        Value evaluateComparison(const Value& left, TokenType op, const Value& right);
        Value evaluateLogical(const Value& left, TokenType op, const Value& right);
        Value evaluateBitwise(const Value& left, TokenType op, const Value& right);
        
        // Type conversion helpers (inlined for performance)
        inline bool isTruthy(const Value& value) {
            if (value.isNull()) return false;
            if (value.isBool()) return value.asBool();
            if (value.isInt()) return value.asInt() != 0;
            if (value.isFloat()) return value.asFloat() != 0.0;
            if (value.isString()) return !value.asString().empty();
            return true;  // Other types are truthy
        }
        
        inline Value toNumeric(const Value& value) {
            if (value.isInt()) {
                return Value(static_cast<Float>(value.asInt()));
            } else if (value.isFloat()) {
                return value;
            } else if (value.isBool()) {
                return Value(value.asBool() ? 1.0 : 0.0);
            } else {
                throw RuntimeError("Cannot convert to numeric value");
            }
        }
        
        // Function call helpers
        Value callFunction(const ScriptDefinedFunction& function, const std::vector<Value>& args);
        void validateFunctionArguments(const std::vector<Parameter>& params, const std::vector<Value>& args);
        Value makeFunction(std::shared_ptr<ScriptDefinedFunction> func);
    };
    
} // namespace JaiScript