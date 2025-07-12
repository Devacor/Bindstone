#include <jaiscript/jaiscript.hpp>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace jai {

// Define static method registries for built-in types
const std::unordered_map<std::string, interpreter::builtin_method> interpreter::arrayMethods_ = {
    {"size", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_array().size()));
    }},
    
    {"push", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("push() takes exactly one argument");
        }
        auto& arrayPtr = get_array_storage(self);
        arrayPtr->push_back(args[0].clone());  // Deep copy when pushing
        return interp->make_value();
    }},
    
    {"pop", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("pop() takes no arguments");
        }
        auto& arrayPtr = get_array_storage(self);
        if (arrayPtr->empty()) {
            throw runtime_error("Cannot pop from empty array");
        }
        script_value last = arrayPtr->back();
        arrayPtr->pop_back();
        return last;
    }},
    
    {"empty", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("empty() takes no arguments");
        }
        return interp->make_value(self.as_array().empty());
    }},
    
    {"clear", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("clear() takes no arguments");
        }
        auto& arrayPtr = get_array_storage(self);
        arrayPtr->clear();
        return interp->make_value();
    }},
    
    {"front", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("front() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            throw runtime_error("Cannot get front of empty array");
        }
        return arr.front();
    }},
    
    {"back", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("back() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            throw runtime_error("Cannot get back of empty array");
        }
        return arr.back();
    }}
};

const std::unordered_map<std::string, interpreter::builtin_method> interpreter::mapMethods_ = {
    {"size", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_map().size()));
    }},
    
    {"empty", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("empty() takes no arguments");
        }
        return interp->make_value(self.as_map().empty());
    }},
    
    {"clear", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("clear() takes no arguments");
        }
        auto& mapPtr = get_map_storage(self);
        mapPtr->clear();
        return interp->make_value();
    }},
    
    {"contains", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("contains() takes exactly one argument");
        }
        const auto& map = self.as_map();
        return interp->make_value(map.find(args[0]) != map.end());
    }},
    
    {"erase", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (args.size() != 1) {
            throw runtime_error("erase() takes exactly one argument");
        }
        auto& mapPtr = get_map_storage(self);
        mapPtr->erase(args[0]);
        return interp->make_value();
    }},
    
    {"keys", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("keys() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->engine_ref_);
        auto& arrayPtr = get_array_storage(result);
        arrayPtr->reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr->push_back(key.clone());
        }
        return result;
    }},
    
    {"values", [](interpreter* interp, const script_value& self, const std::vector<script_value>& args) -> script_value {
        if (!args.empty()) {
            throw runtime_error("values() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->engine_ref_);
        auto& arrayPtr = get_array_storage(result);
        arrayPtr->reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr->push_back(value.clone());
        }
        return result;
    }}
};

void environment::define(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    values_[id] = value;
}

void environment::define(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    values_[id] = std::move(value);
}

void environment::define(uint64_t id, const script_value& value) {
    values_[id] = value;
}

void environment::define(uint64_t id, script_value&& value) {
    values_[id] = std::move(value);
}

script_value environment::get(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get(name);
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

script_value environment::get(uint64_t id) const {
    return get(id, 0);
}

script_value environment::get(uint64_t id, int depth) const {
    // Prevent infinite recursion in environment chains
    const int MAX_RECURSION_DEPTH = 100;
    if (depth > MAX_RECURSION_DEPTH) {
        const std::string& name = symbolizer_->get_string(id);
        throw runtime_error("Maximum environment recursion depth exceeded for variable '" + name + "' at depth " + std::to_string(depth));
    }
    
    
    
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get(id, depth + 1);
    }
    
    // Need to get the name for error message
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    if (parent_) {
        parent_->assign(name, value);
        return;
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

const script_value& environment::get_ref(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(name);
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

const script_value& environment::get_ref(uint64_t id) const {
    return get_ref(id, 0);
}

const script_value& environment::get_ref(uint64_t id, int depth) const {
    // Prevent infinite recursion in environment chains
    const int MAX_RECURSION_DEPTH = 100;
    if (depth > MAX_RECURSION_DEPTH) {
        const std::string& name = symbolizer_->get_string(id);
        throw runtime_error("Maximum environment recursion depth exceeded for variable '" + name + "' at depth " + std::to_string(depth));
    }
    
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(id, depth + 1);
    }
    
    // Need to get the name for error message
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

script_value& environment::get_ref(const std::string& name) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(name);
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

script_value& environment::get_ref(uint64_t id) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }
    
    if (parent_) {
        return parent_->get_ref(id);
    }
    
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }
    
    if (parent_) {
        parent_->assign(name, std::move(value));
        return;
    }
    
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(uint64_t id, const script_value& value) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    if (parent_) {
        parent_->assign(id, value);
        return;
    }
    
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(uint64_t id, script_value&& value) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }
    
    if (parent_) {
        parent_->assign(id, std::move(value));
        return;
    }
    
    const std::string& name = symbolizer_->get_string(id);
    throw runtime_error("Undefined variable '" + name + "'");
}

bool environment::contains(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    if (values_.find(id) != values_.end()) {
        return true;
    }
    return parent_ ? parent_->contains(name) : false;
}

bool environment::contains(uint64_t id) const {
    if (values_.find(id) != values_.end()) {
        return true;
    }
    return parent_ ? parent_->contains(id) : false;
}

std::unordered_map<std::string, script_value> environment::get_local_variables() const {
    std::unordered_map<std::string, script_value> result;
    for (const auto& [id, value] : values_) {
        result[symbolizer_->get_string(id)] = value;
    }
    return result;
}

void environment::reset(std::shared_ptr<environment> new_parent) {
    values_.clear();
    parent_ = new_parent;
}

std::unordered_map<std::string, script_value> environment::get_all_variables() const {
    std::unordered_map<std::string, script_value> allVars;
    
    // Start with parent's variables (if any)
    if (parent_) {
        allVars = parent_->get_all_variables();
    }
    
    // Add/override with local variables
    for (const auto& [id, value] : values_) {
        const std::string& name = symbolizer_->get_string(id);
        allVars[name] = value;
    }
    
    return allVars;
}

script_value* environment::get_value_ptr(uint64_t id) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        return &it->second;
    }
    
    if (parent_) {
        return parent_->get_value_ptr(id);
    }
    
    return nullptr;
}

// interpreter implementation
interpreter::interpreter() 
    : ownedSymbolizer_(std::make_unique<string_symbolizer>()),
      string_symbolizer_(ownedSymbolizer_.get()),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.push_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }
    
    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.push_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }
    
    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer, std::shared_ptr<environment> global_env)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(global_env),
      hasReturnValue_(false) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.push_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }
    
    // Initialize binary operator dispatch table
    init_dispatch_table();
}

void interpreter::add_globals(const std::unordered_map<std::string, script_value>& globals) {
    for (const auto& [name, value] : globals) {
        environment_->define(name, value);
    }
}

void interpreter::add_global(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

void interpreter::prepare_for_execution() {
    // Clear execution state
    valueStack_.clear();
    returnValue_ = make_value();
    hasReturnValue_ = false;
    
    // Clear exception state
    current_exception_.reset();
    is_unwinding_ = false;
    active_exception_value_ = make_value();
    current_catch_var_.clear();
    
    // Reset to global scope but keep all variables defined at global scope
    // Only pop scopes if we're in a nested scope
    while (environment_->parent_) {
        environment_ = environment_->parent_;
    }
    // Note: We don't clear the global environment, so variables persist between executions
}

void interpreter::push_scope() {
    environment_ = std::make_shared<environment>(environment_, string_symbolizer_);
}

void interpreter::pop_scope() {
    if (environment_->parent_) {
        environment_ = environment_->parent_;
    }
}

void interpreter::define_variable(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

script_value interpreter::execute(const std::vector<declaration_ptr>& declarations) {
    script_value lastscript_value;
    hasReturnValue_ = false;  // Reset return value state
    
    for (size_t i = 0; i < declarations.size(); i++) {
        const auto& decl = declarations[i];
        
        // Execute declaration with exception handling
        try {
            decl->accept(this);
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
        }
        
        // Check if we're unwinding due to an uncaught exception
        if (is_unwinding_) {
            // Stop executing further declarations
            break;
        }
        
        // Check if this is an implicit return expression
        if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
            if (expr_decl->implicit_return && !valueStack_.empty()) {
                lastscript_value = pop_value();
            }
        }
        
        // Clear any remaining values on the stack (from non-implicit expressions)
        while (!valueStack_.empty()) {
            pop_value();
        }
        
        // If we hit a return statement, break out of execution
        if (hasReturnValue_) {
            reset_environment_pool();  // Reset pool for next execution
            return returnValue_;
        }
    }
    
    
    reset_environment_pool();  // Reset pool for next execution
    return lastscript_value;
}

script_value interpreter::evaluate(expression_ptr expr) {
    expr->accept(this);
    return pop_value();
}

// Variable access methods
script_value interpreter::get_variable(const std::string& name) const {
    return environment_->get(name).deref();
}

bool interpreter::has_variable(const std::string& name) const {
    return environment_->contains(name);
}

std::unordered_map<std::string, script_value> interpreter::get_all_variables() const {
    // Since we should be at root scope after execution, just return local variables
    return environment_->get_local_variables();
}


// expression visitors
void interpreter::visit_literal_expr(literal_expr* expr) {
    push_value(expr->value);
}

void interpreter::visit_identifier_expr(identifier_expr* expr) {
    // Check if this identifier is the current catch variable
    if (!current_catch_var_.empty() && expr->name == current_catch_var_) {
        push_value(active_exception_value_);
        return;
    }
    
    // Special handling for type constructors like weak_ptr<T>, shared_ptr<T>
    if (expr->name.find("weak_ptr<") == 0 || expr->name.find("shared_ptr<") == 0) {
        // This is a type constructor being used as a function
        // Extract the base type name (weak_ptr or shared_ptr)
        size_t pos = expr->name.find('<');
        std::string base_type = expr->name.substr(0, pos);
        
        // Look up the constructor function for this type
        try {
            script_value constructor_func = environment_->get(base_type);
            if (constructor_func.is_function()) {
                push_value(constructor_func);
                return;
            }
        } catch (const runtime_error&) {
            // Fall through to normal error handling
        }
    }
    
    // Use cached symbol ID if available, otherwise compute and cache it
    if (expr->symbol_id == UINT64_MAX) {
        expr->symbol_id = string_symbolizer_->intern(expr->name);
    }
    
    // Try to get the variable from environment
    try {
        const script_value& val = environment_->get_ref(expr->symbol_id);
        push_value(val.deref());  // Automatically handles references
    } catch (const runtime_error&) {
        // Variable not found - check if it's a member of 'this'
        try {
            script_value this_val = environment_->get("this");
            if (this_val.is_object()) {
                // Try to access as a member of 'this'
                auto obj_holder = std::get<std::shared_ptr<script_value::object_holder>>(this_val.storage_);
                if (obj_holder->is_cpp_class_instance) {
                    auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                    if (instance->has_field(expr->name)) {
                        push_value(instance->get_field(expr->name));
                        return;
                    }
                }
            }
        } catch (...) {
            // No 'this' in scope
        }
        
        // Re-throw the original error
        throw runtime_error("Undefined variable '" + expr->name + "'");
    }
}

void interpreter::visit_binary_expr(binary_expr* expr) {
    // ULTRA-FAST PATH: Literal expressions like "2 + 3" - avoid all AST traversal
    if (auto* leftLit = dynamic_cast<literal_expr*>(expr->left.get())) {
        if (auto* rightLit = dynamic_cast<literal_expr*>(expr->right.get())) {
            const script_value& leftVal = leftLit->value;
            const script_value& rightVal = rightLit->value;
            
            // Fast path for integer arithmetic (most common case) - but only if no custom ops
            if (leftVal.is_int() && rightVal.is_int() && can_use_fast_path(expr->op.type)) {
                script_int leftInt = leftVal.as_int();
                script_int rightInt = rightVal.as_int();
                
                switch (expr->op.type) {
                    case token_type::plus:
                        push_value(make_value(leftInt + rightInt));
                        return;
                    case token_type::minus:
                        push_value(make_value(leftInt - rightInt));
                        return;
                    case token_type::star:
                        push_value(make_value(leftInt * rightInt));
                        return;
                    case token_type::slash:
                        if (rightInt == 0) throw runtime_error("Division by zero");
                        push_value(make_value(leftInt / rightInt));
                        return;
                    case token_type::percent:
                        if (rightInt == 0) throw runtime_error("Division by zero");
                        push_value(make_value(leftInt % rightInt));
                        return;
                    case token_type::less:
                        push_value(make_value(leftInt < rightInt));
                        return;
                    case token_type::less_equal:
                        push_value(make_value(leftInt <= rightInt));
                        return;
                    case token_type::greater:
                        push_value(make_value(leftInt > rightInt));
                        return;
                    case token_type::greater_equal:
                        push_value(make_value(leftInt >= rightInt));
                        return;
                    case token_type::equal_equal:
                        push_value(make_value(leftInt == rightInt));
                        return;
                    case token_type::bang_equal:
                        push_value(make_value(leftInt != rightInt));
                        return;
                    case token_type::spaceship:
                        push_value(make_value(leftInt < rightInt ? script_int(-1) : (leftInt > rightInt ? script_int(1) : script_int(0))));
                        return;
                    default:
                        break; // Fall through to normal path
                }
            }
            // Fast path for float arithmetic - but only if no custom ops
            else if ((leftVal.is_float() || leftVal.is_int()) && (rightVal.is_float() || rightVal.is_int()) && can_use_fast_path(expr->op.type)) {
                script_float leftFloat = leftVal.is_int() ? static_cast<script_float>(leftVal.as_int()) : leftVal.as_float();
                script_float rightFloat = rightVal.is_int() ? static_cast<script_float>(rightVal.as_int()) : rightVal.as_float();
                
                switch (expr->op.type) {
                    case token_type::plus:
                        push_value(make_value(leftFloat + rightFloat));
                        return;
                    case token_type::minus:
                        push_value(make_value(leftFloat - rightFloat));
                        return;
                    case token_type::star:
                        push_value(make_value(leftFloat * rightFloat));
                        return;
                    case token_type::slash:
                        if (rightFloat == 0.0) throw runtime_error("Division by zero");
                        push_value(make_value(leftFloat / rightFloat));
                        return;
                    case token_type::percent:
                        if (rightFloat == 0.0) throw runtime_error("Division by zero");
                        push_value(make_value(std::fmod(leftFloat, rightFloat)));
                        return;
                    case token_type::less:
                        push_value(make_value(leftFloat < rightFloat));
                        return;
                    case token_type::less_equal:
                        push_value(make_value(leftFloat <= rightFloat));
                        return;
                    case token_type::greater:
                        push_value(make_value(leftFloat > rightFloat));
                        return;
                    case token_type::greater_equal:
                        push_value(make_value(leftFloat >= rightFloat));
                        return;
                    case token_type::equal_equal:
                        push_value(make_value(leftFloat == rightFloat));
                        return;
                    case token_type::bang_equal:
                        push_value(make_value(leftFloat != rightFloat));
                        return;
                    case token_type::spaceship:
                        push_value(make_value(leftFloat < rightFloat ? script_int(-1) : (leftFloat > rightFloat ? script_int(1) : script_int(0))));
                        return;
                    default:
                        break;
                }
            }
            // Fast path for string concatenation
            else if (expr->op.type == token_type::plus && leftVal.is_string() && rightVal.is_string()) {
                push_value(make_value(leftVal.as_string() + rightVal.as_string()));
                return;
            }
        }
    }

    // Handle logical operators specially for short-circuit evaluation
    if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
        expr->left->accept(this);
        script_value left = pop_value();
        
        bool leftTruthy = is_truthy(left);
        
        if (expr->op.type == token_type::ampersand_ampersand) {
            if (!leftTruthy) {
                push_value(left);  // Short-circuit: return left (falsy)
                return;
            }
        } else { // pipe_pipe
            if (leftTruthy) {
                push_value(left);  // Short-circuit: return left (truthy)
                return;
            }
        }
        
        // Evaluate right side
        expr->right->accept(this);
        // Result is already on stack
        return;
    }
    
    // Evaluate operands once and use them throughout
    expr->left->accept(this);
    script_value left = pop_value().deref();  // Handle references safely
    
    expr->right->accept(this);
    // Check if we're unwinding due to an exception in the right expression
    if (is_unwinding_) {
        // Don't try to pop a value that wasn't pushed due to the exception
        return;
    }
    script_value right = pop_value().deref();  // Handle references safely
    
    // Check for custom operator functions first
    std::string opName;
    switch (expr->op.type) {
        case token_type::plus: opName = "+"; break;
        case token_type::minus: opName = "-"; break;
        case token_type::star: opName = "*"; break;
        case token_type::slash: opName = "/"; break;
        case token_type::percent: opName = "%"; break;
        case token_type::less: opName = "<"; break;
        case token_type::less_equal: opName = "<="; break;
        case token_type::greater: opName = ">"; break;
        case token_type::greater_equal: opName = ">="; break;
        case token_type::equal_equal: opName = "=="; break;
        case token_type::bang_equal: opName = "!="; break;
        case token_type::spaceship: opName = "<=>"; break;
        case token_type::ampersand: opName = "&"; break;
        case token_type::pipe: opName = "|"; break;
        case token_type::caret: opName = "^"; break;
        case token_type::left_shift: opName = "<<"; break;
        case token_type::right_shift: opName = ">>"; break;
        default: break;
    }
    
    // Check for custom operator function (excluding subscript)
    if (!opName.empty() && environment_ && environment_->contains(opName)) {
        try {
            script_value opFunc = environment_->get(opName);
            if (opFunc.is_function()) {
                const script_function& func = opFunc.as_function();
                std::vector<script_value> args = {left, right};
                push_value(func(args));
                return;
            }
        } catch (const std::exception& e) {
            std::string error = e.what();
            if (error.find("Undefined variable") == std::string::npos) {
                throw;
            }
        }
    }
    
    // Handle subscript operation specially
    if (expr->op.type == token_type::left_bracket) {
        if (left.is_array()) {
            if (!right.is_int()) {
                throw runtime_error("Array index must be an integer");
            }
            script_int index = right.as_int();
            auto& array = const_cast<std::vector<script_value>&>(left.as_array());
            
            if (index < 0 || index >= static_cast<script_int>(array.size())) {
                throw runtime_error("Array index out of bounds: " + std::to_string(index));
            }
            
            // Create a reference to the array element for assignment support
            script_value* element_ptr = &array[index];
            script_value ref_value = script_value::make_reference(element_ptr, environment_);
            push_value(ref_value);
        } else if (left.is_map()) {
            // Get non-const reference to map for operator[]
            auto& map = const_cast<std::map<script_value, script_value>&>(left.as_map());
            
            // Use operator[] which creates the element if it doesn't exist
            // This returns a reference to the value
            script_value& value_ref = map[right];
            
            // Create a reference to the map element for assignment support
            script_value* element_ptr = &value_ref;
            script_value ref_value = script_value::make_reference(element_ptr, environment_);
            push_value(ref_value);
        } else {
            if (left.is_object()) {
                try {
                    script_value getMethod = environment_->get("[]");
                    if (getMethod.is_function()) {
                        const script_function& func = getMethod.as_function();
                        std::vector<script_value> args = {left, right};
                        push_value(func(args));
                        return;
                    }
                } catch (const std::exception&) {
                    // No custom [] operator, continue with error
                }
            }
            throw runtime_error("Subscript can only be used on arrays, maps, or types with [] operator");
        }
        return;
    }
    
    // Use dispatch table for built-in operators with already-evaluated operands
    auto handler = binary_dispatch_table_.find(expr->op.type);
    if (handler != binary_dispatch_table_.end()) {
        script_value result = (this->*handler->second)(left, right);
        push_value(result);
    } else {
        throw runtime_error("Unknown binary operator");
    }
}
void interpreter::visit_unary_expr(unary_expr* expr) {
    // Fast path for literal unary operations
    if (auto* literal = dynamic_cast<literal_expr*>(expr->operand.get())) {
        const script_value& val = literal->value;
        
        switch (expr->op.type) {
            case token_type::minus:
                if (val.is_int()) {
                    push_value(make_value(-val.as_int()));
                    return;
                } else if (val.is_float()) {
                    push_value(make_value(-val.as_float()));
                    return;
                }
                break;
            case token_type::bang:
                push_value(make_value(!is_truthy(val)));
                return;
            case token_type::tilde:
                if (val.is_int()) {
                    push_value(make_value(~val.as_int()));
                    return;
                }
                break;
            default:
                break; // Fall through to generic path for increment/decrement
        }
    }
    
    // Generic path - evaluate operand and use existing logic
    expr->operand->accept(this);
    script_value operand = pop_value();
    
    switch (expr->op.type) {
        case token_type::minus:
            if (operand.is_int()) {
                push_value(make_value(-operand.as_int()));
            } else if (operand.is_float()) {
                push_value(make_value(-operand.as_float()));
            } else {
                throw runtime_error("Unary minus requires numeric operand");
            }
            break;
            
        case token_type::bang:
            push_value(make_value(!is_truthy(operand)));
            break;
            
        case token_type::tilde:
            // Bitwise NOT
            if (!operand.is_int()) {
                throw runtime_error("Bitwise NOT requires integer operand");
            }
            push_value(make_value(~operand.as_int()));
            break;
            
        case token_type::plus_plus:
        case token_type::minus_minus: {
            // Handle increment/decrement
            if (auto* identifier = dynamic_cast<identifier_expr*>(expr->operand.get())) {
                // Cache symbol ID if not already cached
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }
                script_value currentValue = environment_->get(identifier->symbol_id);
                script_value newValue;
                
                if (currentValue.is_int()) {
                    int64_t val = currentValue.as_int();
                    if (expr->op.type == token_type::plus_plus) {
                        newValue = make_value(val + 1);
                    } else {
                        newValue = make_value(val - 1);
                    }
                } else if (currentValue.is_float()) {
                    double val = currentValue.as_float();
                    if (expr->op.type == token_type::plus_plus) {
                        newValue = make_value(val + 1.0);
                    } else {
                        newValue = make_value(val - 1.0);
                    }
                } else {
                    throw runtime_error("Cannot increment/decrement non-numeric value");
                }
                
                // Check if this is a reference variable
                script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
                if (varPtr && varPtr->is_reference()) {
                    // This is a reference - update the target
                    varPtr->deref() = newValue.deref();
                } else {
                    // Regular variable assignment
                    environment_->assign(identifier->symbol_id, newValue);
                }
                
                // For prefix, return the new value; for postfix, return the old value
                if (expr->is_postfix) {
                    push_value(std::move(currentValue));
                } else {
                    push_value(std::move(newValue));
                }
            } else {
                throw runtime_error("Increment/decrement requires a variable");
            }
            break;
        }
            
        default:
            throw runtime_error("Unsupported unary operator");
    }
}

void interpreter::visit_assignment_expr(assignment_expr* expr) {
    
    // For compound assignment operators, we need the current value
    if (expr->op.type != token_type::equal) {
        // Get current value of the target
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }
            script_value currentValue = environment_->get(identifier->symbol_id);
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            script_value rightValue = pop_value();
            
            // Perform the compound operation - try custom operators first, then built-in types
            script_value resultValue;
            bool customOpFound = false;
            
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    // Try custom + operator first
                    if (environment_ && environment_->contains("+")) {
                        try {
                            script_value opFunc = environment_->get("+");
                            if (opFunc.is_function()) {
                                const script_function& func = opFunc.as_function();
                                std::vector<script_value> args = {currentValue, rightValue};
                                resultValue = func(args);
                                customOpFound = true;
                            }
                        } catch (const std::exception&) {
                            // Custom operator failed, try built-in
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() + rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() + rightValue.as_float());
                        } else if (currentValue.is_string() && rightValue.is_string()) {
                            resultValue = make_value(currentValue.as_string() + rightValue.as_string());
                        } else {
                            throw runtime_error("Invalid operands for +=");
                        }
                    }
                    break;
                }
                    
                case token_type::minus_equal: {
                    // Try custom - operator first
                    customOpFound = false;
                    if (environment_ && environment_->contains("-")) {
                        try {
                            script_value opFunc = environment_->get("-");
                            if (opFunc.is_function()) {
                                const script_function& func = opFunc.as_function();
                                std::vector<script_value> args = {currentValue, rightValue};
                                resultValue = func(args);
                                customOpFound = true;
                            }
                        } catch (const std::exception&) {
                            // Custom operator failed, try built-in
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() - rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() - rightValue.as_float());
                        } else {
                            throw runtime_error("Invalid operands for -=");
                        }
                    }
                    break;
                }
                    
                case token_type::star_equal: {
                    // Try custom * operator first
                    customOpFound = false;
                    if (environment_ && environment_->contains("*")) {
                        try {
                            script_value opFunc = environment_->get("*");
                            if (opFunc.is_function()) {
                                const script_function& func = opFunc.as_function();
                                std::vector<script_value> args = {currentValue, rightValue};
                                resultValue = func(args);
                                customOpFound = true;
                            }
                        } catch (const std::exception&) {
                            // Custom operator failed, try built-in
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() * rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() * rightValue.as_float());
                        } else {
                            throw runtime_error("Invalid operands for *=");
                        }
                    }
                    break;
                }
                    
                case token_type::slash_equal:
                    if (rightValue.is_int() && rightValue.as_int() == 0) {
                        throw runtime_error("Division by zero");
                    }
                    if (rightValue.is_float() && rightValue.as_float() == 0.0) {
                        throw runtime_error("Division by zero");
                    }
                    
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() / rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() / rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for /=");
                    }
                    break;
                    
                default:
                    throw runtime_error("Unsupported compound assignment operator");
            }
            
            // Check if this is a reference variable
            script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
            if (varPtr && varPtr->is_reference()) {
                // This is a reference - update the target (deep copy)
                varPtr->deref() = resultValue.deref().clone();
            } else {
                // Regular assignment (deep copy the result)
                environment_->assign(identifier->symbol_id, resultValue.clone());
            }
            push_value(resultValue);
        } else if (auto* memberExpr = dynamic_cast<member_expr*>(expr->target.get())) {
            // Handle compound assignment to member expression (e.g., obj.value += 10)
            // First, get the current value of the property
            memberExpr->accept(this);
            script_value currentValue = pop_value();
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue;
            bool customOpFound = false;
            
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    if (!customOpFound) {
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() + rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() + rightValue.as_float());
                        } else if (currentValue.is_string() && rightValue.is_string()) {
                            resultValue = make_value(currentValue.as_string() + rightValue.as_string());
                        } else {
                            throw runtime_error("Invalid operands for +=");
                        }
                    }
                    break;
                }
                case token_type::minus_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() - rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() - rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for -=");
                    }
                    break;
                }
                case token_type::star_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() * rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() * rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for *=");
                    }
                    break;
                }
                case token_type::slash_equal: {
                    if (rightValue.is_int() && rightValue.as_int() == 0) {
                        throw runtime_error("Division by zero");
                    } else if (rightValue.is_float() && rightValue.as_float() == 0.0) {
                        throw runtime_error("Division by zero");
                    }
                    if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() / rightValue.as_float());
                    } else {
                        throw runtime_error("Invalid operands for /=");
                    }
                    break;
                }
                default:
                    throw runtime_error("Unsupported compound assignment operator");
            }
            
            // Now assign the result back to the property
            // We need to evaluate the object again to get a fresh reference
            memberExpr->object->accept(this);
            script_value objectValue = pop_value();
            
            // Check if it's an object
            if (!objectValue.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            // Extract the class_instance
            auto objHolder = std::get<std::shared_ptr<script_value::object_holder>>(objectValue.storage_);
            auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
            
            // Check if there's a property setter
            script_value setter = instance->get_method("_set_" + memberExpr->member);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {objectValue, resultValue.clone()};
                func(args);
            } else if (instance->has_field(memberExpr->member)) {
                // Direct field assignment (deep copy)
                instance->set_field(memberExpr->member, resultValue.clone());
            } else {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to non-existent member '" + memberExpr->member + "'");
                current_exception_ = script_exception("Cannot assign to non-existent member '" + memberExpr->member + "'", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            push_value(std::move(resultValue));
        } else {
            // General compound assignment for any expression
            // This handles subscripts, function calls that return references, etc.
            
            // First, evaluate the target expression to get current value
            expr->target->accept(this);
            script_value currentValue = pop_value();
            
            // Evaluate the right-hand side
            expr->value->accept(this);
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue;
            
            // Try custom operators first
            try {
                script_value opFunc = environment_->get(std::string(1, expr->op.lexeme[0]));
                if (opFunc.is_function()) {
                    const script_function& func = opFunc.as_function();
                    std::vector<script_value> args = {currentValue, rightValue};
                    resultValue = func(args);
                } else {
                    throw runtime_error("Not a function");
                }
            } catch (const std::exception&) {
                // Fall back to built-in operators
                switch (expr->op.type) {
                    case token_type::plus_equal:
                        if (currentValue.is_string() || rightValue.is_string()) {
                            resultValue = make_value(currentValue.to_string() + rightValue.to_string());
                        } else {
                            resultValue = evaluate_arithmetic(currentValue, token_type::plus, rightValue);
                        }
                        break;
                    case token_type::minus_equal:
                        resultValue = evaluate_arithmetic(currentValue, token_type::minus, rightValue);
                        break;
                    case token_type::star_equal:
                        resultValue = evaluate_arithmetic(currentValue, token_type::star, rightValue);
                        break;
                    case token_type::slash_equal:
                        if ((rightValue.is_int() && rightValue.as_int() == 0) ||
                            (rightValue.is_float() && rightValue.as_float() == 0.0)) {
                            throw runtime_error("Division by zero");
                        }
                        resultValue = evaluate_arithmetic(currentValue, token_type::slash, rightValue);
                        break;
                    case token_type::percent_equal:
                        if (rightValue.is_int() && rightValue.as_int() == 0) {
                            throw runtime_error("Modulo by zero");
                        }
                        resultValue = evaluate_arithmetic(currentValue, token_type::percent, rightValue);
                        break;
                    default:
                        throw runtime_error("Unknown compound assignment operator");
                }
            }
            
            // Now create a regular assignment and execute it
            auto regularAssignment = std::make_shared<assignment_expr>(
                expr->location,
                expr->target,
                token(token_type::equal, "=", expr->op.location),
                std::make_shared<literal_expr>(expr->location, resultValue)
            );
            regularAssignment->accept(this);
        }
    } else {
        // Regular assignment
        expr->value->accept(this);
        // Check if we're unwinding due to an exception in the value expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return;
        }
        script_value value = pop_value();
        
        
        // Check if target is an identifier
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }
            // Get the current value to check if it's a reference
            if (environment_->contains(identifier->symbol_id)) {
                script_value* currentVal = environment_->get_value_ptr(identifier->symbol_id);
                if (currentVal && currentVal->is_reference()) {
                    // This is a reference - assign through it (deep copy the value)
                    currentVal->deref() = value.deref().clone();
                } else {
                    // Regular variable assignment (deep copy the value)
                    environment_->assign(identifier->symbol_id, value.clone());
                }
            } else {
                // Variable doesn't exist - check if it's a member of 'this'
                bool assigned_to_this = false;
                try {
                    script_value this_val = environment_->get("this");
                    if (this_val.is_object()) {
                        auto obj_holder = std::get<std::shared_ptr<script_value::object_holder>>(this_val.storage_);
                        if (obj_holder->is_cpp_class_instance) {
                            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                            if (instance->has_field(identifier->name)) {
                                instance->set_field(identifier->name, value.clone());
                                assigned_to_this = true;
                            }
                        }
                    }
                } catch (...) {
                    // No 'this' in scope
                }
                
                if (!assigned_to_this) {
                    throw runtime_error("Undefined variable '" + identifier->name + "'");
                }
            }
            push_value(std::move(value));  // Assignment expressions return the assigned value
        } 
        // Check if target is a member expression (property assignment)
        else if (auto* memberExpr = dynamic_cast<member_expr*>(expr->target.get())) {
            // Evaluate the object
            memberExpr->object->accept(this);
            script_value objectValue = pop_value();
            
            // Check if it's an object
            if (!objectValue.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            // Extract the class_instance
            auto objHolder = std::get<std::shared_ptr<script_value::object_holder>>(objectValue.storage_);
            auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
            
            // Check if there's a property setter
            script_value setter = instance->get_method("_set_" + memberExpr->member);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {objectValue, value};
                func(args);
            } else if (instance->has_field(memberExpr->member)) {
                // Direct field assignment (deep copy)
                instance->set_field(memberExpr->member, value.clone());
            } else {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to non-existent member '" + memberExpr->member + "'");
                current_exception_ = script_exception("Cannot assign to non-existent member '" + memberExpr->member + "'", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return;
            }
            
            push_value(std::move(value));  // Assignment expressions return the assigned value
        }
        // Check if target is a subscript expression (array[index] or map[key])
        else if (auto* binaryExpr = dynamic_cast<binary_expr*>(expr->target.get())) {
            if (binaryExpr->op.type == token_type::left_bracket) {
                // Evaluate the entire target expression (e.g., nested["nums"][1])
                // This should return a reference if it's a valid lvalue
                expr->target->accept(this);
                script_value target_ref = pop_value();
                
                // Check if we got a reference
                if (target_ref.is_reference()) {
                    // Get the actual target through the reference
                    auto refHolder = std::get<std::shared_ptr<script_value::reference_holder>>(target_ref.storage_);
                    script_value* target_ptr = refHolder->target;
                    if (!target_ptr) {
                        throw runtime_error("Invalid reference in assignment");
                    }
                    
                    // Assign the value
                    *target_ptr = value.clone();
                    push_value(std::move(value));  // Assignment expressions return the assigned value
                } else {
                    // Not a reference - this means the subscript expression didn't
                    // return an lvalue (e.g., trying to assign to a function call result)
                    throw runtime_error("Cannot assign to rvalue expression");
                }
            } else {
                throw runtime_error("Complex assignment targets not yet implemented");
            }
        } else {
            throw runtime_error("Complex assignment targets not yet implemented");
        }
    }
}

// statement visitors
void interpreter::visit_expression_stmt(expression_stmt* stmt) {
    
    // Check if it's an assignment
    if (auto* assign = dynamic_cast<assignment_expr*>(stmt->expression.get())) {
    }
    
    stmt->expression->accept(this);
    
    // Early exit if exception is propagating
    if (is_unwinding_) return;
    
    // For top-level expressions (wrapped in expression_decl), we want to keep the value
    // The execute() method will handle popping it
}

void interpreter::visit_block_stmt(block_stmt* stmt) {
    // Create new environment for the block scope
    auto previous = environment_;
    environment_ = std::make_shared<environment>(environment_, string_symbolizer_);
    
    try {
        for (const auto& decl : stmt->declarations) {
            decl->accept(this);
            
            // Early exit if exception is propagating
            if (is_unwinding_) break;
        }
    } catch (...) {
        // Restore environment even if an error occurs
        environment_ = previous;
        throw;
    }
    
    // Restore previous environment
    environment_ = previous;
}

void interpreter::visit_variable_decl(variable_decl* decl) {
    // Check if this is a reference variable declaration
    bool is_reference = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_reference_type) {
        is_reference = true;
    }
    
    // Check if this is a weak_ptr declaration
    bool is_weak_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_weak_ptr_type) {
        is_weak_ptr = true;
    }
    
    if (is_weak_ptr) {
        // weak_ptr<T> variable - convert initializer to weak_ptr
        if (!decl->initializer) {
            // No initializer - create empty weak_ptr
            script_value null_weak;
            null_weak.type_info_ = decl->type;
            null_weak.storage_ = std::weak_ptr<script_value>();
            environment_->define(decl->name, std::move(null_weak));
        } else {
            // Evaluate initializer and wrap in weak_ptr
            decl->initializer->accept(this);
            script_value value = pop_value();
            
            // Call weak_ptr function
            script_value weak = script_value::make_weak_ptr(value);
            environment_->define(decl->name, std::move(weak));
        }
    } else if (is_reference) {
        // Reference variable - must have initializer
        if (!decl->initializer) {
            throw runtime_error("Reference variable '" + decl->name + "' must be initialized");
        }
        
        // Check if initializer is an identifier (can take reference)
        if (auto identExpr = dynamic_cast<identifier_expr*>(decl->initializer.get())) {
            // Get the target variable's address
            uint64_t targetSymbolId = string_symbolizer_->intern(identExpr->name);
            
            // Get a pointer to the target value in the environment
            // This is safe because environment uses unordered_map which doesn't invalidate pointers
            script_value* targetPtr = environment_->get_value_ptr(targetSymbolId);
            if (!targetPtr) {
                throw runtime_error("Cannot take reference of undefined variable '" + identExpr->name + "'");
            }
            
            // Check if the target is itself a reference
            if (targetPtr->is_reference()) {
                // Reference to reference - get the final target and its environment
                auto refHolder = std::get<std::shared_ptr<script_value::reference_holder>>(targetPtr->storage_);
                targetPtr = refHolder->target;
                // Use the original reference's environment
                auto target_env = refHolder->sourceEnv.lock();
                if (!target_env) {
                    throw runtime_error("Reference target environment has been destroyed");
                }
                script_value refValue = script_value::make_reference(targetPtr, target_env);
                environment_->define(decl->name, std::move(refValue));
            } else {
                // Regular reference - use current environment
                script_value refValue = script_value::make_reference(targetPtr, environment_);
                environment_->define(decl->name, std::move(refValue));
            }
        } else {
            // For other expressions, evaluate them and check if they return a reference
            decl->initializer->accept(this);
            script_value result = pop_value();
            
            // If the result is a reference, we can create a reference to its target
            if (result.is_reference()) {
                auto refHolder = std::get<std::shared_ptr<script_value::reference_holder>>(result.storage_);
                script_value* targetPtr = refHolder->target;
                auto target_env = refHolder->sourceEnv.lock();
                if (!target_env) {
                    throw runtime_error("Reference target environment has been destroyed");
                }
                // Create a new reference to the same target
                script_value refValue = script_value::make_reference(targetPtr, target_env);
                environment_->define(decl->name, std::move(refValue));
            } else {
                throw runtime_error("Cannot take reference of non-lvalue expression");
            }
        }
    } else {
        // Regular variable declaration
        script_value value;
        if (decl->initializer) {
            decl->initializer->accept(this);
            value = pop_value();
            // Clone the value for variable declaration with initializer
            // Note: clone() now automatically dereferences references
            value = value.clone();
        }
        // If no initializer, value remains null
        
        environment_->define(decl->name, std::move(value));
    }
}

// Binary operation helpers
script_value interpreter::evaluate_arithmetic(const script_value& left, token_type op, const script_value& right) {
    // Special case for string concatenation
    if (op == token_type::plus && (left.is_string() || right.is_string())) {
        return make_value(left.to_string() + right.to_string());
    }
    
    // Fast path for pure integer arithmetic (avoid float conversion)
    if (left.is_int() && right.is_int()) {
        script_int leftInt = left.as_int();
        script_int rightInt = right.as_int();
        
        switch (op) {
            case token_type::plus:
                return make_value(leftInt + rightInt);
            case token_type::minus:
                return make_value(leftInt - rightInt);
            case token_type::star:
                return make_value(leftInt * rightInt);
            case token_type::slash:
                if (rightInt == 0) {
                    throw runtime_error("Division by zero");
                }
                // Integer division returns integer (C++ semantics)
                return make_value(leftInt / rightInt);
            case token_type::percent:
                if (rightInt == 0) {
                    throw runtime_error("Division by zero");
                }
                return make_value(leftInt % rightInt);
            default:
                throw runtime_error("Unknown arithmetic operator");
        }
    }
    
    // Mixed or floating point arithmetic path
    script_float leftNum, rightNum;
    
    if (left.is_int()) {
        leftNum = static_cast<script_float>(left.as_int());
    } else if (left.is_float()) {
        leftNum = left.as_float();
    } else {
        throw runtime_error("Left operand must be numeric");
    }
    
    if (right.is_int()) {
        rightNum = static_cast<script_float>(right.as_int());
    } else if (right.is_float()) {
        rightNum = right.as_float();
    } else {
        throw runtime_error("Right operand must be numeric");
    }
    
    switch (op) {
        case token_type::plus:
            return make_value(leftNum + rightNum);
        case token_type::minus:
            return make_value(leftNum - rightNum);
        case token_type::star:
            return make_value(leftNum * rightNum);
        case token_type::slash:
            if (rightNum == 0.0) {
                throw runtime_error("Division by zero");
            }
            return make_value(leftNum / rightNum);
        case token_type::percent:
            if (rightNum == 0.0) {
                throw runtime_error("Division by zero");
            }
            return make_value(std::fmod(leftNum, rightNum));
        default:
            throw runtime_error("Unknown arithmetic operator");
    }
}

script_value interpreter::evaluate_comparison(const script_value& left, token_type op, const script_value& right) {
    // Handle weak_ptr comparisons with null
    if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
        if (op == token_type::equal_equal || op == token_type::bang_equal) {
            // For weak_ptr, null comparison checks if expired
            bool is_expired = false;
            if (left.is_weak_ptr()) {
                if (std::holds_alternative<std::weak_ptr<script_value>>(left.storage_)) {
                    auto weak_ptr = std::get<std::weak_ptr<script_value>>(left.storage_);
                    // Check if weak_ptr is expired (includes default-constructed)
                    is_expired = weak_ptr.expired();
                } else if (std::holds_alternative<std::shared_ptr<script_value::object_holder>>(left.storage_)) {
                    // weak_ptr_holder type - check if it contains an actual value
                    auto holder = std::get<std::shared_ptr<script_value::object_holder>>(left.storage_);
                    is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
                } else {
                    // Other cases - consider expired
                    is_expired = true;
                }
            } else {
                // right is weak_ptr
                if (std::holds_alternative<std::weak_ptr<script_value>>(right.storage_)) {
                    auto weak_ptr = std::get<std::weak_ptr<script_value>>(right.storage_);
                    // Check if weak_ptr is expired (includes default-constructed)
                    is_expired = weak_ptr.expired();
                } else if (std::holds_alternative<std::shared_ptr<script_value::object_holder>>(right.storage_)) {
                    // weak_ptr_holder type - check if it contains an actual value
                    auto holder = std::get<std::shared_ptr<script_value::object_holder>>(right.storage_);
                    is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
                } else {
                    // Other cases - consider expired
                    is_expired = true;
                }
            }
            
            if (op == token_type::equal_equal) {
                return make_value(is_expired);  // weak == null is true if expired
            } else {
                return make_value(!is_expired); // weak != null is true if not expired
            }
        }
    }
    
    // Handle null comparisons
    if (left.is_null() || right.is_null()) {
        switch (op) {
            case token_type::equal_equal:
                return make_value(left.is_null() && right.is_null());
            case token_type::bang_equal:
                return make_value(!(left.is_null() && right.is_null()));
            default:
                throw runtime_error("Cannot compare null values with relational operators");
        }
    }
    
    // For now, only support numeric and string comparisons
    if (left.is_string() && right.is_string()) {
        const auto& leftStr = left.as_string();
        const auto& rightStr = right.as_string();
        
        switch (op) {
            case token_type::less:
                return make_value(leftStr < rightStr);
            case token_type::less_equal:
                return make_value(leftStr <= rightStr);
            case token_type::greater:
                return make_value(leftStr > rightStr);
            case token_type::greater_equal:
                return make_value(leftStr >= rightStr);
            case token_type::equal_equal:
                return make_value(leftStr == rightStr);
            case token_type::bang_equal:
                return make_value(leftStr != rightStr);
            case token_type::spaceship: {
                // Three-way comparison for strings
                int cmp = leftStr.compare(rightStr);
                return make_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
            }
            default:
                throw runtime_error("Unknown comparison operator");
        }
    }
    
    // Numeric comparison
    script_float leftNum = to_numeric(left).as_float();
    script_float rightNum = to_numeric(right).as_float();
    
    switch (op) {
        case token_type::less:
            return make_value(leftNum < rightNum);
        case token_type::less_equal:
            return make_value(leftNum <= rightNum);
        case token_type::greater:
            return make_value(leftNum > rightNum);
        case token_type::greater_equal:
            return make_value(leftNum >= rightNum);
        case token_type::equal_equal:
            return make_value(leftNum == rightNum);
        case token_type::bang_equal:
            return make_value(leftNum != rightNum);
        case token_type::spaceship: {
            // Three-way comparison for numbers
            // Return -1 if less, 0 if equal, 1 if greater
            if (leftNum < rightNum) return make_value(script_int(-1));
            else if (leftNum > rightNum) return make_value(script_int(1));
            else return make_value(script_int(0));
        }
        default:
            throw runtime_error("Unknown comparison operator");
    }
}

script_value interpreter::evaluate_logical(const script_value& left, token_type op, const script_value& right) {
    bool leftTruthy = is_truthy(left);
    
    switch (op) {
        case token_type::ampersand_ampersand:
            // Short-circuit: if left is false, return left
            if (!leftTruthy) {
                return left;
            }
            return right;
            
        case token_type::pipe_pipe:
            // Short-circuit: if left is true, return left
            if (leftTruthy) {
                return left;
            }
            return right;
            
        default:
            throw runtime_error("Unknown logical operator");
    }
}

script_value interpreter::evaluate_bitwise(const script_value& left, token_type op, const script_value& right) {
    // Bitwise operations only work on integers
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Bitwise operations require integer operands");
    }
    
    script_int leftInt = left.as_int();
    script_int rightInt = right.as_int();
    
    switch (op) {
        case token_type::ampersand:
            return make_value(leftInt & rightInt);
        case token_type::pipe:
            return make_value(leftInt | rightInt);
        case token_type::caret:
            return make_value(leftInt ^ rightInt);
        case token_type::left_shift:
            return make_value(leftInt << rightInt);
        case token_type::right_shift:
            return make_value(leftInt >> rightInt);
        default:
            throw runtime_error("Unknown bitwise operator");
    }
}


// Placeholder implementations for remaining visitors
void interpreter::visit_call_expr(call_expr* expr) {
    // Evaluate the callee expression
    expr->callee->accept(this);
    script_value callee = pop_value();
    
    // Check if the callee is a function
    if (!callee.is_function()) {
        throw runtime_error("Cannot call non-function value");
    }
    
    // Use a local vector for arguments to avoid issues with nested calls
    std::vector<script_value> arguments;
    arguments.reserve(expr->arguments.size());
    
    // Also track argument metadata for reference parameters
    std::vector<std::pair<uint64_t, std::shared_ptr<environment>>> argMetadata;
    argMetadata.reserve(expr->arguments.size());
    
    for (const auto& argExpr : expr->arguments) {
        // Check if this is a simple identifier (needed for references)
        if (auto identExpr = dynamic_cast<identifier_expr*>(argExpr.get())) {
            // Get the symbol ID for this variable
            uint64_t symbol_id = string_symbolizer_->intern(identExpr->name);
            argMetadata.emplace_back(symbol_id, environment_);
        } else {
            // Not an identifier - can't take reference
            argMetadata.emplace_back(UINT64_MAX, nullptr);
        }
        
        // Evaluate argument with exception handling
        try {
            argExpr->accept(this);
            arguments.emplace_back(std::move(pop_value()));
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return;
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return;
        }
    }
    
    // Store argument metadata in a member variable so call_function can access it
    current_arg_metadata_ = std::move(argMetadata);
    
    // Call the function with C++ exception handling
    const script_function& func = callee.as_function();
    script_value result;
    
    try {
        result = func(arguments);
    } catch (const script_exception& e) {
        // Convert script exceptions to interpreter exception state
        current_arg_metadata_.clear();
        active_exception_value_ = make_value(e.what());
        current_exception_ = e;
        is_unwinding_ = true;
        push_value(make_value());  // Push a null value since the call failed
        return;
    } catch (const std::runtime_error& e) {
        // Wrap C++ runtime_error with message and trigger exception handling
        current_arg_metadata_.clear();
        active_exception_value_ = make_value(e.what());
        current_exception_ = script_exception(e.what());
        is_unwinding_ = true;
        push_value(make_value());  // Push a null value since the call failed
        return;
    } catch (const std::exception& e) {
        // Other C++ exceptions get the generic message
        current_arg_metadata_.clear();
        active_exception_value_ = make_value("Unbound exception type caught in JaiScript.");
        current_exception_ = script_exception("Unbound exception type caught in JaiScript.");
        is_unwinding_ = true;
        push_value(make_value());  // Push a null value since the call failed
        return;
    }
    
    // Clear argument metadata
    current_arg_metadata_.clear();
    
    // Push result onto the stack
    push_value(result);
}

void interpreter::visit_member_expr(member_expr* expr) {
    // Evaluate the object expression
    expr->object->accept(this);
    script_value objectValue = pop_value();
    
    // Dereference if needed - subscript access returns references
    objectValue = objectValue.deref();
    
    // Handle array methods
    if (objectValue.is_array()) {
        auto methodIt = arrayMethods_.find(expr->member);
        if (methodIt != arrayMethods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;
            
            // Create a wrapper function that captures the array value and method
            script_function boundMethod = [this, objectValue, method](const std::vector<script_value>& args) -> script_value {
                return method(this, objectValue, args);
            };
            
            push_value(script_value::make_function(boundMethod, engine_ref_));
            return;
        }
        else {
            // Set exception state instead of throwing
            active_exception_value_ = make_value("Array has no method '" + expr->member + "'");
            current_exception_ = script_exception("Array has no method '" + expr->member + "'", expr->location);
            is_unwinding_ = true;
            push_value(make_value());
            return;
        }
    }
    
    // Handle map methods
    if (objectValue.is_map()) {
        auto methodIt = mapMethods_.find(expr->member);
        if (methodIt != mapMethods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;
            
            // Create a wrapper function that captures the map value and method
            script_function boundMethod = [this, objectValue, method](const std::vector<script_value>& args) -> script_value {
                return method(this, objectValue, args);
            };
            
            push_value(script_value::make_function(boundMethod, engine_ref_));
            return;
        }
        else {
            throw runtime_error("Map has no method '" + expr->member + "'");
        }
    }
    
    // Handle weak_ptr methods
    if (objectValue.is_weak_ptr()) {
        if (expr->member == "lock") {
            script_function lockMethod = [this, objectValue](const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("lock() takes no arguments");
                }
                
                // Check storage type
                if (std::holds_alternative<std::weak_ptr<script_value>>(objectValue.storage_)) {
                    // True weak_ptr
                    auto weak_ptr = std::get<std::weak_ptr<script_value>>(objectValue.storage_);
                    if (auto locked = weak_ptr.lock()) {
                        return *locked;
                    }
                } else if (std::holds_alternative<std::shared_ptr<script_value::object_holder>>(objectValue.storage_)) {
                    // Weak reference stored as object_holder
                    auto holder = std::get<std::shared_ptr<script_value::object_holder>>(objectValue.storage_);
                    if (holder->type_name == "weak_ptr_holder" && holder->data) {
                        auto stored_value = std::static_pointer_cast<script_value>(holder->data);
                        return *stored_value;
                    }
                }
                return make_value(); // null
            };
            push_value(script_value::make_function(lockMethod, engine_ref_));
            return;
        } else if (expr->member == "expired") {
            script_function expiredMethod = [this, objectValue](const std::vector<script_value>& args) -> script_value {
                if (!args.empty()) {
                    throw runtime_error("expired() takes no arguments");
                }
                
                // For our implementation, weak_ptr_holder never expires
                // This is a simplification - proper implementation would track object lifetime
                if (std::holds_alternative<std::weak_ptr<script_value>>(objectValue.storage_)) {
                    auto weak_ptr = std::get<std::weak_ptr<script_value>>(objectValue.storage_);
                    return make_value(weak_ptr.expired());
                } else {
                    return make_value(false); // Not expired
                }
            };
            push_value(script_value::make_function(expiredMethod, engine_ref_));
            return;
        } else {
            throw runtime_error("weak_ptr has no method '" + expr->member + "'");
        }
    }
    
    // Check if it's an object
    if (!objectValue.is_object()) {
        throw runtime_error("Cannot access member '" + expr->member + "' on non-object type");
    }
    
    // Extract the class_instance from the object
    // Access the object_holder directly since we're a friend class
    auto objHolder = std::get<std::shared_ptr<script_value::object_holder>>(objectValue.storage_);
    
    
    // Get the class_instance - could be script or C++ class
    std::shared_ptr<class_instance> instance;
    
    // For C++ classes, is_cpp_class_instance is true and data is class_instance
    if (objHolder->is_cpp_class_instance) {
        instance = std::static_pointer_cast<class_instance>(objHolder->data);
    } else {
        // For script classes, the data IS a class_instance directly
        // Try to cast it (will fail if it's not a class_instance)
        instance = std::static_pointer_cast<class_instance>(objHolder->data);
    }
    
    if (!instance) {
        throw runtime_error("Cannot access member '" + expr->member + "' on non-class object");
    }
    
    // First check if it's a field (registered by the property() method)
    bool has_field_result = instance->has_field(expr->member);
    if (has_field_result) {
        // Check if there's a property getter method
        script_value getter = instance->get_method("_get_" + expr->member);
        if (!getter.is_null()) {
            // Call the getter with 'this' as argument
            const script_function& func = getter.as_function();
            std::vector<script_value> args = {objectValue};
            push_value(func(args));
            return;
        }
        // Otherwise return the field value directly
        push_value(instance->get_field(expr->member));
        return;
    }
    
    // Otherwise, look for a method
    script_value method = instance->get_method(expr->member);
    if (!method.is_null()) {
        // Return a bound method (function that has 'this' pre-bound)
        // We'll create a wrapper function that includes the object as first argument
        script_function boundMethod = [objectValue, method](const std::vector<script_value>& args) -> script_value {
            // Prepend the object as the first argument ('this')
            std::vector<script_value> methodArgs;
            methodArgs.reserve(args.size() + 1);
            methodArgs.push_back(objectValue);
            methodArgs.insert(methodArgs.end(), args.begin(), args.end());
            
            // Call the original method with 'this' as first argument
            const script_function& func = method.as_function();
            auto result = func(methodArgs);
            return result;
        };
        
        push_value(script_value::make_function(boundMethod, engine_ref_));
        return;
    }
    
    // Set exception state instead of throwing
    active_exception_value_ = make_value("Object has no member '" + expr->member + "'");
    current_exception_ = script_exception("Object has no member '" + expr->member + "'", expr->location);
    is_unwinding_ = true;
    push_value(make_value());  // Push null for failed member access
}

void interpreter::visit_lambda_expr(lambda_expr* expr) {
    
    // Capture current environment for closure
    auto closure_env = environment_;
    
    // Check if we need a capture environment
    bool has_explicit_captures = !expr->captures.empty();
    bool has_default_capture = (expr->default_capture != lambda_expr::capture_default::none);
    
    
    // For default captures, analyze the lambda body to find which variables are actually used
    std::unordered_set<std::string> used_variables;
    if (has_default_capture) {
        // Helper to recursively find all identifiers in an expression
        std::function<void(expression*)> find_identifiers;
        find_identifiers = [&](expression* e) {
            if (auto* ident = dynamic_cast<identifier_expr*>(e)) {
                // Skip parameter names
                bool is_param = false;
                for (const auto& param : expr->parameters) {
                    if (param.name == ident->name) {
                        is_param = true;
                        break;
                    }
                }
                if (!is_param) {
                    used_variables.insert(ident->name);
                }
            } else if (auto* binary = dynamic_cast<binary_expr*>(e)) {
                find_identifiers(binary->left.get());
                find_identifiers(binary->right.get());
            } else if (auto* unary = dynamic_cast<unary_expr*>(e)) {
                find_identifiers(unary->operand.get());
            } else if (auto* call = dynamic_cast<call_expr*>(e)) {
                find_identifiers(call->callee.get());
                for (const auto& arg : call->arguments) {
                    find_identifiers(arg.get());
                }
            } else if (auto* member = dynamic_cast<member_expr*>(e)) {
                find_identifiers(member->object.get());
            } else if (auto* assign = dynamic_cast<assignment_expr*>(e)) {
                find_identifiers(assign->target.get());
                find_identifiers(assign->value.get());
            } else if (auto* ternary = dynamic_cast<ternary_expr*>(e)) {
                find_identifiers(ternary->condition.get());
                find_identifiers(ternary->then_expression.get());
                find_identifiers(ternary->else_expression.get());
            }
            // Add more expression types as needed
        };
        
        // Helper to find identifiers in statements
        std::function<void(statement*)> find_in_statement;
        find_in_statement = [&](statement* s) {
            if (auto* expr_stmt = dynamic_cast<expression_stmt*>(s)) {
                find_identifiers(expr_stmt->expression.get());
            } else if (auto* block = dynamic_cast<block_stmt*>(s)) {
                for (const auto& decl : block->declarations) {
                    if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
                        find_identifiers(expr_decl->expression.get());
                    } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(decl.get())) {
                        find_in_statement(stmt_decl->statement.get());
                    }
                }
            } else if (auto* if_s = dynamic_cast<if_stmt*>(s)) {
                find_identifiers(if_s->condition.get());
                find_in_statement(if_s->then_statement.get());
                if (if_s->else_statement) {
                    find_in_statement(if_s->else_statement.get());
                }
            } else if (auto* while_s = dynamic_cast<while_stmt*>(s)) {
                find_identifiers(while_s->condition.get());
                find_in_statement(while_s->body.get());
            } else if (auto* return_s = dynamic_cast<return_stmt*>(s)) {
                if (return_s->value) {
                    find_identifiers(return_s->value.get());
                }
            }
            // Add more statement types as needed
        };
        
        // Analyze the lambda body
        find_in_statement(expr->body.get());
        
    }
    
    // Determine if we actually need a capture environment
    bool needs_capture_env = has_explicit_captures || (has_default_capture && !used_variables.empty());
    
    
    std::shared_ptr<environment> final_closure_env;
    
    if (needs_capture_env) {
        // Create captured variables in the closure environment
        std::shared_ptr<environment> captureEnv = std::make_shared<environment>(closure_env, string_symbolizer_);
        
        // Process default captures first ([=] or [&])
        if (has_default_capture && !used_variables.empty()) {
            bool capture_by_ref = (expr->default_capture == lambda_expr::capture_default::by_reference);
            
            for (const auto& varName : used_variables) {
                // Check if this variable is explicitly overridden in the capture list
                bool is_overridden = false;
                for (const auto& capture : expr->captures) {
                    if (capture.name == varName) {
                        is_overridden = true;
                        break;
                    }
                }
                
                if (!is_overridden && environment_->contains(varName)) {
                    if (capture_by_ref) {
                        // Capture by reference - create reference to original variable
                        script_value* targetPtr = environment_->get_value_ptr(string_symbolizer_->intern(varName));
                        if (targetPtr) {
                            script_value refValue = script_value::make_reference(targetPtr, environment_);
                            captureEnv->define(varName, std::move(refValue));
                        }
                    } else {
                        // Capture by value - deep copy at capture time
                        script_value capturedValue = environment_->get(varName);
                        captureEnv->define(varName, capturedValue.clone());
                    }
                }
            }
        }
        
        // Process explicit captures
        for (const auto& capture : expr->captures) {
            if (environment_->contains(capture.name)) {
                if (capture.by_reference) {
                    // Capture by reference - create reference to original variable
                    uint64_t symbolId = string_symbolizer_->intern(capture.name);
                    script_value* targetPtr = environment_->get_value_ptr(symbolId);
                    if (targetPtr) {
                        script_value refValue = script_value::make_reference(targetPtr, environment_);
                        captureEnv->define(capture.name, std::move(refValue));
                    } else {
                        throw runtime_error("Cannot capture variable by reference: " + capture.name);
                    }
                } else {
                    // Capture by value - deep copy at capture time
                    script_value capturedValue = environment_->get(capture.name);
                    captureEnv->define(capture.name, capturedValue.clone());
                }
            } else {
                throw runtime_error("Cannot capture undefined variable: " + capture.name);
            }
        }
        
        final_closure_env = captureEnv;
    } else {
        // No captures needed - use current environment directly (fast path)
        final_closure_env = closure_env;
        
    }
    
    // Convert the lambda body to a block_stmt if it's not already
    std::shared_ptr<block_stmt> lambdaBody;
    if (auto blockStmt = std::dynamic_pointer_cast<block_stmt>(expr->body)) {
        lambdaBody = blockStmt;
    } else {
        // Wrap single statement in a block
        std::vector<declaration_ptr> stmts;
        if (auto stmt = std::dynamic_pointer_cast<statement>(expr->body)) {
            auto stmtDecl = std::make_shared<statement_decl>(expr->location, stmt);
            stmts.push_back(stmtDecl);
        }
        lambdaBody = std::make_shared<block_stmt>(expr->location, std::move(stmts));
    }
    
    // Pre-cache parameter symbol IDs for optimization
    for (auto& param : expr->parameters) {
        if (param.symbol_id == UINT64_MAX) {
            param.symbol_id = string_symbolizer_->intern(param.name);
        }
    }
    
    // Create the script function
    // Use final_closure_env which is either the capture environment or current environment
    // This ensures lambdas can access variables from their creation context
    // IMPORTANT: If needs_capture_env is false, we pass nullptr as closure_env
    // This makes the lambda behave exactly like a regular function
    
    
    auto lambdaFunc = std::make_shared<script_defined_function>(
        "<lambda>",  // Anonymous function name
        expr->parameters,
        expr->return_type,
        lambdaBody,
        needs_capture_env ? final_closure_env : nullptr  // Only use closure env if we have captures
    );
    
    // Create a script_function wrapper
    // capture lambdaFunc by value to ensure it stays alive
    script_function funcWrapper = [this, lambdaFunc](const std::vector<script_value>& args) -> script_value {
        return call_function(*lambdaFunc, args);
    };
    
    // Push the lambda as a function value
    push_value(script_value::make_function(funcWrapper, engine_ref_));
}

void interpreter::visit_new_expr(new_expr* expr) {
    // This handles expressions like: new Point(), new Point(3.0, 4.0), etc.
    // The new_expr contains a type and arguments
    
    // std::cerr << "DEBUG: visit_new_expr called for type: " << (expr->type ? expr->type->type_name : "NULL") << std::endl;
    
    if (!expr->type) {
        throw runtime_error("New expression missing type information");
    }
    
    // Handle built-in types specially
    if (expr->type->base_type == script_value_type::jai_array_type) {
        // array<T>{} constructor
        if (!expr->arguments.empty()) {
            throw runtime_error("array{} constructor does not take arguments");
        }
        
        // Create empty array with the specified element type
        auto element_type = expr->type->element_type();
        if (!element_type) {
            element_type = type_info::make_int(); // Default to int if no type specified
        }
        push_value(script_value::make_array(element_type));
        return;
    }
    
    if (expr->type->base_type == script_value_type::jai_map_type) {
        // map<K,V>{} constructor
        if (!expr->arguments.empty()) {
            throw runtime_error("map{} constructor does not take arguments");
        }
        
        // Create empty map with the specified key/value types
        auto key_type = expr->type->key_type();
        auto value_type = expr->type->value_type();
        if (!key_type) key_type = type_info::make_string();
        if (!value_type) value_type = type_info::make_int();
        push_value(script_value::make_map(key_type, value_type));
        return;
    }
    
    std::string className = expr->type->type_name;
    
    // Evaluate all arguments
    std::vector<script_value> args;
    for (const auto& argExpr : expr->arguments) {
        argExpr->accept(this);
        args.push_back(std::move(pop_value()));
    }
    
    // Look for a constructor function registered with this class name
    // The class builder registers constructors as overloaded functions
    try {
        // std::cerr << "DEBUG: Looking for constructor: " << className << std::endl;
        script_value constructorFunc = environment_->get(className);
        if (constructorFunc.is_function()) {
            // std::cerr << "DEBUG: Found constructor function for: " << className << std::endl;
            const script_function& func = constructorFunc.as_function();
            script_value instance = func(args);
            push_value(std::move(instance));
            return;
        }
        // std::cerr << "DEBUG: Constructor found but not a function for: " << className << std::endl;
    } catch (const runtime_error& e) {
        // Constructor function not found, fall through to error
        // std::cerr << "DEBUG: Constructor not found for: " << className << " - " << e.what() << std::endl;
    }
    
    throw runtime_error("No constructor found for class: " + className);
}

void interpreter::visit_ternary_expr(ternary_expr* expr) {
    // Evaluate the condition
    expr->condition->accept(this);
    script_value conditionValue = pop_value();
    
    // Check if condition is truthy
    bool conditionIsTruthy = is_truthy(conditionValue);
    
    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        expr->then_expression->accept(this);
    } else {
        expr->else_expression->accept(this);
    }
}

void interpreter::visit_array_literal_expr(array_literal_expr* expr) {
    // Create array script_value with mixed element type (for now)
    auto element_type = type_info::make_int(); // TODO: Better type inference
    script_value arrayValue = script_value::make_array(element_type, engine_ref_);
    
    // Get the internal vector to populate
    auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());
    
    // Evaluate each element and add to array
    for (const auto& element : expr->elements) {
        element->accept(this);
        array.push_back(pop_value());
    }
    
    push_value(std::move(arrayValue));
}

void interpreter::visit_map_literal_expr(map_literal_expr* expr) {
    // Create map script_value with mixed key/value types (for now)
    auto keyType = type_info::make_string(); // TODO: Better type inference
    auto valueType = type_info::make_int(); // TODO: Better type inference
    script_value mapValue = script_value::make_map(keyType, valueType, engine_ref_);
    
    // Get the internal map to populate
    auto& map = const_cast<std::map<script_value, script_value>&>(mapValue.as_map());
    
    // Evaluate each key-value pair and add to map
    for (const auto& entry : expr->entries) {
        // Evaluate key
        entry.first->accept(this);
        script_value key = pop_value();
        
        // Evaluate value
        entry.second->accept(this);
        script_value value = pop_value();
        
        // Insert into map
        map.insert_or_assign(std::move(key), std::move(value));
    }
    
    push_value(std::move(mapValue));
}

void interpreter::visit_this_expr(this_expr* expr) {
    // Try to get 'this' from the current environment
    try {
        script_value this_val = environment_->get("this");
        push_value(this_val);
    } catch (const runtime_error&) {
        throw runtime_error("'this' can only be used inside methods");
    }
}

void interpreter::visit_super_expr(super_expr* expr) {
    // The super expression is used in two contexts:
    // 1. Constructor delegation: Enemy(name) : super(name)
    // 2. Method calls: super::attack()
    
    // For now, we'll handle it as a method call context
    // Constructor delegation is handled differently in the parser
    
    // Get 'this' from the environment
    auto this_value = environment_->get("this");
    if (this_value.is_null()) {
        throw runtime_error("'super' used outside of class method");
    }
    
    // Push 'this' onto the stack for method call
    push_value(this_value);
}

void interpreter::visit_throw_expr(throw_expr* expr) {
    if (expr->value) {
        // Evaluate the expression to throw
        expr->value->accept(this);
        script_value val = pop_value();
        
        // Store the exception value and convert to string for exception message
        active_exception_value_ = val;
        std::string message = val.to_string();
        current_exception_ = script_exception(message, expr->location);
    } else {
        // Re-throw current exception
        if (!current_exception_) {
            throw script_exception("No exception to re-throw", expr->location);
        }
        // Keep the existing active_exception_value_
    }
    
    is_unwinding_ = true;
}

void interpreter::visit_if_stmt(if_stmt* stmt) {
    // Evaluate the condition
    stmt->condition->accept(this);
    script_value conditionValue = pop_value();
    
    // Execute appropriate branch based on truthiness
    if (is_truthy(conditionValue)) {
        stmt->then_statement->accept(this);
    } else if (stmt->else_statement) {
        stmt->else_statement->accept(this);
    }
}

void interpreter::visit_while_stmt(while_stmt* stmt) {
    while (true) {
        // Evaluate the condition
        stmt->condition->accept(this);
        script_value conditionValue = pop_value();
        
        // Check if we should continue the loop
        if (!is_truthy(conditionValue)) {
            break;
        }
        
        try {
            // Execute the loop body
            stmt->body->accept(this);
        } catch (const break_exception&) {
            // Break out of the loop
            break;
        } catch (const continue_exception&) {
            // Continue to next iteration
            continue;
        }
        
        // Check if a return statement was executed
        if (hasReturnValue_) {
            break;
        }
    }
}

void interpreter::visit_for_stmt(for_stmt* stmt) {
    // Create new scope for the for loop (initialization variables should be scoped)
    auto previous = environment_;
    environment_ = std::make_shared<environment>(environment_, string_symbolizer_);
    
    try {
        // Execute initialization (if present)
        if (stmt->initializer) {
            stmt->initializer->accept(this);
        }
        
        while (true) {
            // Check condition (if present, default to true)
            if (stmt->condition) {
                stmt->condition->accept(this);
                script_value conditionValue = pop_value();
                if (!is_truthy(conditionValue)) {
                    break;
                }
            }
            
            try {
                // Execute the loop body
                stmt->body->accept(this);
            } catch (const break_exception&) {
                // Break out of the loop
                break;
            } catch (const continue_exception&) {
                // Continue to next iteration, but execute update first
                if (stmt->update) {
                    stmt->update->accept(this);
                    // Pop the update result if it leaves a value on the stack
                    if (!valueStack_.empty()) {
                        pop_value();
                    }
                }
                continue;
            }
            
            // Check if a return statement was executed
            if (hasReturnValue_) {
                break;
            }
            
            // Execute update expression (if present)
            if (stmt->update) {
                stmt->update->accept(this);
                // Pop the update result if it leaves a value on the stack
                if (!valueStack_.empty()) {
                    pop_value();
                }
            }
        }
    } catch (...) {
        // Restore environment even if an error occurs
        environment_ = previous;
        throw;
    }
    
    // Restore previous environment
    environment_ = previous;
}

void interpreter::visit_range_for_stmt(range_for_stmt* stmt) {
    throw runtime_error("Range-based for loops not yet implemented");
}

void interpreter::visit_return_stmt(return_stmt* stmt) {
    if (stmt->value) {
        // Evaluate the return expression
        stmt->value->accept(this);
        returnValue_ = pop_value();
    } else {
        // Return null if no expression
        returnValue_ = make_value();
    }
    
    hasReturnValue_ = true;
}

void interpreter::visit_break_stmt(break_stmt* stmt) {
    throw break_exception();
}

void interpreter::visit_continue_stmt(continue_stmt* stmt) {
    throw continue_exception();
}

void interpreter::visit_try_stmt(try_stmt* stmt) {
    // Save exception state
    auto saved_exception = current_exception_;
    auto saved_unwinding = is_unwinding_;
    auto saved_exception_value = active_exception_value_;
    auto saved_catch_var = current_catch_var_;
    
    // Reset state for try block
    // Don't reset exception state if we're in a catch block (allows re-throw)
    if (current_catch_var_.empty()) {
        current_exception_.reset();
        active_exception_value_ = make_value();
    }
    is_unwinding_ = false;
    current_catch_var_.clear();
    
    // Execute try block
    stmt->try_block->accept(this);
    
    // Check if exception was thrown
    if (is_unwinding_ && current_exception_) {
        // Reset unwinding flag
        is_unwinding_ = false;
        
        // Set the current catch variable name so identifier lookup can find it
        current_catch_var_ = stmt->catch_var;
        
        // Execute catch block
        stmt->catch_block->accept(this);
        
        // Clear catch variable
        current_catch_var_.clear();
        
        // Only clear exception if it wasn't re-thrown
        if (!is_unwinding_) {
            current_exception_.reset();
            active_exception_value_ = make_value();
        }
    }
    
    // If still unwinding after catch, we need to be careful about state restoration
    // Don't restore if a new exception was thrown in the catch block
    if (is_unwinding_ && saved_unwinding) {
        // We were already unwinding before this try/catch, restore that state
        current_exception_ = saved_exception;
        active_exception_value_ = saved_exception_value;
    }
    // If is_unwinding_ is true but saved_unwinding was false, 
    // it means a new exception was thrown in the catch block - keep it
    
    // Always restore the catch variable state
    current_catch_var_ = saved_catch_var;
}

void interpreter::visit_function_decl(function_decl* decl) {
    // Pre-cache symbol IDs for all parameters (parameter binding optimization)
    for (auto& param : decl->parameters) {
        if (param.symbol_id == UINT64_MAX) {
            param.symbol_id = string_symbolizer_->intern(param.name);
        }
    }
    
    // Don't capture any environment in the closure - just use nullptr
    // The environment stack will handle variable lookup naturally
    auto scriptFunc = std::make_shared<script_defined_function>(
        decl->name,
        decl->parameters,
        decl->return_type,
        decl->body,
        nullptr  // No closure needed - environment stack handles everything
    );
    
    // Create wrapper function
    script_value functionValue = script_value::make_function([this, scriptFunc](const std::vector<script_value>& args) -> script_value {
        return call_function(*scriptFunc, args);
    }, engine_ref_);
    
    // Define the function in current environment
    environment_->define(decl->name, functionValue);
}

void interpreter::visit_class_decl(class_decl* decl) {
    // Check if class already exists (for hot reloading)
    std::shared_ptr<script_class_definition> class_def = nullptr;
    bool is_redefinition = false;
    
    // Use a static prefix to avoid repeated allocations
    static const std::string CLASS_PREFIX = "__class_";
    std::string class_var_name = CLASS_PREFIX + decl->name;
    
    try {
        auto existing = environment_->get(class_var_name);
        if (!existing.is_null()) {
            // Class already exists - reuse the definition for hot reloading
            class_def = existing.as<std::shared_ptr<script_class_definition>>();
            is_redefinition = true;
            // Found existing class definition for hot reload
        }
    } catch (...) {
        // Class doesn't exist yet
        // No existing class definition found
    }
    
    if (!class_def) {
        // Create a new script class definition
        class_def = std::make_shared<script_class_definition>(decl->name, engine_ref_);
    } else if (is_redefinition) {
        // Clear old ASTs for hot reload
        class_def->clear_asts();
    }
    
    // Collect new field defaults and methods
    std::unordered_map<std::string, script_value> new_field_defaults;
    std::unordered_map<std::string, script_value> new_methods;
    
    // Reserve capacity based on member count for efficiency
    if (!decl->members.empty()) {
        new_field_defaults.reserve(decl->members.size());
        new_methods.reserve(decl->members.size());
    }
    
    // Debug output
    // std::cerr << "DEBUG: Processing class declaration: " << decl->name << std::endl;
    
    // Handle base classes (single inheritance for now)
    if (!decl->base_classes.empty()) {
        // For now, only support single inheritance
        if (decl->base_classes.size() > 1) {
            throw runtime_error("Multiple inheritance not supported");
        }
        
        // Look up base class definition
        const std::string& base_name = decl->base_classes[0];
        
        // First try to find a script class
        auto base_class_var = environment_->get("__class_" + base_name);
        
        if (!base_class_var.is_null()) {
            // Found a script class
            auto base_class_def = base_class_var.as<std::shared_ptr<class_definition>>();
            class_def->set_parent(base_class_def);
        } else {
            // Try to find a C++ class using the class lookup callback
            if (class_lookup_callback_) {
                auto cpp_class_def = class_lookup_callback_(base_name);
                if (cpp_class_def) {
                    // Found a C++ class! Set it as the base
                    class_def->set_cpp_base_class(cpp_class_def);
                    
                    // Also set as regular parent for method resolution
                    class_def->set_parent(cpp_class_def);
                } else if (environment_->contains(base_name)) {
                    // Constructor exists but no class definition found
                    // This shouldn't happen with proper engine integration
                    throw runtime_error("Constructor found for '" + base_name + "' but no class definition available");
                } else {
                    throw runtime_error("Base class not found: " + base_name);
                }
            } else {
                // No class lookup callback set - check if constructor exists
                if (environment_->contains(base_name)) {
                    throw runtime_error("Script class inheriting from C++ class requires engine integration");
                } else {
                    throw runtime_error("Base class not found: " + base_name);
                }
            }
        }
    }
    
    // Track whether we found an explicit constructor
    bool found_constructor = false;
    
    // Process class members
    for (const auto& member : decl->members) {
        // Extract the actual declaration from the member
        auto* var_decl = dynamic_cast<variable_decl*>(member.declaration.get());
        auto* func_decl = dynamic_cast<function_decl*>(member.declaration.get());
        
        if (var_decl) {
            // Field declaration
            script_value default_val;
            std::string field_name = var_decl->name;
            
            if (var_decl->initializer) {
                // Check if the initializer is an assignment expression
                // This happens when the parser sees "x = 0" and creates assignment_expr
                auto* assign_expr = dynamic_cast<assignment_expr*>(var_decl->initializer.get());
                if (assign_expr) {
                    // For field declarations like "x = 0", we need to get the field name from the assignment
                    if (auto* ident_expr = dynamic_cast<identifier_expr*>(assign_expr->target.get())) {
                        field_name = ident_expr->name;
                    }
                    // Get the RHS value
                    assign_expr->value->accept(this);
                    default_val = pop_value();
                } else {
                    // Normal initializer expression
                    var_decl->initializer->accept(this);
                    default_val = pop_value();
                }
            }
            
            // Collect field for later processing
            if (!field_name.empty()) {
                new_field_defaults[field_name] = default_val;
            }
            
        } else if (func_decl) {
            // Method declaration
            auto method_name = func_decl->name;
            
            // Check for constructor
            if (method_name == decl->name) {
                // Constructor
                found_constructor = true;
                
                // Pre-cache symbol IDs for constructor parameters
                for (auto& param : func_decl->parameters) {
                    if (param.symbol_id == UINT64_MAX) {
                        param.symbol_id = string_symbolizer_->intern(param.name);
                    }
                }
                
                class_def->add_constructor_from_ast(
                    std::static_pointer_cast<function_decl>(member.declaration),
                    this
                );
                
                // Constructor will be registered after all members are processed
                
            } else if (method_name.size() > 0 && method_name[0] == '~') {
                // Destructor
                class_def->add_destructor_from_ast(
                    std::static_pointer_cast<function_decl>(member.declaration),
                    this
                );
                
            } else {
                // Regular method
                if (is_redefinition) {
                    // For redefinition, just collect the method function
                    // We'll add it to the class via redefine_class later
                    auto method_ast = std::static_pointer_cast<function_decl>(member.declaration);
                    auto method_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), 
                                       method_ast, 
                                       class_def, 
                                       class_name = decl->name](const std::vector<script_value>& args) -> script_value {
                        auto self = weak_self.lock();
                        if (!self) {
                            throw runtime_error("Interpreter was destroyed before method call");
                        }
                        
                        // First argument should be 'this' object
                        if (args.empty()) {
                            throw runtime_error("Method called without 'this' object");
                        }
                        
                        // Extract 'this' from first argument
                        script_value this_obj = args[0];
                        
                        // Create remaining arguments (excluding 'this')
                        std::vector<script_value> method_args(args.begin() + 1, args.end());
                        
                        // Create a new environment for the method that has 'this' defined
                        auto method_env = std::make_shared<environment>(
                            self->environment_, 
                            self->string_symbolizer_
                        );
                        method_env->define("this", this_obj);
                        
                        // Call the interpreter method directly
                        return self->execute_method_ast(method_ast, method_env, method_args);
                    };
                    
                    new_methods[method_name] = script_value::make_function(method_func, engine_ref_);
                } else {
                    // For new classes, add method normally
                    class_def->add_method_from_ast(
                        method_name,
                        std::static_pointer_cast<function_decl>(member.declaration),
                        this
                    );
                }
            }
        }
    }
    
    // After processing all members, create a dispatcher for constructors if any were found
    if (found_constructor) {
        // Create a constructor dispatcher that selects based on argument count
        auto ctor_dispatcher = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), 
                               class_def, 
                               class_name = decl->name](const std::vector<script_value>& args) -> script_value {
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Get all constructor ASTs
            const auto& ctor_asts = class_def->get_constructor_asts();
            
            // Find constructor with matching parameter count
            std::shared_ptr<function_decl> matching_ctor;
            for (const auto& ctor_ast : ctor_asts) {
                if (ctor_ast->parameters.size() == args.size()) {
                    matching_ctor = ctor_ast;
                    break;
                }
            }
            
            if (!matching_ctor) {
                throw runtime_error("No constructor found for " + class_name + 
                                  " with " + std::to_string(args.size()) + " arguments");
            }
            
            // Create instance
            auto instance = class_def->create_instance();
            // Instance created
            
            // Create environment with 'this'
            auto ctor_env = std::make_shared<environment>(self->environment_, self->string_symbolizer_);
            ctor_env->define("this", script_value::make_object(class_name, instance));
            // 'this' defined in constructor environment
            
            // Execute the matching constructor
            script_defined_function ctor_script_func(
                matching_ctor->name,
                matching_ctor->parameters,
                matching_ctor->return_type,
                matching_ctor->body,
                ctor_env
            );
            
            self->call_function(ctor_script_func, args);
            // Constructor executed
            
            auto result = script_value::make_object(class_name, instance);
            // Object wrapped
            return result;
        };
        
        // Register the dispatcher
        environment_->define(decl->name, script_value::make_function(ctor_dispatcher, engine_ref_));
    }
    
    // If no constructor was found, create a default constructor
    else {
        // Create a default constructor that just initializes the instance
        auto default_ctor_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), class_def, class_name = decl->name](const std::vector<script_value>& args) -> script_value {
            // Get strong reference from weak_ptr
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Default constructor shouldn't have arguments
            if (!args.empty()) {
                throw runtime_error("Default constructor for class " + class_name + " takes no arguments");
            }
            
            // Create instance using inherited create_instance()!
            // This will initialize all fields with their default values
            auto instance = class_def->create_instance();
            // Default constructor instance created
            
            auto result = script_value::make_object(class_name, instance);
            // Default constructor object wrapped
            return result;
        };
        
        // Register default constructor
        environment_->define(decl->name, script_value::make_function(default_ctor_func, engine_ref_));
        // std::cerr << "DEBUG: Registered default constructor for class: " << decl->name << std::endl;
    }
    
    // If this is a redefinition, we need to call redefine_class to update all instances
    if (is_redefinition) {
        // Call redefine_class with the new field defaults and methods
        // Call redefine_class to migrate existing instances
        class_def->redefine_class(new_field_defaults, new_methods, engine_ref_);
    } else {
        // For new classes, add the fields normally
        for (const auto& [field_name, default_val] : new_field_defaults) {
            class_def->add_field(field_name, default_val);
        }
        // Initialize fingerprint for future comparisons
        class_def->initialize_fingerprint();
    }
    
    // Store the class definition in a special variable for later retrieval
    // This allows inheritance and other features to work
    environment_->define(class_var_name, script_value::make_object("class_definition", class_def, engine_ref_));
    
    // The constructor function is already registered in the environment
    // which allows "new ClassName()" syntax to work
}

void interpreter::visit_expression_decl(expression_decl* decl) {
    // Evaluate the expression and leave the result on the stack
    // This allows top-level expressions to return values
    decl->expression->accept(this);
}

// Execute a method AST with a given environment
script_value interpreter::execute_method_ast(std::shared_ptr<function_decl> ast, 
                                           std::shared_ptr<environment> method_env,
                                           const std::vector<script_value>& args) {
    // Create a script_defined_function with the method environment
    script_defined_function script_func(
        ast->name,
        ast->parameters, 
        ast->return_type,
        ast->body,
        method_env  // Method environment with 'this'
    );
    
    // Execute method with the arguments
    return call_function(script_func, args);
}

// Function call implementation
script_value interpreter::call_function(const script_defined_function& function, const std::vector<script_value>& args) {
    // Validate arguments
    validate_function_arguments(function.parameters, args);
    
    
    // Create new environment for function execution using pool optimization
    // Both lambdas and functions need a fresh environment for their parameters
    auto previousEnv = environment_;
    
    // For lambdas with closures, the execution environment needs to chain:
    // [parameter env] -> [closure env] -> [global env]
    // For regular functions:
    // [parameter env] -> [current env]
    if (function.closure_env) {
        // Lambda: create fresh environment with closure as parent
        environment_ = get_pooled_environment(function.closure_env);
        
    } else {
        // Regular function: create fresh environment with current as parent
        environment_ = get_pooled_environment(previousEnv);
    }
    
    // Store previous return state
    bool previousHasReturn = hasReturnValue_;
    script_value previousReturn = returnValue_;
    hasReturnValue_ = false;
    
    try {
        
        // Bind parameters to arguments
        for (size_t i = 0; i < function.parameters.size(); ++i) {
            const auto& param = function.parameters[i];
            const auto& arg = args[i];
            
            
            // Use pre-cached symbol ID (parameter binding optimization)
            // Symbol IDs are cached at function definition time in visit_function_decl
            if (param.is_reference) {
                // For reference parameters, create a reference value
                if (!current_arg_metadata_.empty() && i < current_arg_metadata_.size()) {
                    auto symbol_id = current_arg_metadata_[i].first;
                    auto env = current_arg_metadata_[i].second;
                    
                    if (symbol_id != UINT64_MAX && env != nullptr) {
                        // Get pointer to the argument
                        script_value* argPtr = env->get_value_ptr(symbol_id);
                        if (!argPtr) {
                            throw runtime_error("Cannot take reference of undefined variable");
                        }
                        
                        // If the argument is itself a reference, get the final target
                        if (argPtr->is_reference()) {
                            auto refHolder = std::get<std::shared_ptr<script_value::reference_holder>>(argPtr->storage_);
                            if (!refHolder || !refHolder->target) {
                                throw runtime_error("Reference target is null");
                            }
                            // Create reference to the final target
                            script_value refValue = script_value::make_reference(refHolder->target, refHolder->sourceEnv.lock());
                            if (param.symbol_id != UINT64_MAX) {
                                environment_->define(param.symbol_id, std::move(refValue));
                            } else {
                                environment_->define(param.name, std::move(refValue));
                            }
                        } else {
                            // Create reference to the argument
                            script_value refValue = script_value::make_reference(argPtr, env);
                            if (param.symbol_id != UINT64_MAX) {
                                environment_->define(param.symbol_id, std::move(refValue));
                            } else {
                                environment_->define(param.name, std::move(refValue));
                            }
                        }
                    } else {
                        // No metadata - can't create reference
                        throw runtime_error("Cannot pass non-lvalue to reference parameter");
                    }
                } else {
                    // No metadata - can't create reference
                    throw runtime_error("Cannot pass non-lvalue to reference parameter");
                }
            } else {
                // Non-reference parameter - deep copy the argument
                if (param.symbol_id != UINT64_MAX) {
                    environment_->define(param.symbol_id, arg.clone());
                } else {
                    // Fallback to parameter name if symbol_id not set
                    environment_->define(param.name, arg.clone());
                }
            }
        }
        
        // Execute function body without creating another environment
        // (since we already created one for the function call)
        for (const auto& decl : function.body->declarations) {
            decl->accept(this);
            // Check if we hit a return statement and break early
            if (hasReturnValue_) {
                break;
            }
        }
        
        // Get return value
        script_value result;
        if (hasReturnValue_) {
            result = returnValue_;
        } else {
            // If no return statement, return null
            result = make_value();
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

void interpreter::validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args) {
    if (params.size() != args.size()) {
        throw runtime_error("Function expected " + std::to_string(params.size()) + 
                         " arguments but got " + std::to_string(args.size()));
    }
    
    // TODO: Add type checking for parameters
    // For now, we'll just check argument count
}

script_value interpreter::make_function(std::shared_ptr<script_defined_function> func) {
    // Create a wrapper that handles reference parameters properly
    script_function wrapper = [this, func](const std::vector<script_value>& args) -> script_value {
        // For functions with reference parameters, we need special handling
        bool hasRefParams = false;
        for (const auto& param : func->parameters) {
            if (param.is_reference) {
                hasRefParams = true;
                break;
            }
        }
        
        if (!hasRefParams) {
            // No reference parameters - use normal call
            return call_function(*func, args);
        }
        
        // Has reference parameters - we need to handle them specially
        // For now, just call normally - we'll implement proper reference handling later
        return call_function(*func, args);
    };
    return script_value::make_function(wrapper, engine_ref_);
}

// Function call optimization helpers
std::shared_ptr<environment> interpreter::get_pooled_environment(std::shared_ptr<environment> parent) {
    if (environment_pool_index_ < environment_pool_.size()) {
        // Reuse existing environment from pool
        auto env = environment_pool_[environment_pool_index_++];
        env->reset(parent);
        return env;
    } else {
        // Pool is exhausted, create new environment and add to pool
        auto newEnv = std::make_shared<environment>(parent, string_symbolizer_);
        environment_pool_.push_back(newEnv);
        ++environment_pool_index_;
        return newEnv;
    }
}

void interpreter::reset_environment_pool() {
    environment_pool_index_ = 0;
}

} // namespace jai