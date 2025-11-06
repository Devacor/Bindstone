#include <jaiscript/core/parse_errors.hpp>

namespace jai {

    std::string parse_error_category_impl::message(int ev) const {
        switch (static_cast<parse_error_code>(ev)) {
        case parse_error_code::success:
            return "Success";

        // Token errors
        case parse_error_code::unexpected_token:
            return "Unexpected token";
        case parse_error_code::unexpected_eof:
            return "Unexpected end of file";
        case parse_error_code::expected_token:
            return "Expected token";

        // Expression errors
        case parse_error_code::invalid_expression:
            return "Invalid expression";
        case parse_error_code::invalid_assignment_target:
            return "Invalid assignment target";
        case parse_error_code::invalid_increment_target:
            return "Invalid increment/decrement target";
        case parse_error_code::invalid_member_access:
            return "Invalid member access";

        // Declaration errors
        case parse_error_code::invalid_declaration:
            return "Invalid declaration";
        case parse_error_code::duplicate_parameter:
            return "Duplicate parameter name";
        case parse_error_code::invalid_parameter:
            return "Invalid parameter";
        case parse_error_code::invalid_class_member:
            return "Invalid class member";
        case parse_error_code::invalid_constructor:
            return "Invalid constructor";
        case parse_error_code::invalid_destructor:
            return "Invalid destructor";
        case parse_error_code::invalid_override:
            return "Invalid override specification";

        // Statement errors
        case parse_error_code::invalid_statement:
            return "Invalid statement";
        case parse_error_code::invalid_break:
            return "Break statement outside loop or switch";
        case parse_error_code::invalid_continue:
            return "Continue statement outside loop";
        case parse_error_code::invalid_return:
            return "Invalid return statement";
        case parse_error_code::invalid_switch_case:
            return "Invalid switch case";

        // Type errors
        case parse_error_code::invalid_type_specification:
            return "Invalid type specification";
        case parse_error_code::unknown_template_type:
            return "Unknown template type";

        // Namespace errors
        case parse_error_code::invalid_namespace:
            return "Invalid namespace declaration";
        case parse_error_code::invalid_include:
            return "Invalid include directive";
        case parse_error_code::invalid_import:
            return "Invalid import declaration";

        default:
            return "Unknown parse error";
        }
    }

    const std::error_category& parse_error_category() noexcept {
        static parse_error_category_impl instance;
        return instance;
    }

} // namespace jai
