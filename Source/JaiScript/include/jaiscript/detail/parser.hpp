#pragma once

#include "lexer.hpp"
#include "ast.hpp"
#include <jaiscript/core/types.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/parse_errors.hpp>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_set>

namespace jai {

    // Forward declaration
    class string_symbolizer;
    class engine;

    class parser {
    public:
        // One true constructor with dependency injection
        parser(const std::vector<token>& tokens, string_symbolizer* symbolizer, engine* eng, const std::unordered_set<std::string>& registeredTemplateTypes, const std::string& filename = "<script>");

        // Parse the entire program
        checked_result<std::vector<declaration_ptr>> parse();
        
        // Check if parsing had errors
        bool has_errors() const { return !errors_.empty(); }
        const std::vector<std::string>& get_errors() const { return errors_; }
        
    private:
        const std::vector<token>& tokens_;
        std::string filename_;
        size_t current_ = 0;
        std::vector<std::string> errors_;
        std::unordered_set<std::string> registered_template_types_;
        string_symbolizer* symbolizer_ = nullptr;  // Optional: for interning identifiers at parse time
        engine* engine_ = nullptr;  // Engine for interning type_info objects

        // token buffer for handling >> splitting in generic contexts
        std::optional<token> pushed_back_token_;
        mutable token last_advanced_;  // Storage for advance() when consuming pushed_back_token_

        // Current namespace context for nested namespace declarations
        std::vector<std::string> current_namespace_path_;
        
        // Error handling
        void report_error(const std::string& message, const token& token);  // Logs error to errors_ vector
        void synchronize();  // Error recovery
        
        // token management (return by reference to avoid copying strings)
        const token& peek() const;
        const token& previous() const;
        const token& advance();
        bool is_at_end() const;
        bool check(token_type type) const;
        bool match(token_type type);
        bool match(std::initializer_list<token_type> types);
        checked_result<token> consume(token_type type, const std::string& message);
        
        // declaration parsing
        checked_result<declaration_ptr> declaration();
        checked_result<declaration_ptr> class_declaration();
        checked_result<declaration_ptr> namespace_declaration();
        checked_result<declaration_ptr> function_declaration();
        checked_result<declaration_ptr> variable_declaration();
        checked_result<declaration_ptr> include_declaration();
        checked_result<declaration_ptr> import_declaration();
        std::optional<std::string> parse_operator_method_symbol();
        
        // statement parsing
        checked_result<statement_ptr> statement();
        checked_result<statement_ptr> expression_statement();
        checked_result<statement_ptr> block_statement();
        checked_result<statement_ptr> if_statement();
        checked_result<statement_ptr> while_statement();
        checked_result<statement_ptr> for_statement();
        checked_result<statement_ptr> return_statement();
        checked_result<statement_ptr> break_statement();
        checked_result<statement_ptr> continue_statement();
        checked_result<statement_ptr> try_statement();
        checked_result<statement_ptr> switch_statement();
        
        // expression parsing (precedence climbing)
        checked_result<expression_ptr> expression();
        checked_result<expression_ptr> assignment();
        checked_result<expression_ptr> ternary();
        checked_result<expression_ptr> logical_or();
        checked_result<expression_ptr> logical_and();
        checked_result<expression_ptr> bitwise_or();
        checked_result<expression_ptr> bitwise_xor();
        checked_result<expression_ptr> bitwise_and();
        checked_result<expression_ptr> equality();
        checked_result<expression_ptr> relational();
        checked_result<expression_ptr> shift();
        checked_result<expression_ptr> additive();
        checked_result<expression_ptr> multiplicative();
        checked_result<expression_ptr> unary();
        checked_result<expression_ptr> postfix();
        checked_result<expression_ptr> primary();
        
        // Helper parsers
        checked_result<expression_ptr> finish_call(expression_ptr callee);
        checked_result<expression_ptr> finish_member_access(expression_ptr object, bool is_arrow);
        bool looks_like_map_literal();  // Lookahead to distinguish map literals from blocks
        checked_result<expression_ptr> parse_map_literal();
        checked_result<type_info_ptr> parse_type();

        // Constant folding optimization: evaluates literal operations at parse time
        expression_ptr try_constant_fold(expression_ptr left, const token& op, expression_ptr right);

        // Helper to create and store a type_info object, returning a pointer to it
        type_info_ptr store_type_info(type_info&& info);

        checked_result<std::vector<parameter>> parse_parameter_list();

        // Lambda parsing (anonymous `function (params) {...}` desugars to a no-capture
        // lambda through the shared tail)
        checked_result<expression_ptr> lambda_expression();
        checked_result<expression_ptr> anonymous_function_expression();
        checked_result<expression_ptr> finish_lambda_after_captures(std::shared_ptr<lambda_expr> lambda);
        checked_result<std::pair<std::vector<lambda_expr::capture>, lambda_expr::capture_default>> parse_capture_list();

        // Helper for parsing function bodies
        checked_result<declaration_ptr> parse_function_body(std::string_view name, uint64_t name_id, type_info_ptr return_type, bool allow_ctor_initializers = false);

        // Consumes an optional '&' after a return type and wraps it as a reference
        // return (int& f() / -> int& / auto&). No-op when the next token isn't '&'.
        type_info_ptr wrap_reference_return_type(type_info_ptr return_type);
        
        // Helper for parsing > in generic contexts (handles >> token splitting)
        void consume_greater_in_generic(const std::string& message);
        
        // Recursion depth tracking to prevent stack overflow on deeply nested input.
        // NOTE: this must be low enough that REACHING the limit does not itself
        // overflow the native stack (each recursive-descent level consumes a real
        // C++ frame). 1024 was too high: on a 1 MB stack (Windows default) with
        // Debug-sized frames, ~1024-deep unary/expression recursion overflowed the
        // stack BEFORE the guard could fire. 250 leaves comfortable headroom while
        // still allowing any realistic expression nesting depth.
        int parse_depth_ = 0;
        static constexpr int MAX_PARSE_DEPTH = 250;

        // weight: how many depth units this level costs. Constructs whose single
        // recursion level burns many native frames (parenthesised grouping re-enters
        // the whole precedence chain, ~18 frames/level) use a higher weight so the
        // guard fires well before the native stack runs out.
        struct depth_guard {
            int& depth_;
            int weight_;
            bool overflow_ = false;
            depth_guard(int& d, int max, int weight = 1) : depth_(d), weight_(weight) {
                depth_ += weight_;
                overflow_ = (depth_ > max);
            }
            ~depth_guard() { depth_ -= weight_; }
        };

        // Context tracking for context-sensitive parsing
        bool in_switch_case_ = false;  // Track if we're inside a switch case
        bool in_coroutine_ = false;

        // Helper to check if a type name is registered for template parsing
        bool is_registered_template_type(const std::string& type_name) const;

        // Helper to get symbol ID from token - uses pre-interned if available
        uint64_t get_symbol_id(const token& tok) const;

        // ============ Slot-based local variable tracking ============
        // Tracks local variable slots for the current function being parsed.
        // Used for O(1) local variable access at runtime.
        struct function_scope {
            size_t next_slot = 0;  // Next available slot index
            std::unordered_map<uint64_t, size_t> symbol_to_slot;  // symbol_id -> slot
            // Lexical scoping for the lookup map: block scopes log (symbol, previous slot
            // or SIZE_MAX) so end_block_scope restores shadowed/expired names. Slots stay
            // unique per declaration; only the name->slot visibility is scoped.
            std::vector<std::pair<uint64_t, size_t>> shadow_undo;
            std::vector<size_t> block_marks;
        };
        std::vector<function_scope> function_scope_stack_;  // Stack for nested functions/lambdas

        // Enter a new function scope (call at start of function/lambda parsing)
        void enter_function_scope() {
            function_scope_stack_.emplace_back();
        }

        // Exit current function scope, returns the total slot count
        size_t exit_function_scope() {
            if (function_scope_stack_.empty()) return 0;
            size_t count = function_scope_stack_.back().next_slot;
            function_scope_stack_.pop_back();
            return count;
        }

        void begin_block_scope() {
            if (function_scope_stack_.empty()) return;
            auto& scope = function_scope_stack_.back();
            scope.block_marks.push_back(scope.shadow_undo.size());
        }

        void end_block_scope() {
            if (function_scope_stack_.empty()) return;
            auto& scope = function_scope_stack_.back();
            if (scope.block_marks.empty()) return;
            const size_t mark = scope.block_marks.back();
            scope.block_marks.pop_back();
            while (scope.shadow_undo.size() > mark) {
                const auto [symbol_id, previous_slot] = scope.shadow_undo.back();
                scope.shadow_undo.pop_back();
                if (previous_slot == SIZE_MAX) {
                    scope.symbol_to_slot.erase(symbol_id);
                } else {
                    scope.symbol_to_slot[symbol_id] = previous_slot;
                }
            }
        }

        // RAII for begin/end_block_scope across the parser's early-return error paths
        struct block_scope_guard {
            parser& p_;
            explicit block_scope_guard(parser& p) : p_(p) { p_.begin_block_scope(); }
            ~block_scope_guard() { p_.end_block_scope(); }
            block_scope_guard(const block_scope_guard&) = delete;
            block_scope_guard& operator=(const block_scope_guard&) = delete;
        };

        // Allocate a slot for a variable, returns the slot index
        size_t allocate_slot(uint64_t symbol_id) {
            if (function_scope_stack_.empty()) return SIZE_MAX;  // Not in a function
            auto& scope = function_scope_stack_.back();
            size_t slot = scope.next_slot++;
            if (!scope.block_marks.empty()) {
                auto existing = scope.symbol_to_slot.find(symbol_id);
                scope.shadow_undo.emplace_back(symbol_id,
                    existing != scope.symbol_to_slot.end() ? existing->second : SIZE_MAX);
            }
            scope.symbol_to_slot[symbol_id] = slot;
            return slot;
        }

        // Look up a variable's slot index (SIZE_MAX if not found or not in function scope)
        size_t lookup_slot(uint64_t symbol_id) const {
            // Search from innermost to outermost function scope
            for (auto it = function_scope_stack_.rbegin(); it != function_scope_stack_.rend(); ++it) {
                auto found = it->symbol_to_slot.find(symbol_id);
                if (found != it->symbol_to_slot.end()) {
                    return found->second;
                }
            }
            return SIZE_MAX;  // Not found - must be global or closure capture
        }

        // Check if we're currently inside a function scope
        bool in_function_scope() const {
            return !function_scope_stack_.empty();
        }
    };

} // namespace jai