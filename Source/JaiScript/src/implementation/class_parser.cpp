#include "jaiscript/detail/class_parser.hpp"
#include "jaiscript/detail/lexer.hpp"
#include <stdexcept>

namespace jai {

// AST node implementations
void script_class_decl::accept(ast_visitor* visitor) {
    if (auto class_visitor = dynamic_cast<class_visitor*>(visitor)) {
        class_visitor->visit_script_class_decl(this);
    }
}

void field_decl::accept(ast_visitor* visitor) {
    if (auto class_visitor = dynamic_cast<class_visitor*>(visitor)) {
        class_visitor->visit_field_decl(this);
    }
}

void method_decl::accept(ast_visitor* visitor) {
    if (auto class_visitor = dynamic_cast<class_visitor*>(visitor)) {
        class_visitor->visit_method_decl(this);
    }
}

void constructor_decl::accept(ast_visitor* visitor) {
    if (auto class_visitor = dynamic_cast<class_visitor*>(visitor)) {
        class_visitor->visit_constructor_decl(this);
    }
}

void destructor_decl::accept(ast_visitor* visitor) {
    if (auto class_visitor = dynamic_cast<class_visitor*>(visitor)) {
        class_visitor->visit_destructor_decl(this);
    }
}

void script_super_expr::accept(ast_visitor* visitor) {
    if (auto class_visitor = dynamic_cast<class_visitor*>(visitor)) {
        class_visitor->visit_script_super_expr(this);
    }
}

// class_parser implementation
class_parser::class_parser(parser& parent_parser) 
    : parser_(parent_parser) {
}

std::unique_ptr<script_class_decl> class_parser::parse_class_declaration() {
    auto class_stmt = std::make_unique<script_class_decl>();
    
    // Consume 'class' keyword
    parser_.consume(token_type::class_keyword, "Expected 'class'");
    
    // Get class name
    auto name_token = parser_.consume(token_type::identifier, "Expected class name");
    class_stmt->name = name_token.lexeme;
    current_class_name_ = class_stmt->name;
    
    // Check for inheritance
    if (parser_.match(token_type::colon)) {
        auto base_token = parser_.consume(token_type::identifier, "Expected base class name");
        class_stmt->base_class_name = base_token.lexeme;
    }
    
    // Parse class body
    parser_.consume(token_type::left_brace, "Expected '{'");
    
    access_level current_access = access_level::public_access;
    
    while (!parser_.check(token_type::right_brace) && !parser_.is_at_end()) {
        // Check for access modifiers
        if (is_access_modifier(parser_.peek().type)) {
            current_access = parse_access_level();
            parser_.consume(token_type::colon, "Expected ':' after access modifier");
            continue;
        }
        
        // Parse class members
        if (is_destructor_name(parser_.peek().lexeme)) {
            auto destructor = parse_destructor_declaration();
            destructor->class_name = class_stmt->name;
            class_stmt->destructor = std::move(destructor);
        } else if (is_constructor_name(parser_.peek().lexeme)) {
            auto constructor = parse_constructor_declaration();
            constructor->class_name = class_stmt->name;
            class_stmt->constructors.push_back(std::move(constructor));
        } else {
            // Try to parse as method or field
            auto saved_position = parser_.current();
            
            try {
                // Try method first
                auto method = parse_method_declaration();
                method->access = current_access;
                class_stmt->methods.push_back(std::move(method));
            } catch (...) {
                // If method parsing fails, try field
                parser_.set_current(saved_position);
                auto field = parse_field_declaration();
                field->access = current_access;
                class_stmt->fields.push_back(std::move(field));
            }
        }
    }
    
    parser_.consume(token_type::right_brace, "Expected '}'");
    return class_stmt;
}

std::unique_ptr<field_decl> class_parser::parse_field_declaration() {
    auto field = std::make_unique<field_decl>();
    
    // Parse type (optional - can be inferred)
    if (parser_.check_next(token_type::identifier)) {
        field->type_name = parser_.advance().lexeme;
    }
    
    // Parse field name
    field->name = parser_.consume(token_type::identifier, "Expected field name").lexeme;
    
    // Parse default value
    if (parser_.match(token_type::equal)) {
        field->default_value = parser_.expression();
    }
    
    parser_.consume(token_type::semicolon, "Expected ';' after field declaration");
    return field;
}

std::unique_ptr<method_decl> class_parser::parse_method_declaration() {
    auto method = std::make_unique<method_decl>();
    
    // Parse return type (optional)
    if (parser_.check_next(token_type::identifier)) {
        method->return_type = parser_.advance().lexeme;
    }
    
    // Parse method name
    method->name = parser_.consume(token_type::identifier, "Expected method name").lexeme;
    
    // Parse parameters
    method->parameters = parse_parameter_list();
    
    // Check for override keyword
    if (parser_.match(token_type::override_keyword)) {
        method->is_override = true;
    }
    
    // Parse method body
    method->body = std::unique_ptr<block_stmt>(
        static_cast<block_stmt*>(parser_.block_statement().release())
    );
    
    return method;
}

std::unique_ptr<constructor_decl> class_parser::parse_constructor_declaration() {
    auto constructor = std::make_unique<constructor_decl>();
    
    // Constructor name should match class name
    constructor->class_name = parser_.consume(token_type::identifier, "Expected constructor name").lexeme;
    
    // Parse parameters
    constructor->parameters = parse_parameter_list();
    
    // Check for delegation
    parse_constructor_delegation(constructor.get());
    
    // Parse constructor body
    constructor->body = std::unique_ptr<block_stmt>(
        static_cast<block_stmt*>(parser_.block_statement().release())
    );
    
    return constructor;
}

std::unique_ptr<destructor_decl> class_parser::parse_destructor_declaration() {
    auto destructor = std::make_unique<destructor_decl>();
    
    // Consume '~' and class name
    parser_.consume(token_type::tilde, "Expected '~'");
    destructor->class_name = parser_.consume(token_type::identifier, "Expected class name").lexeme;
    parser_.consume(token_type::left_paren, "Expected '('");
    parser_.consume(token_type::right_paren, "Expected ')'");
    
    // Parse destructor body
    destructor->body = std::unique_ptr<block_stmt>(
        static_cast<block_stmt*>(parser_.block_statement().release())
    );
    
    return destructor;
}

access_level class_parser::parse_access_level() {
    auto token = parser_.advance();
    
    switch (token.type) {
    case token_type::public_keyword:
        return access_level::public_access;
    case token_type::private_keyword:
        return access_level::private_access;
    case token_type::protected_keyword:
        return access_level::protected_access;
    default:
        throw std::runtime_error("Invalid access modifier");
    }
}

void class_parser::parse_constructor_delegation(constructor_decl* ctor) {
    if (!parser_.match(token_type::colon)) {
        return;  // No delegation
    }
    
    ctor->is_delegating = true;
    
    if (parser_.match(token_type::super_keyword)) {
        // Base class delegation: super(args)
        ctor->delegation_type_val = delegation_type::base_class;
    } else if (parser_.check(token_type::identifier)) {
        // Same-class delegation: ClassName(args)
        std::string delegate_class = parser_.advance().lexeme;
        if (delegate_class != ctor->class_name) {
            throw std::runtime_error("Invalid constructor delegation to different class");
        }
        ctor->delegation_type_val = delegation_type::same_class;
    } else {
        throw std::runtime_error("Expected delegation target after ':'");
    }
    
    // Parse delegation arguments
    ctor->delegation_args = parse_argument_list();
}

std::unique_ptr<script_super_expr> class_parser::parse_super_expression() {
    auto super = std::make_unique<script_super_expr>();
    
    parser_.consume(token_type::super_keyword, "Expected 'super'");
    parser_.consume(token_type::double_colon, "Expected '::'");
    super->method_name = parser_.consume(token_type::identifier, "Expected method name").lexeme;
    super->arguments = parse_argument_list();
    
    return super;
}

bool class_parser::is_access_modifier(token_type type) {
    return type == token_type::public_keyword ||
           type == token_type::private_keyword ||
           type == token_type::protected_keyword;
}

bool class_parser::is_constructor_name(const std::string& name) {
    return name == current_class_name_;
}

bool class_parser::is_destructor_name(const std::string& name) {
    return !name.empty() && name[0] == '~';
}

std::vector<std::string> class_parser::parse_parameter_list() {
    std::vector<std::string> parameters;
    
    parser_.consume(token_type::left_paren, "Expected '('");
    
    if (!parser_.check(token_type::right_paren)) {
        do {
            // For simplicity, just store parameter names
            // In full implementation, would parse types too
            auto param = parser_.consume(token_type::identifier, "Expected parameter name");
            parameters.push_back(param.lexeme);
        } while (parser_.match(token_type::comma));
    }
    
    parser_.consume(token_type::right_paren, "Expected ')'");
    return parameters;
}

std::vector<std::unique_ptr<expression>> class_parser::parse_argument_list() {
    std::vector<std::unique_ptr<expression>> arguments;
    
    parser_.consume(token_type::left_paren, "Expected '('");
    
    if (!parser_.check(token_type::right_paren)) {
        do {
            arguments.push_back(parser_.expression());
        } while (parser_.match(token_type::comma));
    }
    
    parser_.consume(token_type::right_paren, "Expected ')'");
    return arguments;
}

} // namespace jai