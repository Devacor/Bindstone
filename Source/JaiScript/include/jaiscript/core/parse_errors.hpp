#pragma once

#ifndef __JAISCRIPT_CORE_PARSE_ERRORS_HPP__
#define __JAISCRIPT_CORE_PARSE_ERRORS_HPP__

#include <system_error>
#include <string>

namespace jai {

    // Error codes for JaiScript parser errors
    // Using std::error_code pattern for zero-overhead error handling
    enum class parse_error_code {
        success = 0,  // Must be 0 for default-constructed error_code

        // Token errors (1-10)
        unexpected_token = 1,
        unexpected_eof = 2,
        expected_token = 3,

        // Expression errors (11-30)
        invalid_expression = 11,
        invalid_assignment_target = 12,
        invalid_increment_target = 13,
        invalid_member_access = 14,

        // Declaration errors (31-50)
        invalid_declaration = 31,
        duplicate_parameter = 32,
        invalid_parameter = 33,
        invalid_class_member = 34,
        invalid_constructor = 35,
        invalid_destructor = 36,
        invalid_override = 37,

        // Statement errors (51-70)
        invalid_statement = 51,
        invalid_break = 52,
        invalid_continue = 53,
        invalid_return = 54,
        invalid_switch_case = 55,

        // Type errors (71-90)
        invalid_type_specification = 71,
        unknown_template_type = 72,

        // Namespace errors (91-100)
        invalid_namespace = 91,
        invalid_include = 92,
        invalid_import = 93,
    };

    // Error category for JaiScript parser
    class parse_error_category_impl : public std::error_category {
    public:
        const char* name() const noexcept override {
            return "jaiscript.parse";
        }

        std::string message(int ev) const override;
    };

    // Global error category instance
    const std::error_category& parse_error_category() noexcept;

    // Convenience function to make error codes
    inline std::error_code make_error_code(parse_error_code e) noexcept {
        return {static_cast<int>(e), parse_error_category()};
    }

} // namespace jai

// Register for implicit conversion to error_code
namespace std {
    template <>
    struct is_error_code_enum<jai::parse_error_code> : true_type {};
}

#endif // __JAISCRIPT_CORE_PARSE_ERRORS_HPP__
