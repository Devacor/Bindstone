#include "../../include/jaiscript/detail/interpreter.hpp"
#include "../../include/jaiscript/core/class_builder.hpp"
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iostream>

namespace JaiScript {

// Environment implementation
void Environment::define(const std::string& name, const Value& value) {
    uint32_t id = symbolizer_->intern(name);
    values_[id] = value;
}

void Environment::define(const std::string& name, Value&& value) {
    uint32_t id = symbolizer_->intern(name);
    values_[id] = std::move(value);
}

Value Environment::get(const std::string& name) const {
    uint32_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get(name);
    }
    
    throw RuntimeError("Undefined variable '" + name + "'");
}

void Environment::assign(const std::string& name, const Value& value) {
    uint32_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    if (parent_) {
        parent_->assign(name, value);
        return;
    }
    
    throw RuntimeError("Undefined variable '" + name + "'");
}

void Environment::assign(const std::string& name, Value&& value) {
    uint32_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }
    
    if (parent_) {
        parent_->assign(name, std::move(value));
        return;
    }
    
    throw RuntimeError("Undefined variable '" + name + "'");
}

bool Environment::contains(const std::string& name) const {
    uint32_t id = symbolizer_->intern(name);
    if (values_.find(id) != values_.end()) {
        return true;
    }
    return parent_ ? parent_->contains(name) : false;
}

std::unordered_map<std::string, Value> Environment::getLocalVariables() const {
    std::unordered_map<std::string, Value> result;
    for (const auto& [id, value] : values_) {
        result[symbolizer_->getString(id)] = value;
    }
    return result;
}

// Interpreter implementation
Interpreter::Interpreter() 
    : ownedSymbolizer_(std::make_unique<StringSymbolizer>()),
      stringSymbolizer_(ownedSymbolizer_.get()),
      environment_(std::make_shared<Environment>(stringSymbolizer_)),
      hasReturnValue_(false) {
}

Interpreter::Interpreter(StringSymbolizer* externalSymbolizer)
    : ownedSymbolizer_(nullptr),
      stringSymbolizer_(externalSymbolizer),
      environment_(std::make_shared<Environment>(stringSymbolizer_)),
      hasReturnValue_(false) {
}

void Interpreter::addGlobals(const std::unordered_map<std::string, Value>& globals) {
    for (const auto& [name, value] : globals) {
        environment_->define(name, value);
    }
}

Value Interpreter::execute(const std::vector<DeclarationPtr>& declarations) {
    Value lastValue;
    hasReturnValue_ = false;  // Reset return value state
    
    for (const auto& decl : declarations) {
        decl->accept(this);
        
        // If there's a value on the stack, save it as the last value
        if (!valueStack_.empty()) {
            lastValue = popValue();
        }
        
        // If we hit a return statement, break out of execution
        if (hasReturnValue_) {
            return returnValue_;
        }
    }
    
    return lastValue;
}

Value Interpreter::evaluate(ExpressionPtr expr) {
    expr->accept(this);
    return popValue();
}

// Variable access methods
Value Interpreter::getVariable(const std::string& name) const {
    return environment_->get(name);
}

bool Interpreter::hasVariable(const std::string& name) const {
    return environment_->contains(name);
}

std::unordered_map<std::string, Value> Interpreter::getAllVariables() const {
    // Since we should be at root scope after execution, just return local variables
    return environment_->getLocalVariables();
}


// Expression visitors
void Interpreter::visitLiteralExpr(LiteralExpr* expr) {
    pushValue(expr->value);
}

void Interpreter::visitIdentifierExpr(IdentifierExpr* expr) {
    pushValue(environment_->get(expr->name));
}

void Interpreter::visitBinaryExpr(BinaryExpr* expr) {
    // First check for custom operator functions
    std::string opName;
    switch (expr->op.type) {
        case TokenType::Plus: opName = "+"; break;
        case TokenType::Minus: opName = "-"; break;
        case TokenType::Star: opName = "*"; break;
        case TokenType::Slash: opName = "/"; break;
        case TokenType::Percent: opName = "%"; break;
        case TokenType::Less: opName = "<"; break;
        case TokenType::LessEqual: opName = "<="; break;
        case TokenType::Greater: opName = ">"; break;
        case TokenType::GreaterEqual: opName = ">="; break;
        case TokenType::EqualEqual: opName = "=="; break;
        case TokenType::BangEqual: opName = "!="; break;
        case TokenType::Spaceship: opName = "<=>"; break;
        case TokenType::Ampersand: opName = "&"; break;
        case TokenType::Pipe: opName = "|"; break;
        case TokenType::Caret: opName = "^"; break;
        case TokenType::LeftShift: opName = "<<"; break;
        case TokenType::RightShift: opName = ">>"; break;
        default: break;
    }
    
    if (!opName.empty() && environment_->contains(opName)) {
        try {
            Value opFunc = environment_->get(opName);
            if (opFunc.isFunction()) {
                // Evaluate operands for custom function
                expr->left->accept(this);
                Value left = popValue();
                
                expr->right->accept(this);
                Value right = popValue();
                
                // Call the custom operator function
                const ScriptFunction& func = opFunc.asFunction();
                std::vector<Value> args = {left, right};
                pushValue(func(args));
                return;
            }
        } catch (const std::exception& e) {
            // If custom operator fails, fall back to built-in behavior
            // Rethrow if it's not a simple lookup failure
            std::string error = e.what();
            if (error.find("Undefined variable") == std::string::npos) {
                throw;
            }
        }
    }
    
    // Fast path for literal arithmetic - avoid evaluation overhead
    if (auto* leftLit = dynamic_cast<LiteralExpr*>(expr->left.get())) {
        if (auto* rightLit = dynamic_cast<LiteralExpr*>(expr->right.get())) {
            const Value& leftVal = leftLit->value;
            const Value& rightVal = rightLit->value;
            
            // Fast path for integer arithmetic
            if (leftVal.isInt() && rightVal.isInt()) {
                Int leftInt = leftVal.asInt();
                Int rightInt = rightVal.asInt();
                
                switch (expr->op.type) {
                    case TokenType::Plus:
                        pushValue(Value(leftInt + rightInt));
                        return;
                    case TokenType::Minus:
                        pushValue(Value(leftInt - rightInt));
                        return;
                    case TokenType::Star:
                        pushValue(Value(leftInt * rightInt));
                        return;
                    case TokenType::Slash:
                        if (rightInt == 0) {
                            throw RuntimeError("Division by zero");
                        }
                        pushValue(Value(leftInt / rightInt));
                        return;
                    case TokenType::Percent:
                        if (rightInt == 0) {
                            throw RuntimeError("Division by zero");
                        }
                        pushValue(Value(leftInt % rightInt));
                        return;
                    case TokenType::Less:
                        pushValue(Value(leftInt < rightInt));
                        return;
                    case TokenType::LessEqual:
                        pushValue(Value(leftInt <= rightInt));
                        return;
                    case TokenType::Greater:
                        pushValue(Value(leftInt > rightInt));
                        return;
                    case TokenType::GreaterEqual:
                        pushValue(Value(leftInt >= rightInt));
                        return;
                    case TokenType::EqualEqual:
                        pushValue(Value(leftInt == rightInt));
                        return;
                    case TokenType::BangEqual:
                        pushValue(Value(leftInt != rightInt));
                        return;
                    case TokenType::Spaceship:
                        pushValue(Value(leftInt < rightInt ? Int(-1) : (leftInt > rightInt ? Int(1) : Int(0))));
                        return;
                    default:
                        break; // Fall through to generic path
                }
            }
            // Fast path for float arithmetic
            else if ((leftVal.isInt() || leftVal.isFloat()) && (rightVal.isInt() || rightVal.isFloat())) {
                Float leftFloat = leftVal.isInt() ? static_cast<Float>(leftVal.asInt()) : leftVal.asFloat();
                Float rightFloat = rightVal.isInt() ? static_cast<Float>(rightVal.asInt()) : rightVal.asFloat();
                
                switch (expr->op.type) {
                    case TokenType::Plus:
                        pushValue(Value(leftFloat + rightFloat));
                        return;
                    case TokenType::Minus:
                        pushValue(Value(leftFloat - rightFloat));
                        return;
                    case TokenType::Star:
                        pushValue(Value(leftFloat * rightFloat));
                        return;
                    case TokenType::Slash:
                        if (rightFloat == 0.0) {
                            throw RuntimeError("Division by zero");
                        }
                        pushValue(Value(leftFloat / rightFloat));
                        return;
                    case TokenType::Less:
                        pushValue(Value(leftFloat < rightFloat));
                        return;
                    case TokenType::LessEqual:
                        pushValue(Value(leftFloat <= rightFloat));
                        return;
                    case TokenType::Greater:
                        pushValue(Value(leftFloat > rightFloat));
                        return;
                    case TokenType::GreaterEqual:
                        pushValue(Value(leftFloat >= rightFloat));
                        return;
                    case TokenType::EqualEqual:
                        pushValue(Value(leftFloat == rightFloat));
                        return;
                    case TokenType::BangEqual:
                        pushValue(Value(leftFloat != rightFloat));
                        return;
                    case TokenType::Spaceship:
                        pushValue(Value(leftFloat < rightFloat ? Int(-1) : (leftFloat > rightFloat ? Int(1) : Int(0))));
                        return;
                    default:
                        break; // Fall through to generic path
                }
            }
            // Fast path for string concatenation
            else if (expr->op.type == TokenType::Plus && leftVal.isString() && rightVal.isString()) {
                pushValue(Value(leftVal.asString() + rightVal.asString()));
                return;
            }
        }
    }
    
    // Handle logical operators specially for short-circuit evaluation
    if (expr->op.type == TokenType::AmpersandAmpersand || expr->op.type == TokenType::PipePipe) {
        // Evaluate left operand (which could be a complex expression with brackets)
        expr->left->accept(this);
        Value left = popValue();
        
        bool leftTruthy = isTruthy(left);
        
        if (expr->op.type == TokenType::AmpersandAmpersand) {
            // Short-circuit: if left is false, return false without evaluating right
            if (!leftTruthy) {
                pushValue(Value(false));
                return;
            }
            // Otherwise evaluate right and return its truthiness
            expr->right->accept(this);
            Value right = popValue();
            pushValue(Value(isTruthy(right)));
            return;
        } else { // PipePipe
            // Short-circuit: if left is true, return true without evaluating right
            if (leftTruthy) {
                pushValue(Value(true));
                return;
            }
            // Otherwise evaluate right and return its truthiness
            expr->right->accept(this);
            Value right = popValue();
            pushValue(Value(isTruthy(right)));
            return;
        }
    }
    
    // Generic path - evaluate operands and use existing logic
    expr->left->accept(this);
    Value left = popValue();
    
    expr->right->accept(this);
    Value right = popValue();
    
    // First check if there's a custom operator function registered
    switch (expr->op.type) {
        case TokenType::Plus: opName = "+"; break;
        case TokenType::Minus: opName = "-"; break;
        case TokenType::Star: opName = "*"; break;
        case TokenType::Slash: opName = "/"; break;
        case TokenType::Percent: opName = "%"; break;
        case TokenType::Less: opName = "<"; break;
        case TokenType::LessEqual: opName = "<="; break;
        case TokenType::Greater: opName = ">"; break;
        case TokenType::GreaterEqual: opName = ">="; break;
        case TokenType::EqualEqual: opName = "=="; break;
        case TokenType::BangEqual: opName = "!="; break;
        case TokenType::Spaceship: opName = "<=>"; break;
        case TokenType::Ampersand: opName = "&"; break;
        case TokenType::Pipe: opName = "|"; break;
        case TokenType::Caret: opName = "^"; break;
        case TokenType::LeftShift: opName = "<<"; break;
        case TokenType::RightShift: opName = ">>"; break;
        default: break;
    }
    
    if (!opName.empty()) {
        try {
            // Check if there's a custom operator function in the environment chain
            if (environment_->contains(opName)) {
                Value opFunc = environment_->get(opName);
                if (opFunc.isFunction()) {
                    // Call the custom operator function
                    const ScriptFunction& func = opFunc.asFunction();
                    std::vector<Value> args = {left, right};
                    pushValue(func(args));
                    return;
                }
            }
        } catch (const std::exception& e) {
            // If custom operator fails, fall back to built-in behavior
            // Rethrow if it's not a simple lookup failure
            std::string error = e.what();
            if (error.find("Undefined variable") == std::string::npos) {
                throw;
            }
        }
    }
    
    // Perform the operation based on the operator
    switch (expr->op.type) {
        // Arithmetic operators
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
            pushValue(evaluateArithmetic(left, expr->op.type, right));
            break;
            
        // Comparison operators
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual:
        case TokenType::EqualEqual:
        case TokenType::BangEqual:
        case TokenType::Spaceship:
            pushValue(evaluateComparison(left, expr->op.type, right));
            break;
            
            
        // Subscript operator
        case TokenType::LeftBracket: {
            // Array or Map subscript access
            if (left.isArray()) {
                // Array access: arr[index]
                if (!right.isInt()) {
                    throw RuntimeError("Array index must be an integer");
                }
                Int index = right.asInt();
                const auto& array = left.asArray();
                
                if (index < 0 || index >= static_cast<Int>(array.size())) {
                    throw RuntimeError("Array index out of bounds: " + std::to_string(index));
                }
                
                pushValue(array[index]);
            } else if (left.isMap()) {
                // Map access: map[key]
                const auto& map = left.asMap();
                auto it = map.find(right);
                
                if (it != map.end()) {
                    pushValue(it->second);
                } else {
                    // Return null for missing keys (like JavaScript/Python)
                    pushValue(Value());
                }
            } else {
                throw RuntimeError("Subscript operator [] can only be used on arrays and maps");
            }
            break;
        }
            
        // Bitwise operators
        case TokenType::Ampersand:
        case TokenType::Pipe:
        case TokenType::Caret:
        case TokenType::LeftShift:
        case TokenType::RightShift:
            pushValue(evaluateBitwise(left, expr->op.type, right));
            break;
            
        default:
            throw RuntimeError("Unsupported binary operator");
    }
}

void Interpreter::visitUnaryExpr(UnaryExpr* expr) {
    // Fast path for literal unary operations
    if (auto* literal = dynamic_cast<LiteralExpr*>(expr->operand.get())) {
        const Value& val = literal->value;
        
        switch (expr->op.type) {
            case TokenType::Minus:
                if (val.isInt()) {
                    pushValue(Value(-val.asInt()));
                    return;
                } else if (val.isFloat()) {
                    pushValue(Value(-val.asFloat()));
                    return;
                }
                break;
            case TokenType::Bang:
                pushValue(Value(!isTruthy(val)));
                return;
            case TokenType::Tilde:
                if (val.isInt()) {
                    pushValue(Value(~val.asInt()));
                    return;
                }
                break;
            default:
                break; // Fall through to generic path for increment/decrement
        }
    }
    
    // Generic path - evaluate operand and use existing logic
    expr->operand->accept(this);
    Value operand = popValue();
    
    switch (expr->op.type) {
        case TokenType::Minus:
            if (operand.isInt()) {
                pushValue(Value(-operand.asInt()));
            } else if (operand.isFloat()) {
                pushValue(Value(-operand.asFloat()));
            } else {
                throw RuntimeError("Unary minus requires numeric operand");
            }
            break;
            
        case TokenType::Bang:
            pushValue(Value(!isTruthy(operand)));
            break;
            
        case TokenType::Tilde:
            // Bitwise NOT
            if (!operand.isInt()) {
                throw RuntimeError("Bitwise NOT requires integer operand");
            }
            pushValue(Value(~operand.asInt()));
            break;
            
        case TokenType::PlusPlus:
        case TokenType::MinusMinus: {
            // Handle increment/decrement
            if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr->operand.get())) {
                Value currentValue = environment_->get(identifier->name);
                Value newValue;
                
                if (currentValue.isInt()) {
                    int64_t val = currentValue.asInt();
                    if (expr->op.type == TokenType::PlusPlus) {
                        newValue = Value(val + 1);
                    } else {
                        newValue = Value(val - 1);
                    }
                } else if (currentValue.isFloat()) {
                    double val = currentValue.asFloat();
                    if (expr->op.type == TokenType::PlusPlus) {
                        newValue = Value(val + 1.0);
                    } else {
                        newValue = Value(val - 1.0);
                    }
                } else {
                    throw RuntimeError("Cannot increment/decrement non-numeric value");
                }
                
                environment_->assign(identifier->name, newValue);
                
                // For prefix, return the new value; for postfix, return the old value
                if (expr->isPostfix) {
                    pushValue(currentValue);
                } else {
                    pushValue(newValue);
                }
            } else {
                throw RuntimeError("Increment/decrement requires a variable");
            }
            break;
        }
            
        default:
            throw RuntimeError("Unsupported unary operator");
    }
}

void Interpreter::visitAssignmentExpr(AssignmentExpr* expr) {
    // For compound assignment operators, we need the current value
    if (expr->op.type != TokenType::Equal) {
        // Get current value of the target
        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr->target.get())) {
            Value currentValue = environment_->get(identifier->name);
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            Value rightValue = popValue();
            
            // Perform the compound operation
            Value resultValue;
            switch (expr->op.type) {
                case TokenType::PlusEqual:
                    if (currentValue.isInt() && rightValue.isInt()) {
                        resultValue = Value(currentValue.asInt() + rightValue.asInt());
                    } else if ((currentValue.isInt() || currentValue.isFloat()) && (rightValue.isInt() || rightValue.isFloat())) {
                        resultValue = Value(currentValue.asFloat() + rightValue.asFloat());
                    } else if (currentValue.isString() && rightValue.isString()) {
                        resultValue = Value(currentValue.asString() + rightValue.asString());
                    } else {
                        throw RuntimeError("Invalid operands for +=");
                    }
                    break;
                    
                case TokenType::MinusEqual:
                    if (currentValue.isInt() && rightValue.isInt()) {
                        resultValue = Value(currentValue.asInt() - rightValue.asInt());
                    } else if ((currentValue.isInt() || currentValue.isFloat()) && (rightValue.isInt() || rightValue.isFloat())) {
                        resultValue = Value(currentValue.asFloat() - rightValue.asFloat());
                    } else {
                        throw RuntimeError("Invalid operands for -=");
                    }
                    break;
                    
                case TokenType::StarEqual:
                    if (currentValue.isInt() && rightValue.isInt()) {
                        resultValue = Value(currentValue.asInt() * rightValue.asInt());
                    } else if ((currentValue.isInt() || currentValue.isFloat()) && (rightValue.isInt() || rightValue.isFloat())) {
                        resultValue = Value(currentValue.asFloat() * rightValue.asFloat());
                    } else {
                        throw RuntimeError("Invalid operands for *=");
                    }
                    break;
                    
                case TokenType::SlashEqual:
                    if (rightValue.isInt() && rightValue.asInt() == 0) {
                        throw RuntimeError("Division by zero");
                    }
                    if (rightValue.isFloat() && rightValue.asFloat() == 0.0) {
                        throw RuntimeError("Division by zero");
                    }
                    
                    if (currentValue.isInt() && rightValue.isInt()) {
                        resultValue = Value(currentValue.asInt() / rightValue.asInt());
                    } else if ((currentValue.isInt() || currentValue.isFloat()) && (rightValue.isInt() || rightValue.isFloat())) {
                        resultValue = Value(currentValue.asFloat() / rightValue.asFloat());
                    } else {
                        throw RuntimeError("Invalid operands for /=");
                    }
                    break;
                    
                default:
                    throw RuntimeError("Unsupported compound assignment operator");
            }
            
            // Assign the result
            environment_->assign(identifier->name, resultValue);
            pushValue(resultValue);
        } else {
            throw RuntimeError("Compound assignment requires simple identifier target");
        }
    } else {
        // Regular assignment
        expr->value->accept(this);
        Value value = popValue();
        
        // Check if target is an identifier
        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr->target.get())) {
            if (environment_->contains(identifier->name)) {
                // Need to copy here since we need the value for the return
                environment_->assign(identifier->name, value);
            } else {
                // If variable doesn't exist, define it (JavaScript-like behavior)
                // Need to copy here since we need the value for the return
                environment_->define(identifier->name, value);
            }
            pushValue(std::move(value));  // Assignment expressions return the assigned value
        } 
        // Check if target is a member expression (property assignment)
        else if (auto* memberExpr = dynamic_cast<MemberExpr*>(expr->target.get())) {
            // Evaluate the object
            memberExpr->object->accept(this);
            Value objectValue = popValue();
            
            // Check if it's an object
            if (!objectValue.isObject()) {
                throw RuntimeError("Cannot assign to member of non-object type");
            }
            
            // Extract the ClassInstance
            auto objHolder = std::get<std::shared_ptr<Value::ObjectHolder>>(objectValue.storage_);
            auto instance = std::static_pointer_cast<ClassInstance>(objHolder->data);
            
            // Check if there's a property setter
            Value setter = instance->getMethod("__set_" + memberExpr->member);
            if (!setter.isNull()) {
                // Call the setter with 'this' and the value
                const ScriptFunction& func = setter.asFunction();
                std::vector<Value> args = {objectValue, value};
                func(args);
            } else if (instance->hasField(memberExpr->member)) {
                // Direct field assignment
                instance->setField(memberExpr->member, value);
            } else {
                throw RuntimeError("Cannot assign to non-existent member '" + memberExpr->member + "'");
            }
            
            pushValue(std::move(value));  // Assignment expressions return the assigned value
        }
        // Check if target is a subscript expression (array[index] or map[key])
        else if (auto* binaryExpr = dynamic_cast<BinaryExpr*>(expr->target.get())) {
            if (binaryExpr->op.type == TokenType::LeftBracket) {
                // Evaluate the array/map
                binaryExpr->left->accept(this);
                Value containerValue = popValue();
                
                // Evaluate the index/key
                binaryExpr->right->accept(this);
                Value indexValue = popValue();
                
                if (containerValue.isArray()) {
                    // Array assignment: arr[index] = value
                    if (!indexValue.isInt()) {
                        throw RuntimeError("Array index must be an integer");
                    }
                    Int index = indexValue.asInt();
                    auto& array = const_cast<std::vector<Value>&>(containerValue.asArray());
                    
                    if (index < 0 || index >= static_cast<Int>(array.size())) {
                        throw RuntimeError("Array index out of bounds: " + std::to_string(index));
                    }
                    
                    array[index] = value;
                } else if (containerValue.isMap()) {
                    // Map assignment: map[key] = value
                    auto& map = const_cast<std::map<Value, Value>&>(containerValue.asMap());
                    map[indexValue] = value;
                } else {
                    throw RuntimeError("Subscript assignment can only be used on arrays and maps");
                }
                
                pushValue(std::move(value));  // Assignment expressions return the assigned value
            } else {
                throw RuntimeError("Complex assignment targets not yet implemented");
            }
        } else {
            throw RuntimeError("Complex assignment targets not yet implemented");
        }
    }
}

// Statement visitors
void Interpreter::visitExpressionStmt(ExpressionStmt* stmt) {
    stmt->expression->accept(this);
    // For top-level expressions (wrapped in ExpressionDecl), we want to keep the value
    // The execute() method will handle popping it
}

void Interpreter::visitBlockStmt(BlockStmt* stmt) {
    // Create new environment for the block scope
    auto previous = environment_;
    environment_ = std::make_shared<Environment>(environment_, stringSymbolizer_);
    
    try {
        for (const auto& decl : stmt->declarations) {
            decl->accept(this);
        }
    } catch (...) {
        // Restore environment even if an error occurs
        environment_ = previous;
        throw;
    }
    
    // Restore previous environment
    environment_ = previous;
}

void Interpreter::visitVariableDecl(VariableDecl* decl) {
    Value value;
    
    if (decl->initializer) {
        decl->initializer->accept(this);
        value = popValue();
    }
    // If no initializer, value remains null
    
    environment_->define(decl->name, std::move(value));
}

// Binary operation helpers
Value Interpreter::evaluateArithmetic(const Value& left, TokenType op, const Value& right) {
    // Special case for string concatenation
    if (op == TokenType::Plus && (left.isString() || right.isString())) {
        return Value(left.toString() + right.toString());
    }
    
    // Convert to numeric values
    Float leftNum = 0.0;
    Float rightNum = 0.0;
    bool useInt = left.isInt() && right.isInt();
    
    if (left.isInt()) {
        leftNum = static_cast<Float>(left.asInt());
    } else if (left.isFloat()) {
        leftNum = left.asFloat();
        useInt = false;
    } else {
        throw RuntimeError("Left operand must be numeric");
    }
    
    if (right.isInt()) {
        rightNum = static_cast<Float>(right.asInt());
    } else if (right.isFloat()) {
        rightNum = right.asFloat();
        useInt = false;
    } else {
        throw RuntimeError("Right operand must be numeric");
    }
    
    Float result = 0.0;
    switch (op) {
        case TokenType::Plus:
            result = leftNum + rightNum;
            break;
        case TokenType::Minus:
            result = leftNum - rightNum;
            break;
        case TokenType::Star:
            result = leftNum * rightNum;
            break;
        case TokenType::Slash:
            if (rightNum == 0.0) {
                throw RuntimeError("Division by zero");
            }
            result = leftNum / rightNum;
            useInt = false;  // Division always returns float
            break;
        case TokenType::Percent:
            if (rightNum == 0.0) {
                throw RuntimeError("Division by zero");
            }
            result = std::fmod(leftNum, rightNum);
            break;
        default:
            throw RuntimeError("Unknown arithmetic operator");
    }
    
    // Return int if both operands were int and operation preserves int
    if (useInt) {
        return Value(static_cast<Int>(result));
    } else {
        return Value(result);
    }
}

Value Interpreter::evaluateComparison(const Value& left, TokenType op, const Value& right) {
    // Handle null comparisons
    if (left.isNull() || right.isNull()) {
        switch (op) {
            case TokenType::EqualEqual:
                return Value(left.isNull() && right.isNull());
            case TokenType::BangEqual:
                return Value(!(left.isNull() && right.isNull()));
            default:
                throw RuntimeError("Cannot compare null values with relational operators");
        }
    }
    
    // For now, only support numeric and string comparisons
    if (left.isString() && right.isString()) {
        const auto& leftStr = left.asString();
        const auto& rightStr = right.asString();
        
        switch (op) {
            case TokenType::Less:
                return Value(leftStr < rightStr);
            case TokenType::LessEqual:
                return Value(leftStr <= rightStr);
            case TokenType::Greater:
                return Value(leftStr > rightStr);
            case TokenType::GreaterEqual:
                return Value(leftStr >= rightStr);
            case TokenType::EqualEqual:
                return Value(leftStr == rightStr);
            case TokenType::BangEqual:
                return Value(leftStr != rightStr);
            case TokenType::Spaceship: {
                // Three-way comparison for strings
                int cmp = leftStr.compare(rightStr);
                return Value(cmp < 0 ? Int(-1) : (cmp > 0 ? Int(1) : Int(0)));
            }
            default:
                throw RuntimeError("Unknown comparison operator");
        }
    }
    
    // Numeric comparison
    Float leftNum = toNumeric(left).asFloat();
    Float rightNum = toNumeric(right).asFloat();
    
    switch (op) {
        case TokenType::Less:
            return Value(leftNum < rightNum);
        case TokenType::LessEqual:
            return Value(leftNum <= rightNum);
        case TokenType::Greater:
            return Value(leftNum > rightNum);
        case TokenType::GreaterEqual:
            return Value(leftNum >= rightNum);
        case TokenType::EqualEqual:
            return Value(leftNum == rightNum);
        case TokenType::BangEqual:
            return Value(leftNum != rightNum);
        case TokenType::Spaceship: {
            // Three-way comparison for numbers
            // Return -1 if less, 0 if equal, 1 if greater
            if (leftNum < rightNum) return Value(Int(-1));
            else if (leftNum > rightNum) return Value(Int(1));
            else return Value(Int(0));
        }
        default:
            throw RuntimeError("Unknown comparison operator");
    }
}

Value Interpreter::evaluateLogical(const Value& left, TokenType op, const Value& right) {
    bool leftTruthy = isTruthy(left);
    
    switch (op) {
        case TokenType::AmpersandAmpersand:
            // Short-circuit: if left is false, return left
            if (!leftTruthy) {
                return left;
            }
            return right;
            
        case TokenType::PipePipe:
            // Short-circuit: if left is true, return left
            if (leftTruthy) {
                return left;
            }
            return right;
            
        default:
            throw RuntimeError("Unknown logical operator");
    }
}

Value Interpreter::evaluateBitwise(const Value& left, TokenType op, const Value& right) {
    // Bitwise operations only work on integers
    if (!left.isInt() || !right.isInt()) {
        throw RuntimeError("Bitwise operations require integer operands");
    }
    
    Int leftInt = left.asInt();
    Int rightInt = right.asInt();
    
    switch (op) {
        case TokenType::Ampersand:
            return Value(leftInt & rightInt);
        case TokenType::Pipe:
            return Value(leftInt | rightInt);
        case TokenType::Caret:
            return Value(leftInt ^ rightInt);
        case TokenType::LeftShift:
            return Value(leftInt << rightInt);
        case TokenType::RightShift:
            return Value(leftInt >> rightInt);
        default:
            throw RuntimeError("Unknown bitwise operator");
    }
}


// Placeholder implementations for remaining visitors
void Interpreter::visitCallExpr(CallExpr* expr) {
    // Evaluate the callee expression
    expr->callee->accept(this);
    Value callee = popValue();
    
    // Check if the callee is a function
    if (!callee.isFunction()) {
        throw RuntimeError("Cannot call non-function value");
    }
    
    // Evaluate all arguments
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    
    for (const auto& argExpr : expr->arguments) {
        argExpr->accept(this);
        args.push_back(std::move(popValue()));
    }
    
    // Call the function
    const ScriptFunction& func = callee.asFunction();
    Value result = func(args);
    
    // Push result onto the stack
    pushValue(result);
}

void Interpreter::visitMemberExpr(MemberExpr* expr) {
    // Evaluate the object expression
    expr->object->accept(this);
    Value objectValue = popValue();
    
    // Check if it's an object
    if (!objectValue.isObject()) {
        throw RuntimeError("Cannot access member '" + expr->member + "' on non-object type");
    }
    
    // Extract the ClassInstance from the object
    // Access the ObjectHolder directly since we're a friend class
    auto objHolder = std::get<std::shared_ptr<Value::ObjectHolder>>(objectValue.storage_);
    auto instance = std::static_pointer_cast<ClassInstance>(objHolder->data);
    
    // First check if it's a field (registered by the property() method)
    if (instance->hasField(expr->member)) {
        // Check if there's a property getter method
        Value getter = instance->getMethod("__get_" + expr->member);
        if (!getter.isNull()) {
            // Call the getter with 'this' as argument
            const ScriptFunction& func = getter.asFunction();
            std::vector<Value> args = {objectValue};
            pushValue(func(args));
            return;
        }
        // Otherwise return the field value directly
        pushValue(instance->getField(expr->member));
        return;
    }
    
    // Otherwise, look for a method
    Value method = instance->getMethod(expr->member);
    if (!method.isNull()) {
        // Return a bound method (function that has 'this' pre-bound)
        // We'll create a wrapper function that includes the object as first argument
        ScriptFunction boundMethod = [objectValue, method](const std::vector<Value>& args) -> Value {
            // Prepend the object as the first argument ('this')
            std::vector<Value> methodArgs;
            methodArgs.reserve(args.size() + 1);
            methodArgs.push_back(objectValue);
            methodArgs.insert(methodArgs.end(), args.begin(), args.end());
            
            // Call the original method with 'this' as first argument
            const ScriptFunction& func = method.asFunction();
            return func(methodArgs);
        };
        
        pushValue(Value::makeFunction(boundMethod));
        return;
    }
    
    throw RuntimeError("Object has no member '" + expr->member + "'");
}

void Interpreter::visitLambdaExpr(LambdaExpr* expr) {
    // Capture current environment for closure
    auto closureEnv = environment_;
    
    // Create captured variables in the closure environment if needed
    std::shared_ptr<Environment> captureEnv = std::make_shared<Environment>(closureEnv, stringSymbolizer_);
    
    // Process captures
    for (const auto& capture : expr->captures) {
        if (environment_->contains(capture.name)) {
            if (capture.byReference) {
                // For reference capture, we don't copy the value to the capture environment
                // Instead, we let the lambda access the variable through the parent environment chain
                // This allows modifications to affect the original variable
                // The capture environment will automatically delegate to the parent for this variable
                // We mark this as a reference capture by NOT defining it in the capture environment
                // This way, variable access will go through the parent environment chain
            } else {
                // For value capture, make a copy at capture time
                Value capturedValue = environment_->get(capture.name);
                captureEnv->define(capture.name, capturedValue);
            }
        } else {
            throw RuntimeError("Cannot capture undefined variable: " + capture.name);
        }
    }
    
    // Convert the lambda body to a BlockStmt if it's not already
    std::shared_ptr<BlockStmt> lambdaBody;
    if (auto blockStmt = std::dynamic_pointer_cast<BlockStmt>(expr->body)) {
        lambdaBody = blockStmt;
    } else {
        // Wrap single statement in a block
        std::vector<DeclarationPtr> stmts;
        if (auto stmt = std::dynamic_pointer_cast<Statement>(expr->body)) {
            auto stmtDecl = std::make_shared<StatementDecl>(expr->location, stmt);
            stmts.push_back(stmtDecl);
        }
        lambdaBody = std::make_shared<BlockStmt>(expr->location, std::move(stmts));
    }
    
    // Create the script function
    auto lambdaFunc = std::make_shared<ScriptDefinedFunction>(
        "<lambda>",  // Anonymous function name
        expr->parameters,
        expr->returnType,
        lambdaBody,
        captureEnv  // Use the capture environment
    );
    
    // Create a ScriptFunction wrapper
    // Capture lambdaFunc by value to ensure it stays alive
    ScriptFunction funcWrapper = [this, lambdaFunc](const std::vector<Value>& args) -> Value {
        return callFunction(*lambdaFunc, args);
    };
    
    // Push the lambda as a function value
    pushValue(Value::makeFunction(funcWrapper));
}

void Interpreter::visitNewExpr(NewExpr* expr) {
    // This handles expressions like: new Point(), new Point(3.0, 4.0), etc.
    // The NewExpr contains a type and arguments
    
    if (!expr->type) {
        throw RuntimeError("New expression missing type information");
    }
    
    std::string className = expr->type->typeName;
    
    // Evaluate all arguments
    std::vector<Value> args;
    for (const auto& argExpr : expr->arguments) {
        argExpr->accept(this);
        args.push_back(std::move(popValue()));
    }
    
    // Look for a constructor function registered with this class name
    // The class builder registers constructors as overloaded functions
    try {
        Value constructorFunc = environment_->get(className);
        if (constructorFunc.isFunction()) {
            const ScriptFunction& func = constructorFunc.asFunction();
            Value instance = func(args);
            pushValue(std::move(instance));
            return;
        }
    } catch (const RuntimeError&) {
        // Constructor function not found, fall through to error
    }
    
    throw RuntimeError("No constructor found for class: " + className);
}

void Interpreter::visitTernaryExpr(TernaryExpr* expr) {
    // Evaluate the condition
    expr->condition->accept(this);
    Value conditionValue = popValue();
    
    // Check if condition is truthy
    bool conditionIsTruthy = isTruthy(conditionValue);
    
    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        expr->thenExpr->accept(this);
    } else {
        expr->elseExpr->accept(this);
    }
}

void Interpreter::visitArrayLiteralExpr(ArrayLiteralExpr* expr) {
    // Create array Value with mixed element type (for now)
    auto elementType = TypeInfo::makeInt(); // TODO: Better type inference
    Value arrayValue = Value::makeArray(elementType);
    
    // Get the internal vector to populate
    auto& array = const_cast<std::vector<Value>&>(arrayValue.asArray());
    
    // Evaluate each element and add to array
    for (const auto& element : expr->elements) {
        element->accept(this);
        array.push_back(popValue());
    }
    
    pushValue(std::move(arrayValue));
}

void Interpreter::visitMapLiteralExpr(MapLiteralExpr* expr) {
    // Create map Value with mixed key/value types (for now)
    auto keyType = TypeInfo::makeString(); // TODO: Better type inference
    auto valueType = TypeInfo::makeInt(); // TODO: Better type inference
    Value mapValue = Value::makeMap(keyType, valueType);
    
    // Get the internal map to populate
    auto& map = const_cast<std::map<Value, Value>&>(mapValue.asMap());
    
    // Evaluate each key-value pair and add to map
    for (const auto& entry : expr->entries) {
        // Evaluate key
        entry.first->accept(this);
        Value key = popValue();
        
        // Evaluate value
        entry.second->accept(this);
        Value value = popValue();
        
        // Insert into map
        map[std::move(key)] = std::move(value);
    }
    
    pushValue(std::move(mapValue));
}

void Interpreter::visitThisExpr(ThisExpr* expr) {
    throw RuntimeError("'this' keyword not yet implemented");
}

void Interpreter::visitSuperExpr(SuperExpr* expr) {
    throw RuntimeError("'super' keyword not yet implemented");
}

void Interpreter::visitIfStmt(IfStmt* stmt) {
    // Evaluate the condition
    stmt->condition->accept(this);
    Value conditionValue = popValue();
    
    // Execute appropriate branch based on truthiness
    if (isTruthy(conditionValue)) {
        stmt->thenStmt->accept(this);
    } else if (stmt->elseStmt) {
        stmt->elseStmt->accept(this);
    }
}

void Interpreter::visitWhileStmt(WhileStmt* stmt) {
    while (true) {
        // Evaluate the condition
        stmt->condition->accept(this);
        Value conditionValue = popValue();
        
        // Check if we should continue the loop
        if (!isTruthy(conditionValue)) {
            break;
        }
        
        // Execute the loop body
        stmt->body->accept(this);
        
        // TODO: Handle break/continue statements when implemented
    }
}

void Interpreter::visitForStmt(ForStmt* stmt) {
    // Create new scope for the for loop (initialization variables should be scoped)
    auto previous = environment_;
    environment_ = std::make_shared<Environment>(environment_, stringSymbolizer_);
    
    try {
        // Execute initialization (if present)
        if (stmt->init) {
            stmt->init->accept(this);
        }
        
        while (true) {
            // Check condition (if present, default to true)
            if (stmt->condition) {
                stmt->condition->accept(this);
                Value conditionValue = popValue();
                if (!isTruthy(conditionValue)) {
                    break;
                }
            }
            
            // Execute the loop body
            stmt->body->accept(this);
            
            // Execute update expression (if present)
            if (stmt->update) {
                stmt->update->accept(this);
                // Pop the update result if it leaves a value on the stack
                if (!valueStack_.empty()) {
                    popValue();
                }
            }
            
            // TODO: Handle break/continue statements when implemented
        }
    } catch (...) {
        // Restore environment even if an error occurs
        environment_ = previous;
        throw;
    }
    
    // Restore previous environment
    environment_ = previous;
}

void Interpreter::visitRangeForStmt(RangeForStmt* stmt) {
    throw RuntimeError("Range-based for loops not yet implemented");
}

void Interpreter::visitReturnStmt(ReturnStmt* stmt) {
    if (stmt->value) {
        // Evaluate the return expression
        stmt->value->accept(this);
        returnValue_ = popValue();
    } else {
        // Return null if no expression
        returnValue_ = Value();
    }
    
    hasReturnValue_ = true;
}

void Interpreter::visitBreakStmt(BreakStmt* stmt) {
    throw RuntimeError("Break statements not yet implemented");
}

void Interpreter::visitContinueStmt(ContinueStmt* stmt) {
    throw RuntimeError("Continue statements not yet implemented");
}

void Interpreter::visitFunctionDecl(FunctionDecl* decl) {
    // Create a script-defined function
    auto scriptFunc = std::make_shared<ScriptDefinedFunction>(
        decl->name,
        decl->parameters,
        decl->returnType,
        decl->body,
        environment_  // Capture current environment for closures
    );
    
    // Create a ScriptFunction wrapper that will call our function
    ScriptFunction funcWrapper = [this, scriptFunc](const std::vector<Value>& args) -> Value {
        return callFunction(*scriptFunc, args);
    };
    
    // Store the function in the environment
    Value functionValue = Value::makeFunction(funcWrapper);
    environment_->define(decl->name, functionValue);
}

void Interpreter::visitClassDecl(ClassDecl* decl) {
    throw RuntimeError("Class declarations not yet implemented");
}

void Interpreter::visitExpressionDecl(ExpressionDecl* decl) {
    // Evaluate the expression and leave the result on the stack
    // This allows top-level expressions to return values
    decl->expression->accept(this);
}

// Function call implementation
Value Interpreter::callFunction(const ScriptDefinedFunction& function, const std::vector<Value>& args) {
    // Validate arguments
    validateFunctionArguments(function.parameters, args);
    
    // Create new environment for function execution
    auto previousEnv = environment_;
    environment_ = std::make_shared<Environment>(function.closureEnv ? function.closureEnv : environment_, stringSymbolizer_);
    
    // Store previous return state
    bool previousHasReturn = hasReturnValue_;
    Value previousReturn = returnValue_;
    hasReturnValue_ = false;
    
    try {
        // Bind parameters to arguments
        for (size_t i = 0; i < function.parameters.size(); ++i) {
            const auto& param = function.parameters[i];
            const auto& arg = args[i];
            
            // TODO: Handle reference parameters and type checking
            // For now, just bind the value
            environment_->define(param.name, arg);
        }
        
        // Execute function body
        function.body->accept(this);
        
        // Get return value
        Value result;
        if (hasReturnValue_) {
            result = returnValue_;
        } else {
            // If no return statement, return null
            result = Value();
        }
        
        // Restore previous state
        environment_ = previousEnv;
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        
        return result;
        
    } catch (...) {
        // Restore state on exception
        environment_ = previousEnv;
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        throw;
    }
}

void Interpreter::validateFunctionArguments(const std::vector<Parameter>& params, const std::vector<Value>& args) {
    if (params.size() != args.size()) {
        throw RuntimeError("Function expected " + std::to_string(params.size()) + 
                         " arguments but got " + std::to_string(args.size()));
    }
    
    // TODO: Add type checking for parameters
    // For now, we'll just check argument count
}

Value Interpreter::makeFunction(std::shared_ptr<ScriptDefinedFunction> func) {
    ScriptFunction wrapper = [this, func](const std::vector<Value>& args) -> Value {
        return callFunction(*func, args);
    };
    return Value::makeFunction(wrapper);
}

} // namespace JaiScript