#include "../../include/jaiscript/detail/parser.hpp"
#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/detail/interpreter.hpp"  // For string_symbolizer
#include "../../include/jaiscript/detail/integer_ops.hpp"  // checked constant folding
#include <sstream>
#include <iostream>
#include <optional>
#include <cctype>

namespace jai {

namespace {

    // ---- Escape-mark pass (cell reference model) ----------------------------------
    // A variable "ref-escapes" when it may be bound by reference at runtime: it appears
    // as a bare-identifier call argument (callee ref-ness is unknowable statically), as
    // the identifier source of a reference declaration (auto& x = y), or as an explicit
    // by-reference lambda capture. Marked declarations box their storage into a cell
    // (reference_holder cell mode), so reference binding is a handle share and an escaped
    // reference keeps its target alive. Conservative by design: function-level granularity
    // (all same-named decls in a function mark together) and unresolved symbols bubble
    // outward through enclosing functions to the program scope; symbols unresolved even
    // there (cross-execute globals, C++ defines) are handled at bind time instead. Class
    // FIELDS are never registered: field refs use the instance-pinned identity mode.
    struct ref_escape_marker {
        struct scope {
            std::unordered_multimap<uint64_t, variable_decl*> vars;
            std::unordered_multimap<uint64_t, parameter*> params;
            std::unordered_multimap<uint64_t, range_for_stmt*> range_vars;
            std::unordered_multimap<uint64_t, std::pair<destructuring_decl*, size_t>> destructures;
        };
        std::vector<scope> scopes;
        string_symbolizer* symbolizer = nullptr;

        void escape(uint64_t symbol) {
            if (symbol == UINT64_MAX) {
                return;
            }
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                bool found = false;
                auto vr = it->vars.equal_range(symbol);
                for (auto v = vr.first; v != vr.second; ++v) { v->second->ref_escaping = true; found = true; }
                auto pr = it->params.equal_range(symbol);
                for (auto p = pr.first; p != pr.second; ++p) { p->second->ref_escaping = true; found = true; }
                auto rr = it->range_vars.equal_range(symbol);
                for (auto r = rr.first; r != rr.second; ++r) { r->second->var_ref_escaping = true; found = true; }
                auto dr = it->destructures.equal_range(symbol);
                for (auto d = dr.first; d != dr.second; ++d) {
                    destructuring_decl* decl = d->second.first;
                    if (decl->ref_escaping.size() < decl->names.size()) {
                        decl->ref_escaping.resize(decl->names.size(), false);
                    }
                    decl->ref_escaping[d->second.second] = true;
                    found = true;
                }
                if (found) {
                    return;
                }
            }
        }

        void escape_arg(const expression* arg) {
            if (arg && arg->get_type() == node_type::identifier_expr) {
                escape(static_cast<const identifier_expr*>(arg)->symbol_id);
            }
        }

        // True when the innermost scope that declares `symbol` declares it as a
        // reference (ref param / auto& decl / auto& loop var). Unresolved symbols
        // answer false (value-decl semantics is the safe default for stores).
        bool names_reference_decl(uint64_t symbol) const {
            if (symbol == UINT64_MAX) {
                return false;
            }
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                bool found = false;
                bool is_ref = false;
                auto vr = it->vars.equal_range(symbol);
                for (auto v = vr.first; v != vr.second; ++v) {
                    found = true;
                    if (v->second->type && v->second->type->base_type == script_value_type::jai_reference_type) {
                        is_ref = true;
                    }
                }
                auto pr = it->params.equal_range(symbol);
                for (auto p = pr.first; p != pr.second; ++p) {
                    found = true;
                    if (p->second->is_reference) {
                        is_ref = true;
                    }
                }
                auto rr = it->range_vars.equal_range(symbol);
                for (auto r = rr.first; r != rr.second; ++r) {
                    found = true;
                    if (r->second->is_reference) {
                        is_ref = true;
                    }
                }
                auto dr = it->destructures.equal_range(symbol);
                if (dr.first != dr.second) {
                    found = true;   // destructured names are always value decls
                }
                if (found) {
                    return is_ref;
                }
            }
            return false;
        }

        void walk_args(const std::vector<expression_ptr>& args) {
            for (const auto& a : args) {
                escape_arg(a.get());
                walk_expr(a.get());
            }
        }

        // Innermost function's ref-return-ness: return statements inside a reference-
        // returning function bind their operand as a reference (return_stmt stamping)
        std::vector<char> ref_return_stack;

        void walk_function(std::vector<parameter>& params, statement* body,
                           const std::vector<constructor_initializer>* inits = nullptr,
                           bool returns_reference = false) {
            scopes.emplace_back();
            ref_return_stack.push_back(returns_reference ? 1 : 0);
            for (auto& p : params) {
                if (p.symbol_id == UINT64_MAX && symbolizer) {
                    p.symbol_id = symbolizer->intern(p.name);
                }
                scopes.back().params.emplace(p.symbol_id, &p);
                if (p.default_value) {
                    walk_expr(p.default_value.get());
                }
            }
            if (inits) {
                for (const auto& ci : *inits) {
                    walk_args(ci.arguments);
                }
            }
            walk_stmt(body);
            ref_return_stack.pop_back();
            scopes.pop_back();
        }

        void walk_expr(expression* e) {
            if (!e) {
                return;
            }
            switch (e->get_type()) {
                case node_type::identifier_expr: {
                    auto* id = static_cast<identifier_expr*>(e);
                    id->names_value_decl = !names_reference_decl(id->symbol_id);
                    break;
                }
                case node_type::binary_expr: {
                    auto* b = static_cast<binary_expr*>(e);
                    walk_expr(b->left.get());
                    walk_expr(b->right.get());
                    break;
                }
                case node_type::unary_expr:
                    walk_expr(static_cast<unary_expr*>(e)->operand.get());
                    break;
                case node_type::assignment_expr: {
                    auto* a = static_cast<assignment_expr*>(e);
                    walk_expr(a->target.get());
                    walk_expr(a->value.get());
                    break;
                }
                case node_type::call_expr: {
                    auto* c = static_cast<call_expr*>(e);
                    walk_expr(c->callee.get());
                    walk_args(c->arguments);
                    break;
                }
                case node_type::member_expr:
                    walk_expr(static_cast<member_expr*>(e)->object.get());
                    break;
                case node_type::lambda_expr: {
                    auto* l = static_cast<lambda_expr*>(e);
                    // Explicit by-ref captures escape the captured variable
                    for (const auto& cap : l->captures) {
                        if (cap.by_reference) {
                            if (cap.symbol_id == UINT64_MAX && symbolizer) {
                                cap.symbol_id = symbolizer->intern(std::string(cap.name));
                            }
                            escape(cap.symbol_id);
                        }
                    }
                    walk_function(l->parameters, l->body.get(), nullptr,
                                  l->return_type && l->return_type->base_type == script_value_type::jai_reference_type);
                    break;
                }
                case node_type::new_expr:
                    walk_args(static_cast<new_expr*>(e)->arguments);
                    break;
                case node_type::include_expr:
                    walk_expr(static_cast<include_expr*>(e)->path_expr.get());
                    break;
                case node_type::ternary_expr: {
                    auto* t = static_cast<ternary_expr*>(e);
                    walk_expr(t->condition.get());
                    walk_expr(t->then_expression.get());
                    walk_expr(t->else_expression.get());
                    break;
                }
                case node_type::array_literal_expr:
                    for (const auto& el : static_cast<array_literal_expr*>(e)->elements) {
                        walk_expr(el.get());
                    }
                    break;
                case node_type::map_literal_expr:
                    for (const auto& [k, v] : static_cast<map_literal_expr*>(e)->entries) {
                        walk_expr(k.get());
                        walk_expr(v.get());
                    }
                    break;
                case node_type::throw_expr:
                    walk_expr(static_cast<throw_expr*>(e)->value.get());
                    break;
                case node_type::yield_expr:
                    walk_expr(static_cast<yield_expr*>(e)->value.get());
                    break;
                default:
                    break;   // literal/identifier/this/super: leaves
            }
        }

        void walk_stmt(statement* s) {
            if (!s) {
                return;
            }
            switch (s->get_type()) {
                case node_type::expression_stmt:
                    walk_expr(static_cast<expression_stmt*>(s)->expression.get());
                    break;
                case node_type::block_stmt:
                    for (const auto& d : static_cast<block_stmt*>(s)->declarations) {
                        walk_stmt(d.get());
                    }
                    break;
                case node_type::if_stmt: {
                    auto* i = static_cast<if_stmt*>(s);
                    walk_expr(i->condition.get());
                    walk_stmt(i->then_statement.get());
                    walk_stmt(i->else_statement.get());
                    break;
                }
                case node_type::while_stmt: {
                    auto* w = static_cast<while_stmt*>(s);
                    walk_expr(w->condition.get());
                    walk_stmt(w->body.get());
                    break;
                }
                case node_type::for_stmt: {
                    auto* f = static_cast<for_stmt*>(s);
                    walk_stmt(f->initializer.get());
                    walk_expr(f->condition.get());
                    walk_expr(f->update.get());
                    walk_stmt(f->body.get());
                    break;
                }
                case node_type::range_for_stmt: {
                    auto* r = static_cast<range_for_stmt*>(s);
                    if (!scopes.empty() && r->variable_name_id != UINT64_MAX) {
                        scopes.back().range_vars.emplace(r->variable_name_id, r);
                    }
                    walk_expr(r->container.get());
                    walk_stmt(r->body.get());
                    break;
                }
                case node_type::return_stmt: {
                    auto* r = static_cast<return_stmt*>(s);
                    if (!ref_return_stack.empty() && ref_return_stack.back()) {
                        // Ref-return producer: the operand binds as a reference; a
                        // bare-identifier operand escapes (its decl boxes from the start)
                        r->binds_reference = true;
                        escape_arg(r->value.get());
                    }
                    walk_expr(r->value.get());
                    break;
                }
                case node_type::try_stmt: {
                    auto* t = static_cast<try_stmt*>(s);
                    walk_stmt(t->try_block.get());
                    walk_stmt(t->catch_block.get());
                    break;
                }
                case node_type::switch_stmt: {
                    auto* sw = static_cast<switch_stmt*>(s);
                    walk_expr(sw->condition.get());
                    for (const auto& c : sw->cases) {
                        walk_stmt(c.get());
                    }
                    walk_stmt(sw->default_case.get());
                    break;
                }
                case node_type::case_stmt: {
                    auto* c = static_cast<case_stmt*>(s);
                    walk_expr(c->value.get());
                    for (const auto& b : c->body) {
                        walk_stmt(b.get());
                    }
                    break;
                }
                case node_type::default_stmt:
                    for (const auto& b : static_cast<default_stmt*>(s)->body) {
                        walk_stmt(b.get());
                    }
                    break;
                case node_type::statement_decl:
                    walk_stmt(static_cast<statement_decl*>(s)->statement.get());
                    break;
                case node_type::variable_decl: {
                    auto* d = static_cast<variable_decl*>(s);
                    // Ref decl: the identifier source escapes (auto& x = y shares y's cell)
                    if (d->type && d->type->base_type == script_value_type::jai_reference_type &&
                        d->initializer && d->initializer->get_type() == node_type::identifier_expr) {
                        escape(static_cast<identifier_expr*>(d->initializer.get())->symbol_id);
                    }
                    walk_expr(d->initializer.get());
                    if (!scopes.empty() && d->name_id != UINT64_MAX) {
                        scopes.back().vars.emplace(d->name_id, d);
                    }
                    break;
                }
                case node_type::function_decl: {
                    auto* fd = static_cast<function_decl*>(s);
                    walk_function(fd->parameters, fd->body.get(), &fd->initializers,
                                  fd->return_type && fd->return_type->base_type == script_value_type::jai_reference_type &&
                                  !fd->is_coroutine);
                    break;
                }
                case node_type::class_decl: {
                    auto* cd = static_cast<class_decl*>(s);
                    for (const auto& m : cd->members) {
                        if (!m.declaration) {
                            continue;
                        }
                        if (m.declaration->get_type() == node_type::function_decl) {
                            walk_stmt(m.declaration.get());
                        } else if (m.declaration->get_type() == node_type::variable_decl) {
                            // Fields never become cells (identity-mode refs); only their
                            // initializer expressions can produce escapes
                            walk_expr(static_cast<variable_decl*>(m.declaration.get())->initializer.get());
                        }
                    }
                    break;
                }
                case node_type::namespace_decl:
                    for (const auto& d : static_cast<namespace_decl*>(s)->declarations) {
                        walk_stmt(d.get());
                    }
                    break;
                case node_type::expression_decl:
                    walk_expr(static_cast<expression_decl*>(s)->expression.get());
                    break;
                case node_type::destructuring_decl: {
                    auto* d = static_cast<destructuring_decl*>(s);
                    walk_expr(d->initializer.get());
                    if (!scopes.empty()) {
                        for (size_t i = 0; i < d->names.size(); ++i) {
                            if (d->names[i].second != UINT64_MAX) {
                                scopes.back().destructures.emplace(d->names[i].second, std::make_pair(d, i));
                            }
                        }
                    }
                    break;
                }
                case node_type::include_decl:
                    walk_expr(static_cast<include_decl*>(s)->path_expr.get());
                    break;
                case node_type::import_decl:
                    walk_expr(static_cast<import_decl*>(s)->path_expr.get());
                    break;
                default:
                    break;   // break/continue/fallthrough/enum: leaves
            }
        }

        void run(const std::vector<declaration_ptr>& program) {
            scopes.emplace_back();   // program scope: top-level decls = globals
            for (const auto& d : program) {
                walk_stmt(d.get());
            }
            scopes.pop_back();
        }
    };

} // namespace

// One true constructor with dependency injection
parser::parser(const std::vector<token>& tokens, string_symbolizer* symbolizer, engine* eng, const std::unordered_set<std::string>& registeredTemplateTypes, const std::string& filename)
    : tokens_(tokens), filename_(filename), current_(0), registered_template_types_(registeredTemplateTypes), symbolizer_(symbolizer), engine_(eng) {
    if (!symbolizer_) {
        throw std::invalid_argument("parser requires a valid string_symbolizer");
    }
    if (!engine_) {
        throw std::invalid_argument("parser requires a valid engine");
    }
}

type_info_ptr parser::store_type_info(type_info&& info) {
    return engine_->get_type_info(info);
}

checked_result<std::vector<declaration_ptr>> parser::parse() {
    std::vector<declaration_ptr> declarations;

    while (!is_at_end()) {
        const size_t errors_before = errors_.size();
        auto decl_result = declaration();

        if (!decl_result) {
            // Surface errors that bypass report_error (e.g. depth-guard failures) —
            // otherwise execute() would return null with no diagnostic at all.
            if (errors_.size() == errors_before && decl_result.error() != std::error_code()) {
                report_error(format_error(decl_result, *symbolizer_), peek());
            }
            synchronize();
        } else {
            auto decl = std::move(decl_result.value());
            if (decl) {
                declarations.push_back(std::move(decl));
            }
        }
    }

    // If there were parse errors, return error carrying the FULL diagnostic list
    // (interned; the engine boundary resolves it via format_error).
    if (!errors_.empty()) {
        std::string all_errors = errors_[0];
        for (size_t i = 1; i < errors_.size(); ++i) {
            all_errors += '\n';
            all_errors += errors_[i];
        }
        uint64_t error_id = symbolizer_->intern(all_errors);
        return checked_result<std::vector<declaration_ptr>>(
            make_error_code(parse_error_code::invalid_expression),
            "Parse error: {0}",
            error_id
        );
    }

    // Escape-mark pass: flag declarations whose storage must be boxed into a cell
    // (see ref_escape_marker above; consumed by both backends at decl/bind time)
    {
        ref_escape_marker marker;
        marker.symbolizer = symbolizer_;
        marker.run(declarations);
    }

    // Mark the last expression declaration as an implicit return
    if (!declarations.empty()) {
        auto& last_decl = declarations.back();
        if (last_decl->get_type() == node_type::expression_decl) {
            static_cast<expression_decl*>(last_decl.get())->implicit_return = true;
        } else if (last_decl->get_type() == node_type::statement_decl) {
            auto* stmt_decl = static_cast<statement_decl*>(last_decl.get());
            // If the last declaration is a statement_decl containing a block,
            // mark the last expression in that block as implicit return
            if (stmt_decl->statement->get_type() == node_type::block_stmt) {
                auto* block = static_cast<block_stmt*>(stmt_decl->statement.get());
                if (!block->declarations.empty()) {
                    if (block->declarations.back()->get_type() == node_type::expression_decl) {
                        static_cast<expression_decl*>(block->declarations.back().get())->implicit_return = true;
                    }
                }
            }
            // If the last declaration is a statement_decl containing an expression_stmt,
            // mark it as implicit return by setting a flag on the statement_decl
            else if (stmt_decl->statement->get_type() == node_type::expression_stmt) {
                auto* expr_stmt = static_cast<expression_stmt*>(stmt_decl->statement.get());
                // We need to mark this expression statement as an implicit return
                // Since expression_stmt doesn't have implicit_return flag, we need to convert
                // this statement_decl to an expression_decl
                auto expr_decl = std::make_shared<expression_decl>(
                    expr_stmt->expression->location,
                    expr_stmt->expression
                );
                expr_decl->implicit_return = true;
                declarations.back() = expr_decl;
            }
        }
    }

    return declarations;
}

// Error handling - logs error to errors_ vector for later reporting
void parser::report_error(const std::string& message, const token& token) {
    std::stringstream ss;
    ss << token.location.to_string() << ": " << message;
    errors_.push_back(ss.str());
}

void parser::synchronize() {
    advance();
    
    while (!is_at_end()) {
        if (previous().type == token_type::semicolon) return;
        
        switch (peek().type) {
            case token_type::class_keyword:
            case token_type::auto_keyword:
            case token_type::var_keyword:
            case token_type::int_keyword:
            case token_type::float_keyword:
            case token_type::string_keyword:
            case token_type::bool_keyword:
            case token_type::char_keyword:
            case token_type::void_keyword:
            case token_type::if_keyword:
            case token_type::while_keyword:
            case token_type::for_keyword:
            case token_type::return_keyword:
            case token_type::try_keyword:
                return;
            default:
                advance();
        }
    }
}

// token management (return by reference to avoid copying strings - major perf win)
const token& parser::peek() const {
    if (pushed_back_token_.has_value()) {
        return pushed_back_token_.value();
    }
    return tokens_[current_];
}

const token& parser::previous() const {
    return tokens_[current_ - 1];
}

const token& parser::advance() {
    if (pushed_back_token_.has_value()) {
        // Move pushed_back token to last_advanced_ so we can return a reference
        last_advanced_ = std::move(pushed_back_token_.value());
        pushed_back_token_.reset();
        return last_advanced_;
    }
    if (!is_at_end()) current_++;
    return previous();
}

bool parser::is_at_end() const {
    return peek().type == token_type::eof;
}

bool parser::check(token_type type) const {
    if (is_at_end()) return false;
    return peek().type == type;
}

bool parser::match(token_type type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool parser::match(std::initializer_list<token_type> types) {
    for (token_type type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

checked_result<token> parser::consume(token_type type, const std::string& message) {
    if (check(type)) return advance();

    // Report error (adds to errors_ vector)
    std::stringstream ss;
    ss << peek().location.to_string() << ": " << message;
    errors_.push_back(ss.str());

    // Return error as checked_result<token>
    // Intern the message so we can pass it as a symbol ID
    uint64_t msg_id = symbolizer_->intern(message);
    return checked_result<token>(
        make_error_code(parse_error_code::expected_token),
        "Expected token: {0}",
        msg_id
    );
}

// Primary expressions
checked_result<expression_ptr> parser::primary() {
    // Map literals parse as a PRIMARY so postfix applies ({"k":1}["k"], {..}.size());
    // they used to short-circuit at assignment level, which silently rejected any
    // postfix on a literal (grammar.md's own splice example was unparseable)
    if (check(token_type::left_brace) && looks_like_map_literal()) {
        return parse_map_literal();
    }

    // Anonymous function expression: `function (params) {...}` (a no-capture lambda).
    // `function name...` never reaches here (declaration() routes it first).
    if (check(token_type::function_keyword) &&
        current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == token_type::left_paren) {
        advance(); // consume 'function'
        return anonymous_function_expression();
    }

    // Literals
    if (match(token_type::true_keyword)) {
        script_value val(script_value::ast_literal_tag{}, true);
        return std::make_shared<literal_expr>(previous().location, val);
    }

    if (match(token_type::false_keyword)) {
        script_value val(script_value::ast_literal_tag{}, false);
        return std::make_shared<literal_expr>(previous().location, val);
    }

    if (match(token_type::null_keyword)) {
        script_value val(script_value::ast_literal_tag{}, std::monostate{});
        return std::make_shared<literal_expr>(previous().location, val);
    }

    if (match(token_type::integer_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.int_value);
        return std::make_shared<literal_expr>(token.location, val);
    }

    if (match(token_type::float_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.float_value);
        return std::make_shared<literal_expr>(token.location, val);
    }

    if (match(token_type::string_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.string_value);
        return std::make_shared<literal_expr>(token.location, val);
    }

    if (match(token_type::char_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.char_value);
        return std::make_shared<literal_expr>(token.location, val);
    }

    // Keywords
    if (match(token_type::this_keyword)) {
        return std::make_shared<this_expr>(previous().location);
    }

    if (match(token_type::super_keyword)) {
        // super:: must be followed by a method name
        token superToken = previous();
        JAISCRIPT_TRY_ASSIGN(token methodName, consume(token_type::identifier, "Expected method name after 'super::'"));

        // Create a member access on super
        auto superExpr = std::make_shared<super_expr>(superToken.location);
        uint64_t member_id = get_symbol_id(methodName);
        return std::make_shared<member_expr>(methodName.location, superExpr, methodName.lexeme, member_id, false);
    }

    // include in expression position: evaluates to the included file's result value
    // (var cfg = include "config.jai"). Statement-position include is claimed earlier
    // by declaration(), so statement semantics are unchanged. Same three path forms as
    // the directive: "path", <path>, (expr).
    if (match(token_type::include_keyword)) {
        token includeToken = previous();
        if (match(token_type::left_paren)) {
            JAISCRIPT_TRY_ASSIGN(expression_ptr path_expr, expression());
            JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after include expression"));
            return std::make_shared<include_expr>(includeToken.location, path_expr);
        }
        std::string path;
        if (match(token_type::string_literal)) {
            path = previous().string_value;
        } else if (match(token_type::less)) {
            std::string path_buffer;
            while (!check(token_type::greater) && !is_at_end()) {
                path_buffer += peek().lexeme;
                advance();
            }
            JAISCRIPT_TRY(consume(token_type::greater, "Expected '>' after include path"));
            path = path_buffer;
        } else {
            report_error("Expected string literal, '<', or '(' after include", peek());
            return make_error_code(parse_error_code::unexpected_token);
        }
        return std::make_shared<include_expr>(includeToken.location, path);
    }

    // new T(args) / new T{args}: pure sugar for shared_ptr<T>(args) construction —
    // the same new_expr the shared_ptr<T>(...) spelling produces (idempotent when T
    // is already shared_ptr<...>)
    if (match(token_type::new_keyword)) {
        token newToken = previous();
        JAISCRIPT_TRY_ASSIGN(type_info_ptr pointee_type, parse_type());

        type_info_ptr type = pointee_type;
        if (!pointee_type || pointee_type->base_type != script_value_type::jai_shared_ptr_type) {
            type_info info(script_value_type::jai_shared_ptr_type);
            info.type_params.push_back(pointee_type);
            if (pointee_type) {
                info.type_name = pointee_type->type_name;
            } else {
                info.type_name = "shared_ptr";
            }
            info.id = symbolizer_->intern(info.canonical_name());
            type = store_type_info(std::move(info));
        }

        if (!check(token_type::left_paren) && !check(token_type::left_brace)) {
            report_error("Expected '(' or '{' after type in 'new' expression", peek());
            return make_error_code(parse_error_code::unexpected_token);
        }
        bool is_brace = check(token_type::left_brace);
        advance(); // consume '(' or '{'

        std::vector<expression_ptr> arguments;
        if (!check(is_brace ? token_type::right_brace : token_type::right_paren)) {
            arguments.reserve(4);
            do {
                JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                arguments.push_back(std::move(arg));
            } while (match(token_type::comma));
        }

        if (is_brace) {
            JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after constructor arguments"));
        } else {
            JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after constructor arguments"));
        }
        return std::make_shared<new_expr>(newToken.location, type, std::move(arguments));
    }

    // Check if a keyword is being used as a namespace identifier (e.g., string::length)
    // This must come before identifier handling
    if (peek().type != token_type::identifier && peek().type != token_type::eof) {
        // Check if this token is followed by ::
        size_t lookAhead = current_ + 1;
        if (lookAhead < tokens_.size() && tokens_[lookAhead].type == token_type::colon_colon) {
            // This is a keyword being used as a namespace name
            token nameToken = advance();
            auto id_expr = std::make_shared<identifier_expr>(nameToken.location, nameToken.lexeme, get_symbol_id(nameToken));
            id_expr->slot_index = lookup_slot(id_expr->symbol_id);
            return id_expr;
        }
    }

    // Identifiers (including potential template constructors)
    if (match(token_type::identifier)) {
        token identToken = previous();
        
        // Check if this might be a templated type like Point<int>
        if (check(token_type::less)) {
            size_t savedPos = current_ - 1; // Save position including the identifier
            current_--; // Back up to re-parse as type
            
            auto type_result = parse_type();
            if (type_result) {
                type_info_ptr type = std::move(type_result.value());
                if (type && check(token_type::left_brace)) {
                    // This is a brace-initialized constructor like array<int>{}
                    advance(); // consume '{'

                    std::vector<expression_ptr> arguments;
                    if (!check(token_type::right_brace)) {
                        arguments.reserve(4);
                        do {
                            JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                            arguments.push_back(std::move(arg));
                        } while (match(token_type::comma));
                    }

                    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after constructor arguments"));
                    return std::make_shared<new_expr>(identToken.location, type, std::move(arguments));
                } else if (type && check(token_type::left_paren)) {
                    // Parentheses constructor for user-defined template types
                    advance(); // consume '('

                    std::vector<expression_ptr> arguments;
                    if (!check(token_type::right_paren)) {
                        do {
                            JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                            arguments.push_back(std::move(arg));
                        } while (match(token_type::comma));
                    }

                    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after constructor arguments"));
                    return std::make_shared<new_expr>(identToken.location, type, std::move(arguments));
                }
            } else {
                // Not a valid template type, continue as regular identifier
            }
            
            // Not a constructor, restore position and continue
            current_ = savedPos + 1;
        }

        auto id_expr = std::make_shared<identifier_expr>(identToken.location, identToken.lexeme, get_symbol_id(identToken));
        id_expr->slot_index = lookup_slot(id_expr->symbol_id);
        return id_expr;
    }

    // Grouped expression. Weighted: one paren level re-enters the entire precedence
    // chain (~18 native frames) but only ticks the shared counter twice, so unweighted
    // nesting overflowed the native stack BELOW the cap (~40 levels on a 1 MB Debug stack).
    if (match(token_type::left_paren)) {
        depth_guard guard(parse_depth_, MAX_PARSE_DEPTH, 6);
        if (guard.overflow_) {
            return checked_result<expression_ptr>(
                make_error_code(parse_error_code::unexpected_token),
                "Maximum expression nesting depth exceeded ({})", MAX_PARSE_DEPTH);
        }
        JAISCRIPT_TRY_ASSIGN(expression_ptr expr, expression());
        JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after expression"));
        return expr;
    }

    // Array literal with [] or Lambda expression
    if (match(token_type::left_bracket)) {
        auto startLoc = previous().location;
        
        // Check for empty array OR empty capture list for lambda
        if (check(token_type::right_bracket)) {
            JAISCRIPT_TRY(consume(token_type::right_bracket, "Expected ']'"));

            // Check if this is followed by '(' which would make it a lambda
            if (check(token_type::left_paren)) {
                // Put back the brackets so lambda_expression can parse them
                current_ -= 2; // Back up to before '['
                return lambda_expression();
            }

            return std::make_shared<array_literal_expr>(startLoc, std::vector<expression_ptr>());
        }
        
        // Save position for potential backtrack to lambda
        size_t savedPos = current_;
        
        // Try to parse as array literal first
        std::vector<expression_ptr> elements;
        bool is_array = true;
        
        // Check if this might be a lambda with default capture [= or [&
        // or explicit capture [this, [x, [&x
        if (check(token_type::equal) || check(token_type::ampersand)) {
            // This is definitely a lambda, not an array
            current_ = savedPos - 1;
            return lambda_expression();
        }

        // Check for [this]() or [identifier]() pattern which is definitely a lambda
        // We need to look ahead further to distinguish from single-element arrays like [x]
        if (check(token_type::this_keyword) || check(token_type::identifier)) {
            // Look ahead to see if this is followed by ] and then (
            size_t lookAhead = current_ + 1;
            if (lookAhead < tokens_.size() && tokens_[lookAhead].type == token_type::right_bracket) {
                // Check if there's a ( after the ]
                size_t lookAhead2 = lookAhead + 1;
                if (lookAhead2 < tokens_.size() && tokens_[lookAhead2].type == token_type::left_paren) {
                    // Pattern is [this]() or [x]() - definitely a lambda
                    current_ = savedPos - 1;
                    return lambda_expression();
                }
            }
            // NOTE: We intentionally do NOT check for [identifier, here anymore
            // because [x, y, z] could be either an array literal OR a lambda capture list.
            // We'll distinguish them later by checking if there's a ( after the closing ].
        }
        
        // Parse first element/expression
        JAISCRIPT_TRY_ASSIGN(auto first_elem, expression());
        elements.push_back(std::move(first_elem));

        // If we see a comma, it's definitely an array
        if (match(token_type::comma)) {
            // Continue parsing array elements
            do {
                JAISCRIPT_TRY_ASSIGN(auto elem, expression());
                elements.push_back(std::move(elem));
            } while (match(token_type::comma));

            JAISCRIPT_TRY(consume(token_type::right_bracket, "Expected ']' after array elements"));
            // Check if this is followed by '(' which would make it a lambda
            if (check(token_type::left_paren)) {
                // Restore position and parse as lambda
                current_ = savedPos - 1;
                return lambda_expression();
            }
            return std::make_shared<array_literal_expr>(startLoc, std::move(elements));
        }
        // If we see ], it might be a single-element array or lambda capture
        else if (match(token_type::right_bracket)) {
            // Check if this is followed by '(' which would make it a lambda
            if (check(token_type::left_paren)) {
                // Restore position and parse as lambda
                current_ = savedPos - 1;
                return lambda_expression();
            }
            return std::make_shared<array_literal_expr>(startLoc, std::move(elements));
        }
        // Otherwise, it might be a lambda capture list
        else {
            // Restore position and parse as lambda
            current_ = savedPos - 1;
            return lambda_expression();
        }
    }
    
    
    // Check for type constructors (array<int>(), map<string, int>(), etc.)
    if (check(token_type::array_keyword) || check(token_type::map_keyword) ||
        check(token_type::weak_ptr_keyword) || check(token_type::shared_ptr_keyword) ||
        check(token_type::int_keyword) || check(token_type::float_keyword) ||
        check(token_type::string_keyword) || check(token_type::bool_keyword) || check(token_type::char_keyword)) {
        size_t savedPos = current_;
        auto type_result = parse_type();
        if (type_result) {
            type_info_ptr type = std::move(type_result.value());
            if (type) {
                // This is a type constructor - convert it to an identifier expression
                // that will be handled by postfix() for () or {} initialization
                current_ = savedPos;
                token typeToken = advance();
                std::string type_name(typeToken.lexeme);

                // For template types, we need to parse the full type
                if (check(token_type::less)) {
                    // This is a template type, parse the full type
                    current_ = savedPos;
                    JAISCRIPT_TRY_ASSIGN(type, parse_type());

                    // Check if this is followed by {} or () for constructor syntax
                    if (check(token_type::left_brace) || check(token_type::left_paren)) {
                        bool is_brace = check(token_type::left_brace);
                        advance(); // consume '{' or '('

                        std::vector<expression_ptr> arguments;
                        if (!check(is_brace ? token_type::right_brace : token_type::right_paren)) {
                            arguments.reserve(4);
                            do {
                                JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                                arguments.push_back(std::move(arg));
                            } while (match(token_type::comma));
                        }

                        if (is_brace) {
                            JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after constructor arguments"));
                        } else {
                            JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after constructor arguments"));
                        }
                        return std::make_shared<new_expr>(typeToken.location, type, std::move(arguments));
                    }

                    // Otherwise return identifier with type name
                    type_name = type->type_name;
                }

                auto [sym_id, stable_view] = symbolizer_->intern_with_view(type_name);
                return std::make_shared<identifier_expr>(typeToken.location, stable_view, sym_id);
            }
        }
        current_ = savedPos;
    }
    
    // Check for custom type constructors (Point<int>(), SafeComponent<Button>(), etc.)
    if (check(token_type::identifier) || check(token_type::user_template_type)) {
        size_t savedPos = current_;
        advance(); // consume identifier or template type

        // Check if this might be a templated constructor
        if (check(token_type::less) || previous().type == token_type::user_template_type) {
            // Backtrack and parse as type
            current_ = savedPos;
            auto type_result = parse_type();
            if (type_result) {
                type_info_ptr type = std::move(type_result.value());
                if (type && check(token_type::left_brace)) {
                    // This is a brace-initialized constructor expression like map<string, int>{}
                    // Parse the arguments and create new_expr directly
                    advance(); // consume '{'

                    std::vector<expression_ptr> arguments;
                    if (!check(token_type::right_brace)) {
                        arguments.reserve(4);
                        do {
                            JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                            arguments.push_back(std::move(arg));
                        } while (match(token_type::comma));
                    }

                    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after constructor arguments"));

                    // Create new_expr with the full type info preserved
                    return std::make_shared<new_expr>(tokens_[savedPos].location, type, std::move(arguments));
                } else if (type && check(token_type::left_paren)) {
                    // Parentheses constructor for template types
                    advance(); // consume '('

                    std::vector<expression_ptr> arguments;
                    if (!check(token_type::right_paren)) {
                        do {
                            JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                            arguments.push_back(std::move(arg));
                        } while (match(token_type::comma));
                    }

                    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after constructor arguments"));
                    return std::make_shared<new_expr>(tokens_[savedPos].location, type, std::move(arguments));
                }
            }
            // Otherwise backtrack and continue
            current_ = savedPos;
        } else {
            // Backtrack - not a template
            current_ = savedPos;
        }
    }
    
    // Super keyword
    if (match(token_type::super_keyword)) {
        return std::make_shared<super_expr>(previous().location);
    }

    // Regular identifier (variable name)
    if (match(token_type::identifier)) {
        token name = previous();
        auto id_expr = std::make_shared<identifier_expr>(name.location, name.lexeme, get_symbol_id(name));
        id_expr->slot_index = lookup_slot(id_expr->symbol_id);
        return id_expr;
    }

    report_error("Expected expression", peek());
    return make_error_code(parse_error_code::unexpected_token);
}

// Type parsing
checked_result<type_info_ptr> parser::parse_type() {
    // Handle auto - type inference (nullptr = infer from initializer, or uninitialized)
    if (match(token_type::auto_keyword)) {
        return type_info_ptr(nullptr);
    }

    // Handle var - dynamic typing (any type allowed, never locks)
    if (match(token_type::var_keyword)) {
        return store_type_info(type_info::make_any(*symbolizer_));
    }

    // Handle function keyword
    if (match(token_type::function_keyword)) {
        return type_info_ptr(nullptr);
    }

    // Primitive types
    if (match(token_type::int_keyword)) {
        return store_type_info(type_info::make_int(*symbolizer_));
    }
    if (match(token_type::float_keyword)) {
        return store_type_info(type_info::make_float(*symbolizer_));
    }
    if (match(token_type::string_keyword)) {
        return store_type_info(type_info::make_string(*symbolizer_));
    }
    if (match(token_type::bool_keyword)) {
        return store_type_info(type_info::make_bool(*symbolizer_));
    }
    if (match(token_type::char_keyword)) {
        return store_type_info(type_info::make_char(*symbolizer_));
    }
    if (match(token_type::void_keyword)) {
        return store_type_info(type_info::make_void(*symbolizer_));
    }

    // Generic types
    if (match(token_type::array_keyword)) {
        JAISCRIPT_TRY(consume(token_type::less, "Expected '<' after 'array'"));
        JAISCRIPT_TRY_ASSIGN(type_info_ptr element_type, parse_type());
        consume_greater_in_generic("Expected '>' after array element type");
        return store_type_info(type_info::make_array(*symbolizer_, element_type));
    }

    if (match(token_type::map_keyword)) {
        JAISCRIPT_TRY(consume(token_type::less, "Expected '<' after 'map'"));
        JAISCRIPT_TRY_ASSIGN(type_info_ptr keyType, parse_type());
        JAISCRIPT_TRY(consume(token_type::comma, "Expected ',' after map key type"));
        JAISCRIPT_TRY_ASSIGN(type_info_ptr valueType, parse_type());
        consume_greater_in_generic("Expected '>' after map value type");
        return store_type_info(type_info::make_map(*symbolizer_, keyType, valueType));
    }

    if (match(token_type::weak_ptr_keyword)) {
        JAISCRIPT_TRY(consume(token_type::less, "Expected '<' after 'weak_ptr'"));
        JAISCRIPT_TRY_ASSIGN(type_info_ptr pointee_type, parse_type());
        consume_greater_in_generic("Expected '>' after weak_ptr type");
        return store_type_info(type_info::make_weak_ptr(*symbolizer_, pointee_type));
    }

    if (match(token_type::shared_ptr_keyword)) {
        JAISCRIPT_TRY(consume(token_type::less, "Expected '<' after 'shared_ptr'"));
        JAISCRIPT_TRY_ASSIGN(type_info_ptr pointee_type, parse_type());
        consume_greater_in_generic("Expected '>' after shared_ptr type");
        // Create a shared_ptr type that wraps the pointee type
        // This ensures reference semantics (no clone on assign)
        type_info info(script_value_type::jai_shared_ptr_type);
        info.type_params.push_back(pointee_type);
        // For shared_ptr<T>, the type_name should be T since it's just reference semantics
        if (pointee_type) {
            info.type_name = pointee_type->type_name;
        } else {
            info.type_name = "shared_ptr";
        }
        info.id = symbolizer_->intern(info.canonical_name());
        return store_type_info(std::move(info));
    }

    // Handle "double" as an alias for float (since we only have float_keyword)
    if (check(token_type::identifier) && peek().lexeme == "double") {
        advance();  // consume "double"
        return store_type_info(type_info::make_float(*symbolizer_));
    }

    // User-defined type (potentially templated)
    if (match({token_type::identifier, token_type::user_template_type})) {
        std::string type_name(previous().lexeme);
        token_type typeToken = previous().type;

        // If it's a user_template_type token, we know it's safe to parse template syntax
        if (typeToken == token_type::user_template_type && match(token_type::less)) {
            // Try to parse as templated type like Point<int> or SafeComponent<Button>
            size_t savedPos = current_ - 1; // Save position after '<'

            std::vector<type_info_ptr> templateParams;

            // Parse template arguments
            // Use a while loop instead of do-while to ensure we have at least one argument
            auto firstParam_result = parse_type();
            if (!firstParam_result) {
                report_error("Expected template parameter", peek());
    return make_error_code(parse_error_code::unexpected_token);
            }
            templateParams.push_back(std::move(firstParam_result.value()));

            while (match(token_type::comma)) {
                auto param_result = parse_type();
                if (!param_result) {
                    report_error("Expected template parameter after ','", peek());
    return make_error_code(parse_error_code::unexpected_token);
                }
                templateParams.push_back(std::move(param_result.value()));
            }

            consume_greater_in_generic("Expected '>' after template parameters");

            // Build the full template type name
            type_name += "<";
            for (size_t i = 0; i < templateParams.size(); ++i) {
                if (i > 0) type_name += ", ";
                if (templateParams[i]) {
                    type_name += templateParams[i]->type_name;
                } else {
                    // Handle null type (shouldn't happen but let's be safe)
                    type_name += "unknown";
                }
            }
            type_name += ">";

            // Create a templated object type
            type_info info = type_info::make_object(*symbolizer_, type_name);
            info.type_params = std::move(templateParams);
            return store_type_info(std::move(info));
        }

        return store_type_info(type_info::make_object(*symbolizer_, type_name));
    }

    report_error("Expected type", peek());
    return make_error_code(parse_error_code::unexpected_token);
}

// Simple expression for now
checked_result<expression_ptr> parser::expression() {
    depth_guard guard(parse_depth_, MAX_PARSE_DEPTH);
    if (guard.overflow_) {
        return checked_result<expression_ptr>(
            make_error_code(parse_error_code::unexpected_token),
            "Maximum expression nesting depth exceeded ({})", MAX_PARSE_DEPTH);
    }
    return assignment();
}

checked_result<expression_ptr> parser::assignment() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, ternary());

    if (match({token_type::equal, token_type::plus_equal, token_type::minus_equal,
               token_type::star_equal, token_type::slash_equal, token_type::percent_equal})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, assignment());
        return std::make_shared<assignment_expr>(op.location, expr, op, right);
    }

    return expr;
}

// Helper function to check if { starts a map literal (as opposed to a block statement)
// This avoids using exceptions for control flow
bool parser::looks_like_map_literal() {
    // Save position for lookahead
    size_t savedPos = current_;

    // Consume the {
    advance();

    // Empty braces {} is an empty map
    if (check(token_type::right_brace)) {
        current_ = savedPos;
        return true;
    }

    // Check for keywords that indicate a block statement, not a map
    if (check(token_type::auto_keyword) || check(token_type::var_keyword) ||
        check(token_type::if_keyword) || check(token_type::while_keyword) ||
        check(token_type::for_keyword) || check(token_type::return_keyword) ||
        check(token_type::class_keyword) || check(token_type::function_keyword)) {
        current_ = savedPos;
        return false;
    }

    // Check for function calls: identifier followed by (
    // This indicates a block with statements, not a map
    if (check(token_type::identifier)) {
        advance(); // consume identifier
        if (check(token_type::left_paren)) {
            current_ = savedPos;
            return false;
        }
        current_ = savedPos + 1; // back to after {
    }

    // Check for JSON-style map: identifier/string followed by :
    if (check(token_type::identifier) || check(token_type::string_literal)) {
        advance();
        bool isMap = check(token_type::colon);
        current_ = savedPos;
        return isMap;
    }

    // Check for C++ style map: {{ ...
    if (check(token_type::left_brace)) {
        current_ = savedPos;
        return true;
    }

    // Default: not a map (could be block or other construct)
    current_ = savedPos;
    return false;
}

checked_result<expression_ptr> parser::parse_map_literal() {
    // Nested C++-style map literals ({{...}}) recurse here without passing through
    // expression(); unguarded brace runs overflow the native stack.
    depth_guard guard(parse_depth_, MAX_PARSE_DEPTH);
    if (guard.overflow_) {
        return checked_result<expression_ptr>(
            make_error_code(parse_error_code::unexpected_token),
            "Maximum expression nesting depth exceeded ({})", MAX_PARSE_DEPTH);
    }

    JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{'"));
    auto startLoc = previous().location;

    // Empty map
    if (check(token_type::right_brace)) {
        JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}'"));
        return std::make_shared<map_literal_expr>(startLoc, std::vector<std::pair<expression_ptr, expression_ptr>>());
    }

    // Save position to check for JSON style
    size_t savedPos = current_;

    // Check if it's JSON style by looking for "key": value pattern
    bool isJsonStyle = false;

    // First, check if the first token could be a key (string or identifier)
    if (check(token_type::string_literal) || check(token_type::identifier)) {
        advance(); // consume the potential key
        if (check(token_type::colon)) {
            isJsonStyle = true;
        }
        // Restore position
        current_ = savedPos;
    }

    std::vector<std::pair<expression_ptr, expression_ptr>> entries;
    entries.reserve(8);

    if (isJsonStyle) {
        // Parse JSON style: {key: value, key2: value2} or {"key": value}
        do {
            expression_ptr key;

            // Check if key is a bare identifier (convert to string literal)
            if (check(token_type::identifier)) {
                token keyToken = advance();
                // Convert bare identifier to string literal for JSON-style syntax
                // e.g., {x: 10} becomes {"x": 10}
                script_string keyStr(keyToken.lexeme);
                script_value keyValue(script_value::ast_literal_tag{}, keyStr);
                key = std::make_shared<literal_expr>(keyToken.location, keyValue);
            } else {
                // Parse as expression (for quoted strings or computed keys)
                JAISCRIPT_TRY_ASSIGN(key, expression());
            }

            JAISCRIPT_TRY(consume(token_type::colon, "Expected ':' after key in JSON-style map"));
            JAISCRIPT_TRY_ASSIGN(expression_ptr value, expression());
            entries.emplace_back(std::move(key), std::move(value));
        } while (match(token_type::comma));
    } else {
        // Parse C++ style: {{"key", value}, {"key2", value2}}
        do {
            JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' for map entry"));
            JAISCRIPT_TRY_ASSIGN(expression_ptr key, expression());
            JAISCRIPT_TRY(consume(token_type::comma, "Expected ',' between key and value in map entry"));
            JAISCRIPT_TRY_ASSIGN(expression_ptr value, expression());
            JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after map entry"));

            entries.emplace_back(std::move(key), std::move(value));
        } while (match(token_type::comma));
    }

    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after map entries"));
    return std::make_shared<map_literal_expr>(startLoc, std::move(entries));
}

checked_result<expression_ptr> parser::ternary() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, logical_or());

    if (match(token_type::question)) {
        token questionLoc = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr then_expression, expression());
        JAISCRIPT_TRY(consume(token_type::colon, "Expected ':' after then expression in ternary"));
        JAISCRIPT_TRY_ASSIGN(expression_ptr else_expression, ternary());
        return std::make_shared<ternary_expr>(questionLoc.location, expr, then_expression, else_expression);
    }

    return expr;
}

// expression precedence chain implementations
checked_result<expression_ptr> parser::logical_or() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, logical_and());

    while (match(token_type::pipe_pipe)) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, logical_and());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::logical_and() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, bitwise_or());

    while (match(token_type::ampersand_ampersand)) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, bitwise_or());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::bitwise_or() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, bitwise_xor());

    while (match(token_type::pipe)) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, bitwise_xor());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::bitwise_xor() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, bitwise_and());

    while (match(token_type::caret)) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, bitwise_and());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::bitwise_and() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, equality());

    while (match(token_type::ampersand)) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, equality());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::equality() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, relational());

    while (match({token_type::equal_equal, token_type::bang_equal})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, relational());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::relational() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, shift());

    while (match({token_type::less, token_type::less_equal,
                   token_type::greater, token_type::greater_equal, token_type::spaceship})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, shift());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

checked_result<expression_ptr> parser::shift() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, additive());

    while (match({token_type::left_shift, token_type::right_shift})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, additive());
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }

    return expr;
}

// Constant folding optimization: evaluates literal operations at parse time
// If both operands are literals, compute the result now instead of at runtime
expression_ptr parser::try_constant_fold(expression_ptr left, const token& op, expression_ptr right) {
    if (left->get_type() != node_type::literal_expr || right->get_type() != node_type::literal_expr) {
        // Not both literals, return binary_expr
        return std::make_shared<binary_expr>(op.location, left, op, right);
    }

    auto* leftLit = static_cast<literal_expr*>(left.get());
    auto* rightLit = static_cast<literal_expr*>(right.get());

    const script_value& leftVal = leftLit->value;
    const script_value& rightVal = rightLit->value;

    // Fold integer arithmetic (most common case) through the overflow policy: raw
    // + - * here was signed-overflow UB in the compiler AND a silent wrap that
    // bypassed the checked policy (PARSER-FOLD-WRAPS). On overflow DO NOT fold -
    // the runtime's checked path raises with the correct operator name. Wrap builds
    // still fold (jai::ints wraps UB-free there).
    if (leftVal.is_int() && rightVal.is_int()) {
        script_int leftInt = leftVal.as_int();
        script_int rightInt = rightVal.as_int();
        script_int result = 0;

        switch (op.type) {
            case token_type::plus:
                if (!ints::try_add(leftInt, rightInt, result)) {
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                break;
            case token_type::minus:
                if (!ints::try_sub(leftInt, rightInt, result)) {
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                break;
            case token_type::star:
                if (!ints::try_mul(leftInt, rightInt, result)) {
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                break;
            case token_type::slash:
                // Zero divisor and INT64_MIN/-1: let the runtime raise
                if (rightInt == 0 || !ints::try_div(leftInt, rightInt, result)) {
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                break;
            case token_type::percent:
                if (rightInt == 0) {
                    // Don't fold modulo by zero - let runtime handle it
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                result = ints::mod(leftInt, rightInt);   // -1 divisor: 0, no hardware trap
                break;
            default:
                // Unsupported operation for constant folding
                return std::make_shared<binary_expr>(op.location, left, op, right);
        }

        // Create a folded literal with the computed value
        return std::make_shared<literal_expr>(op.location, script_value(result, leftVal.get_engine()));
    }

    // Fold float arithmetic
    if ((leftVal.is_float() || leftVal.is_int()) && (rightVal.is_float() || rightVal.is_int())) {
        script_float leftFloat = leftVal.is_int() ? static_cast<script_float>(leftVal.as_int()) : leftVal.as_float();
        script_float rightFloat = rightVal.is_int() ? static_cast<script_float>(rightVal.as_int()) : rightVal.as_float();
        script_float result = 0.0;

        switch (op.type) {
            case token_type::plus:
                result = leftFloat + rightFloat;
                break;
            case token_type::minus:
                result = leftFloat - rightFloat;
                break;
            case token_type::star:
                result = leftFloat * rightFloat;
                break;
            case token_type::slash:
                if (rightFloat == 0.0) {
                    // Don't fold division by zero - let runtime handle it
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                result = leftFloat / rightFloat;
                break;
            case token_type::percent:
                if (rightFloat == 0.0) {
                    // Don't fold modulo by zero - let runtime handle it
                    return std::make_shared<binary_expr>(op.location, left, op, right);
                }
                result = std::fmod(leftFloat, rightFloat);
                break;
            default:
                // Unsupported operation for constant folding
                return std::make_shared<binary_expr>(op.location, left, op, right);
        }

        // Create a folded literal with the computed value
        return std::make_shared<literal_expr>(op.location, script_value(result, leftVal.get_engine()));
    }

    // Fold string concatenation
    if (op.type == token_type::plus && leftVal.is_string() && rightVal.is_string()) {
        script_string result = leftVal.as_string() + rightVal.as_string();
        return std::make_shared<literal_expr>(op.location, script_value(result, leftVal.get_engine()));
    }

    // Can't fold this operation
    return std::make_shared<binary_expr>(op.location, left, op, right);
}

checked_result<expression_ptr> parser::additive() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, multiplicative());

    while (match({token_type::plus, token_type::minus})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, multiplicative());
        expr = try_constant_fold(expr, op, right);  // Try constant folding
    }

    return expr;
}

checked_result<expression_ptr> parser::multiplicative() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, unary());

    while (match({token_type::star, token_type::slash, token_type::percent})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, unary());
        expr = try_constant_fold(expr, op, right);  // Try constant folding
    }

    return expr;
}

checked_result<expression_ptr> parser::unary() {
    // unary() recurses into itself for chained prefix operators (e.g. !!!x,
    // - - -x) WITHOUT re-entering expression(), so the depth guard in
    // expression() does not cover it. Guard here too, or adversarial input
    // like a long run of '!' overflows the native stack.
    depth_guard guard(parse_depth_, MAX_PARSE_DEPTH);
    if (guard.overflow_) {
        return checked_result<expression_ptr>(
            make_error_code(parse_error_code::unexpected_token),
            "Maximum expression nesting depth exceeded ({})", MAX_PARSE_DEPTH);
    }

    if (match({token_type::bang, token_type::minus, token_type::plus_plus,
               token_type::minus_minus, token_type::ampersand, token_type::tilde})) {
        token op = previous();
        JAISCRIPT_TRY_ASSIGN(expression_ptr right, unary());
        return std::make_shared<unary_expr>(op.location, op, right);
    }

    if (match(token_type::throw_keyword)) {
        token throw_token = previous();
        expression_ptr value = nullptr;

        // Check if there's an expression after throw (not a semicolon or end of statement)
        if (!check(token_type::semicolon) && !is_at_end()) {
            JAISCRIPT_TRY_ASSIGN(value, expression());
        }

        return std::make_shared<throw_expr>(throw_token.location, value);
    }

    if (match(token_type::yield_keyword)) {
        token yield_token = previous();
        if (!in_coroutine_) {
            report_error("yield can only be used inside a coroutine function", yield_token);
            return checked_result<expression_ptr>(
                make_error_code(parse_error_code::unexpected_token),
                "yield can only be used inside a coroutine function");
        }
        expression_ptr value = nullptr;
        if (!check(token_type::semicolon) && !check(token_type::right_paren) &&
            !check(token_type::right_brace) && !check(token_type::comma) &&
            !is_at_end()) {
            JAISCRIPT_TRY_ASSIGN(value, expression());
        }
        return std::make_shared<yield_expr>(yield_token.location, std::move(value));
    }

    return postfix();
}

checked_result<expression_ptr> parser::postfix() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, primary());

    while (true) {
        if (match(token_type::left_paren)) {
            JAISCRIPT_TRY_ASSIGN(expr, finish_call(expr));
        } else if (match(token_type::left_brace)) {
            // Brace initialization: Type{args...}
            // This should only be valid if expr is an identifier (type name)
            if (expr->get_type() == node_type::identifier_expr) {
                auto* identExpr = static_cast<identifier_expr*>(expr.get());
                std::vector<expression_ptr> arguments;

                if (!check(token_type::right_brace)) {
                    // Reserve capacity for constructor arguments
                    arguments.reserve(4);
                    do {
                        JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                        arguments.push_back(std::move(arg));
                    } while (match(token_type::comma));
                }

                JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after constructor arguments"));

                // Create a new_expr for object construction
                // The type name is in the identifier
                auto type_info = store_type_info(type_info::make_object(*symbolizer_, identExpr->name));
                expr = std::make_shared<new_expr>(identExpr->location, type_info, std::move(arguments));
            } else {
                report_error("Brace initialization can only be used with type names", previous());
    return make_error_code(parse_error_code::unexpected_token);
            }
        } else if (match(token_type::dot) || match(token_type::question_dot)) {
            bool is_null_safe = (previous().type == token_type::question_dot);
            JAISCRIPT_TRY_ASSIGN(expr, finish_member_access(expr, false));
            if (is_null_safe) {
                static_cast<member_expr*>(expr.get())->null_safe = true;
            }
        } else if (match(token_type::arrow)) {
            JAISCRIPT_TRY_ASSIGN(expr, finish_member_access(expr, true));
        } else if (match(token_type::colon_colon)) {
            // Static member access or namespace member access
            // Allow keywords as member names (for accessing namespace members)
            token name = peek();
            if (name.type == token_type::identifier ||
                (name.type != token_type::eof && name.type != token_type::left_paren &&
                 name.type != token_type::right_paren && name.type != token_type::semicolon)) {
                advance();
                uint64_t member_id = get_symbol_id(name);
                expr = std::make_shared<member_expr>(name.location, expr, name.lexeme, member_id, false, true);
            } else {
                report_error("Expected member name after '::'", name);
    return make_error_code(parse_error_code::unexpected_token);
            }
        } else if (match(token_type::left_bracket)) {
            // Array subscript
            JAISCRIPT_TRY_ASSIGN(expression_ptr index, expression());
            JAISCRIPT_TRY(consume(token_type::right_bracket, "Expected ']' after array index"));
            token op(token_type::left_bracket, "[", previous().location);
            expr = std::make_shared<binary_expr>(op.location, expr, op, index);
        } else if (match({token_type::plus_plus, token_type::minus_minus})) {
            // Postfix increment/decrement
            token op = previous();
            expr = std::make_shared<unary_expr>(op.location, op, expr, true);  // true = postfix
        } else {
            break;
        }
    }

    return expr;
}

// Stub for declaration
checked_result<declaration_ptr> parser::declaration() {
    // Statement-level recursion (blocks, if-chains) needs the same protection as
    // expressions: declaration->statement->block->declaration is unbounded.
    depth_guard guard(parse_depth_, MAX_PARSE_DEPTH);
    if (guard.overflow_) {
        return checked_result<declaration_ptr>(
            make_error_code(parse_error_code::unexpected_token),
            "Maximum statement nesting depth exceeded ({})", MAX_PARSE_DEPTH);
    }

    if (match(token_type::class_keyword)) {
        JAISCRIPT_TRY_ASSIGN(auto result, class_declaration());
        match(token_type::semicolon);  // Consume optional semicolon after class
        return result;
    }
    if (match(token_type::namespace_keyword)) {
        return namespace_declaration();
    }

    // Check for include/import directives
    if (match(token_type::include_keyword)) {
        JAISCRIPT_TRY_ASSIGN(auto result, include_declaration());
        match(token_type::semicolon);  // Consume optional trailing semicolon
        return result;
    }
    if (match(token_type::import_keyword)) {
        JAISCRIPT_TRY_ASSIGN(auto result, import_declaration());
        match(token_type::semicolon);  // Consume optional trailing semicolon
        return result;
    }

    // Check for enum keyword
    if (match(token_type::enum_keyword)) {
        JAISCRIPT_TRY_ASSIGN(token name_tok, consume(token_type::identifier, "Expected enum name"));
        auto [name_id, name_view] = symbolizer_->intern_with_view(name_tok.lexeme);
        JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' after enum name"));

        auto decl = std::make_shared<enum_decl>(name_tok.location, name_view, name_id);

        if (!check(token_type::right_brace)) {
            do {
                JAISCRIPT_TRY_ASSIGN(token val_tok, consume(token_type::identifier, "Expected enum value name"));
                auto [val_id, val_view] = symbolizer_->intern_with_view(val_tok.lexeme);
                decl->values.push_back({val_view, val_id});
            } while (match(token_type::comma));
        }

        JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after enum values"));
        return decl;
    }

    if (match(token_type::coroutine_keyword)) {
        bool was_in_coroutine = in_coroutine_;
        in_coroutine_ = true;
        JAISCRIPT_TRY_ASSIGN(auto result, function_declaration());
        in_coroutine_ = was_in_coroutine;
        auto* func = static_cast<function_decl*>(result.get());
        func->is_coroutine = true;
        // A suspended fiber's return channel is a value channel: ref returns are
        // function-only (the epilogue would flatten anyway - make it explicit)
        if (func->return_type && func->return_type->base_type == script_value_type::jai_reference_type) {
            report_error("Coroutines cannot return references", previous());
            return make_error_code(parse_error_code::unexpected_token);
        }
        match(token_type::semicolon);
        return result;
    }

    // Check specifically for function keyword to handle function declarations
    // (`function (` falls through to expression parsing — anonymous function expression)
    if (check(token_type::function_keyword) &&
        !(current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == token_type::left_paren)) {
        // `function name = expr;` / `function name;` declares a function-typed VARIABLE
        // (parse_type maps `function` to auto, matching function-typed parameters and the
        // namespace path); `function name(` stays a function declaration.
        if (current_ + 2 < tokens_.size() &&
            tokens_[current_ + 1].type == token_type::identifier &&
            (tokens_[current_ + 2].type == token_type::equal ||
             tokens_[current_ + 2].type == token_type::semicolon)) {
            return variable_declaration();
        }
        JAISCRIPT_TRY_ASSIGN(auto result, function_declaration());
        // After parsing a function, consume optional semicolon but don't require it
        match(token_type::semicolon);
        return result;
    }

    // Check for explicit type keywords that start declarations
    if (match({token_type::auto_keyword, token_type::var_keyword, token_type::int_keyword, token_type::float_keyword,
               token_type::string_keyword, token_type::bool_keyword, token_type::char_keyword, token_type::void_keyword,
               token_type::array_keyword, token_type::map_keyword, token_type::weak_ptr_keyword, token_type::shared_ptr_keyword})) {

        // Check for destructuring: auto [x, y, z] = expr;
        if ((previous().type == token_type::auto_keyword || previous().type == token_type::var_keyword)
            && check(token_type::left_bracket)) {
            advance();  // consume [

            auto loc = previous().location;
            auto decl = std::make_shared<destructuring_decl>(loc, nullptr);

            do {
                JAISCRIPT_TRY_ASSIGN(token name_tok, consume(token_type::identifier, "Expected variable name in destructuring"));
                auto [sym_id, sym_view] = symbolizer_->intern_with_view(name_tok.lexeme);
                decl->names.push_back({sym_view, sym_id});

                if (in_function_scope()) {
                    decl->slot_indices.push_back(allocate_slot(sym_id));
                } else {
                    decl->slot_indices.push_back(SIZE_MAX);
                }
            } while (match(token_type::comma));

            JAISCRIPT_TRY(consume(token_type::right_bracket, "Expected ']' after destructuring names"));
            JAISCRIPT_TRY(consume(token_type::equal, "Expected '=' after destructuring pattern"));

            JAISCRIPT_TRY_ASSIGN(decl->initializer, expression());
            JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after destructuring declaration"));

            return decl;
        }

        // Check if this keyword is followed by :: - if so, it's a namespace access, not a type
        if (check(token_type::colon_colon)) {
            // This is a namespace access expression like string::length(), not a variable declaration
            // Back up and let it fall through to expression parsing
            current_--;
            // Fall through to expression parsing below
        } else {
            // We already consumed the type keyword, so we need to backtrack
            current_--;

            // Look ahead to determine if this is a function or variable declaration
            size_t savedPos = current_;
            JAISCRIPT_TRY(parse_type()); // consume the type

        // Skip optional & for reference types
        if (check(token_type::ampersand)) {
            advance();
        }

        // Check if this is followed by an identifier (variable/function name)
        if (check(token_type::identifier)) {
            advance(); // consume identifier
            if (check(token_type::left_paren)) {
                // This is a function declaration
                current_ = savedPos;
                return function_declaration();
            }
            // Otherwise it's a variable declaration
            current_ = savedPos;
            return variable_declaration();
        } else if (check(token_type::left_paren) || check(token_type::left_brace)) {
            // This is a constructor expression like array<int>() or array<int>{}
            current_ = savedPos;
            // Fall through to parse as expression
            } else {
                // No identifier after type, so it's a variable declaration
                current_ = savedPos;
                return variable_declaration();
            }
        }
    }

    // Check if this might be a type name followed by an identifier (for custom types)
    // We need to look ahead to see if this is a declaration
    if (check(token_type::identifier) || check(token_type::user_template_type)) {
        // If it's a user_template_type, we know it's a type declaration
        if (peek().type == token_type::user_template_type) {
            return variable_declaration();
        }

        // Save current position
        size_t savedPos = current_;
        std::string firstIdentifier(peek().lexeme);

        // Look ahead to see if this is "identifier identifier" pattern
        advance(); // consume first identifier

        // Check for template syntax after the identifier
        if (check(token_type::less)) {
            // Might be a templated type like Point<int>
            // Try to parse the full type
            current_ = savedPos;
            size_t typeParsePos = current_;
            auto type_result = parse_type();
            if (type_result) {
                type_info_ptr type = std::move(type_result.value());
                if (type && check(token_type::identifier)) {
                    // We have a type followed by an identifier - it's a declaration
                    current_ = savedPos;
                    return variable_declaration();
                }
            }
            current_ = savedPos;
        } else if (check(token_type::identifier)) {
            // Pattern: identifier identifier - a declaration. A '(' after the second
            // identifier makes it a class-typed free function (C++ return-type-first,
            // same as the namespace path); otherwise a variable: Point p; / Point mk() {...}
            advance(); // consume second identifier
            const bool is_function = check(token_type::left_paren);
            current_ = savedPos;
            if (is_function) {
                return function_declaration();
            }
            return variable_declaration();
        } else if (check(token_type::ampersand) &&
                   current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == token_type::identifier &&
                   current_ + 2 < tokens_.size() && tokens_[current_ + 2].type == token_type::left_paren) {
            // Pattern: ClassName& name( - a class-typed reference-return free function
            // (Box& get() { ... }). Declaration wins like the identifier-identifier
            // form above; a bitwise-and expression spells it (Box) & get().
            current_ = savedPos;
            return function_declaration();
        } else {
            // Not a simple declaration pattern, restore and parse as expression
            current_ = savedPos;
            // Fall through to parse as expression
        }
    }

    // Try parsing as expression first (includes map literals, array literals, etc.)
    // If it fails, we'll try parsing as statement
    if (check(token_type::left_brace)) {
        // Use lookahead to determine if this is a map literal or block statement
        // This avoids exception-based control flow
        if (looks_like_map_literal()) {
            // Looks like a map literal - parse as expression
            JAISCRIPT_TRY_ASSIGN(auto expr, expression());

            // Allow semicolon to be optional at end of file for single expressions
            if (!is_at_end()) {
                JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after expression"));
            }

            // Create an expression_decl for top-level expressions
            return std::make_shared<expression_decl>(expr->location, expr);
        } else {
            // Looks like a block statement - parse as statement
            JAISCRIPT_TRY_ASSIGN(auto stmt, statement());
            return std::make_shared<statement_decl>(stmt->location, stmt);
        }
    }

    // Check for other statements that can appear at top level
    if (check(token_type::if_keyword) || check(token_type::while_keyword) || check(token_type::for_keyword) ||
        check(token_type::return_keyword) || check(token_type::break_keyword) || check(token_type::continue_keyword) ||
        check(token_type::try_keyword) || check(token_type::switch_keyword) || check(token_type::fallthrough_keyword)) {
        // We need to wrap the statement in a declaration since parse() returns declarations
        JAISCRIPT_TRY_ASSIGN(auto stmt, statement());
        // Create a statement_decl to wrap statements at the top level
        return std::make_shared<statement_decl>(stmt->location, stmt);
    }

    // Otherwise it's a top-level expression statement
    JAISCRIPT_TRY_ASSIGN(auto expr, expression());

    // Allow semicolon to be optional at end of file for single expressions
    if (!is_at_end()) {
        JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after expression"));
    }

    // Create an expression_decl for top-level expressions
    return std::make_shared<expression_decl>(expr->location, expr);
}

// Helper method implementations
checked_result<expression_ptr> parser::finish_call(expression_ptr callee) {
    std::vector<expression_ptr> arguments;

    if (!check(token_type::right_paren)) {
        // Reserve capacity for common case of 2-4 arguments
        arguments.reserve(4);
        do {
            JAISCRIPT_TRY_ASSIGN(auto arg, expression());
            arguments.push_back(std::move(arg));
        } while (match(token_type::comma));
    }

    JAISCRIPT_TRY_ASSIGN(token paren, consume(token_type::right_paren, "Expected ')' after arguments"));

    return std::make_shared<call_expr>(paren.location, callee, std::move(arguments));
}

checked_result<expression_ptr> parser::finish_member_access(expression_ptr object, bool is_arrow) {
    JAISCRIPT_TRY_ASSIGN(token name, consume(token_type::identifier, "Expected member name"));
    uint64_t member_id = get_symbol_id(name);
    return std::make_shared<member_expr>(name.location, object, name.lexeme, member_id, is_arrow);
}

checked_result<std::vector<parameter>> parser::parse_parameter_list() {
    std::vector<parameter> params;

    if (!check(token_type::right_paren)) {
        do {
            // Parse parameter without const support (const is only for C++ binding)
            bool is_const = false;

            type_info_ptr type = nullptr;
            std::string name;
            bool is_reference = false;

            // Check for :name syntax (shorthand for auto: name)
            if (match(token_type::colon)) {
                // :name means auto type
                type = nullptr; // nullptr means auto
                JAISCRIPT_TRY_ASSIGN(token name_tok, consume(token_type::identifier, "Expected parameter name after ':'"));
                name = name_tok.lexeme;
            }
            // Check for type: name syntax
            else if (check(token_type::identifier) || check(token_type::auto_keyword) || check(token_type::var_keyword) ||
                     check(token_type::function_keyword) || check(token_type::int_keyword) || check(token_type::float_keyword) ||
                     check(token_type::string_keyword) || check(token_type::bool_keyword) || check(token_type::char_keyword) ||
                     check(token_type::void_keyword) || check(token_type::array_keyword) || check(token_type::map_keyword) ||
                     check(token_type::weak_ptr_keyword) || check(token_type::shared_ptr_keyword)) {

                JAISCRIPT_TRY_ASSIGN(type, parse_type());

                if (match(token_type::colon)) {
                    // type: name syntax
                    JAISCRIPT_TRY_ASSIGN(token name_tok, consume(token_type::identifier, "Expected parameter name after ':'"));
                    name = name_tok.lexeme;
                } else if (check(token_type::ampersand) || check(token_type::identifier)) {
                    // Traditional type name syntax
                    // Check for reference
                    is_reference = match(token_type::ampersand);
                    JAISCRIPT_TRY_ASSIGN(token name_tok, consume(token_type::identifier, "Expected parameter name"));
                    name = name_tok.lexeme;
                } else if (check(token_type::comma) || check(token_type::right_paren) || check(token_type::equal)) {
                    // No identifier after type - treat the type as the parameter name with auto type
                    // This handles shorthand like: void foo(x) where x is untyped, including
                    // untyped defaults foo(x = 3) (the '=' is consumed by the shared default
                    // parse below)
                    if (type && type->base_type == script_value_type::jai_object_type && !type->type_name.empty()) {
                        name = type->type_name;
                        type = nullptr; // Auto type
                    } else {
                        report_error("Expected parameter name", peek());
    return make_error_code(parse_error_code::unexpected_token);
                    }
                } else {
                    report_error("Expected parameter name", peek());
    return make_error_code(parse_error_code::unexpected_token);
                }
            }
            // No type specified, error
            else {
                report_error("Expected parameter type or ':' for auto parameter", peek());
    return make_error_code(parse_error_code::unexpected_token);
            }

            parameter param(type, name, is_reference, is_const);
            param.symbol_id = symbolizer_->intern(name);

            if (match(token_type::equal)) {
                JAISCRIPT_TRY_ASSIGN(param.default_value, expression());
            }

            params.push_back(std::move(param));
        } while (match(token_type::comma));
    }

    bool seen_default = false;
    for (const auto& p : params) {
        if (p.default_value) {
            seen_default = true;
        } else if (seen_default) {
            report_error("Parameter without default value cannot follow parameter with default value", peek());
            return make_error_code(parse_error_code::unexpected_token);
        }
    }

    return params;
}

// Lambda expression parsing
checked_result<expression_ptr> parser::lambda_expression() {
    auto lambda = std::make_shared<lambda_expr>(peek().location);

    // Parse capture list
    JAISCRIPT_TRY(consume(token_type::left_bracket, "Expected '[' for lambda"));
    JAISCRIPT_TRY_ASSIGN(auto capture_result, parse_capture_list());
    lambda->captures = std::move(capture_result.first);
    lambda->default_capture = capture_result.second;
    JAISCRIPT_TRY(consume(token_type::right_bracket, "Expected ']' after capture list"));

    return finish_lambda_after_captures(std::move(lambda));
}

// Anonymous function expression: `function (params) {...}` — pure desugar to a
// no-capture lambda ([]-equivalent: auto-capture, outer locals snapshot BY VALUE
// at creation; explicit by-ref capture needs the lambda spelling)
checked_result<expression_ptr> parser::anonymous_function_expression() {
    auto lambda = std::make_shared<lambda_expr>(previous().location);
    return finish_lambda_after_captures(std::move(lambda));
}

// Shared tail for [] lambdas and anonymous function expressions: parameters,
// optional trailing return, scope/slot bookkeeping, body
checked_result<expression_ptr> parser::finish_lambda_after_captures(std::shared_ptr<lambda_expr> lambda) {
    // Parse parameters
    JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' for lambda parameters"));
    JAISCRIPT_TRY_ASSIGN(lambda->parameters, parse_parameter_list());
    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after parameters"));

    // Parse return type if specified
    if (match(token_type::arrow)) {
        // Check if return type is specified or if we go directly to {
        if (check(token_type::left_brace)) {
            // -> { means auto return type
            lambda->return_type = nullptr; // nullptr means auto
        } else {
            JAISCRIPT_TRY_ASSIGN(lambda->return_type, parse_type());
            lambda->return_type = wrap_reference_return_type(lambda->return_type);
        }
    }

    // Enter function scope for slot-based local variable tracking
    enter_function_scope();

    // Assign slots to parameters (they get slots 0, 1, 2, ...)
    for (auto& param : lambda->parameters) {
        if (param.symbol_id != UINT64_MAX) {
            param.slot_index = allocate_slot(param.symbol_id);
        }
    }

    // Parse body
    if (!match(token_type::left_brace)) {
        exit_function_scope();  // Clean up on error
        report_error("Expected '{' for lambda body", peek());
        return make_error_code(parse_error_code::unexpected_token);
    }
    JAISCRIPT_TRY_ASSIGN(auto body_stmt, block_statement());
    lambda->body = std::dynamic_pointer_cast<block_stmt>(body_stmt);

    // Exit function scope and record total slot count
    lambda->local_count = exit_function_scope();

    return lambda;
}

checked_result<std::pair<std::vector<lambda_expr::capture>, lambda_expr::capture_default>> parser::parse_capture_list() {
    std::vector<lambda_expr::capture> captures;
    lambda_expr::capture_default default_mode = lambda_expr::capture_default::none;

    // Helper lambda to parse a capture variable name (identifier or 'this')
    // Returns interned string_view to ensure permanent storage
    auto parse_capture_name = [this]() -> checked_result<std::string_view> {
        if (check(token_type::this_keyword)) {
            advance();  // consume 'this'
            // "this" is pre-interned in symbolizer
            return symbolizer_->get_string(symbolizer_->get_this_id());
        } else {
            JAISCRIPT_TRY_ASSIGN(token name_tok, consume(token_type::identifier, "Expected capture variable name or 'this'"));
            // Token lexeme already points to symbolizer storage for identifiers
            return name_tok.lexeme;
        }
    };

    if (!check(token_type::right_bracket)) {
        // Check for default capture first
        if (check(token_type::equal)) {
            // [=...] - capture all by value (with possible exceptions)
            advance(); // consume '='
            default_mode = lambda_expr::capture_default::by_value;

            // Check for exceptions after comma
            if (match(token_type::comma)) {
                do {
                    bool byRef = match(token_type::ampersand);
                    JAISCRIPT_TRY_ASSIGN(std::string_view name, parse_capture_name());
                    captures.emplace_back(name, byRef);
                } while (match(token_type::comma));
            }
        } else if (check(token_type::ampersand)) {
            // Could be [&...] or [&variable]
            advance(); // consume '&'

            if (check(token_type::comma) || check(token_type::right_bracket)) {
                // [&...] - capture all by reference (with possible exceptions)
                default_mode = lambda_expr::capture_default::by_reference;

                // Check for exceptions after comma
                if (match(token_type::comma)) {
                    do {
                        bool byRef = false; // Explicit variables after [&,] are by value unless prefixed with &
                        if (match(token_type::ampersand)) {
                            byRef = true;
                        }
                        JAISCRIPT_TRY_ASSIGN(std::string_view name, parse_capture_name());
                        captures.emplace_back(name, byRef);
                    } while (match(token_type::comma));
                }
            } else {
                // [&variable] - specific variable by reference
                JAISCRIPT_TRY_ASSIGN(std::string_view name, parse_capture_name());
                captures.emplace_back(name, true);

                // Continue parsing other captures
                while (match(token_type::comma)) {
                    bool byRef = match(token_type::ampersand);
                    JAISCRIPT_TRY_ASSIGN(std::string_view varName, parse_capture_name());
                    captures.emplace_back(varName, byRef);
                }
            }
        } else {
            // Regular explicit captures: [x, y, &z, this]
            do {
                bool byRef = match(token_type::ampersand);
                JAISCRIPT_TRY_ASSIGN(std::string_view name, parse_capture_name());
                captures.emplace_back(name, byRef);
            } while (match(token_type::comma));
        }
    }

    return std::make_pair(captures, default_mode);
}

// statement parsing implementations
checked_result<statement_ptr> parser::statement() {
    // statement->if_statement->statement (and friends) recurse without passing
    // through declaration(); guard here too.
    depth_guard guard(parse_depth_, MAX_PARSE_DEPTH);
    if (guard.overflow_) {
        return checked_result<statement_ptr>(
            make_error_code(parse_error_code::unexpected_token),
            "Maximum statement nesting depth exceeded ({})", MAX_PARSE_DEPTH);
    }

    if (match(token_type::left_brace)) return block_statement();
    if (match(token_type::if_keyword)) return if_statement();
    if (match(token_type::while_keyword)) return while_statement();
    if (match(token_type::for_keyword)) return for_statement();
    if (match(token_type::return_keyword)) return return_statement();
    if (match(token_type::break_keyword)) return break_statement();
    if (match(token_type::continue_keyword)) return continue_statement();
    if (match(token_type::try_keyword)) return try_statement();
    if (match(token_type::switch_keyword)) return switch_statement();

    // Check for fallthrough keyword
    if (match(token_type::fallthrough_keyword)) {
        if (!in_switch_case_) {
            report_error("'fallthrough' can only be used inside a switch case", previous());
    return make_error_code(parse_error_code::unexpected_token);
        }
        auto fallthrough = std::make_shared<fallthrough_stmt>(previous().location);
        JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after 'fallthrough'"));
        return fallthrough;
    }

    return expression_statement();
}

checked_result<statement_ptr> parser::block_statement() {
    token leftBrace = previous();
    std::vector<declaration_ptr> declarations;

    // Locals declared in this block stop resolving by name once it closes
    block_scope_guard block_scope(*this);

    while (!check(token_type::right_brace) && !is_at_end()) {
        JAISCRIPT_TRY_ASSIGN(declaration_ptr decl, declaration());
        declarations.push_back(std::move(decl));
    }

    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after block"));

    return std::make_shared<block_stmt>(leftBrace.location, std::move(declarations));
}

checked_result<statement_ptr> parser::expression_statement() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, expression());
    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after expression"));
    return std::make_shared<expression_stmt>(expr->location, expr);
}

checked_result<statement_ptr> parser::if_statement() {
    token ifToken = previous();

    JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after 'if'"));
    JAISCRIPT_TRY_ASSIGN(expression_ptr condition, expression());
    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after if condition"));

    JAISCRIPT_TRY_ASSIGN(statement_ptr then_statement, statement());
    statement_ptr else_statement = nullptr;

    if (match(token_type::else_keyword)) {
        JAISCRIPT_TRY_ASSIGN(else_statement, statement());
    }

    return std::make_shared<if_stmt>(ifToken.location, condition, then_statement, else_statement);
}

checked_result<statement_ptr> parser::while_statement() {
    token whileToken = previous();

    JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after 'while'"));
    JAISCRIPT_TRY_ASSIGN(expression_ptr condition, expression());
    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after while condition"));

    JAISCRIPT_TRY_ASSIGN(statement_ptr body, statement());

    return std::make_shared<while_stmt>(whileToken.location, condition, body);
}

checked_result<statement_ptr> parser::for_statement() {
    token forToken = previous();

    JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after 'for'"));

    // Save position for potential backtracking
    size_t savedPosition = current_;

    // Try to parse as range-based for loop first
    // This will be: [const] type [&] identifier : expression
    bool is_const = match(token_type::const_keyword);
    bool is_reference = false;
    type_info_ptr element_type = nullptr;

    // Check if we have a type followed by optional & and identifier : pattern
    if (check(token_type::auto_keyword) || check(token_type::var_keyword) || check(token_type::int_keyword) ||
        check(token_type::float_keyword) || check(token_type::string_keyword) || check(token_type::bool_keyword) ||
        check(token_type::char_keyword) || check(token_type::identifier)) {

        // Parse the type
        auto element_type_result = parse_type();
        if (!element_type_result) {
            // Failed to parse type, restore position and try traditional for
            current_ = savedPosition;
            goto traditional_for;
        }
        element_type = std::move(element_type_result.value());

        // Check for reference after type
        if (match(token_type::ampersand)) {
            is_reference = true;
        }

        // Must have an identifier
        if (check(token_type::identifier)) {
            token varName = advance();

            // Check for colon - this indicates range-based for
            if (match(token_type::colon)) {
                // This is a range-based for loop!
                // Parse the container in the OUTER scope (it must not see the
                // loop variable's slot).
                JAISCRIPT_TRY_ASSIGN(expression_ptr container, expression());
                JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after range expression"));

                // Allocate the loop variable's slot BEFORE parsing the body so
                // that references to it inside the body resolve to this slot.
                // The loop variable's name stops resolving after the loop closes.
                block_scope_guard loop_var_scope(*this);
                uint64_t var_id = get_symbol_id(varName);
                size_t var_slot = SIZE_MAX;
                if (in_function_scope() && var_id != UINT64_MAX) {
                    var_slot = allocate_slot(var_id);
                }

                JAISCRIPT_TRY_ASSIGN(statement_ptr body, statement());

                auto result = std::make_shared<range_for_stmt>(
                    forToken.location, element_type, varName.lexeme, var_id,
                    is_reference, is_const, container, body
                );
                result->variable_slot_index = var_slot;

                return result;
            }
        }
    }

    // Not a range-based for loop, restore position and parse as traditional for
    current_ = savedPosition;

traditional_for:
    // Init
    declaration_ptr init = nullptr;
    if (match(token_type::semicolon)) {
        // No init
    } else if (check(token_type::auto_keyword) || check(token_type::var_keyword) ||
               check(token_type::int_keyword) || check(token_type::float_keyword) ||
               check(token_type::string_keyword) || check(token_type::bool_keyword) ||
               check(token_type::char_keyword) || check(token_type::array_keyword) ||
               check(token_type::map_keyword) || check(token_type::identifier)) {
        // Parse variable declaration without consuming the semicolon
        // (the for loop will handle it)
        JAISCRIPT_TRY_ASSIGN(type_info_ptr type, parse_type());

        // Check for reference after type
        if (match(token_type::ampersand)) {
            type_info refType(script_value_type::jai_reference_type);
            refType.type_name = type ? (type->type_name + "&") : "auto&";
            refType.type_params.push_back(type);
            refType.id = symbolizer_->intern(refType.canonical_name());
            type = store_type_info(std::move(refType));
        }

        JAISCRIPT_TRY_ASSIGN(token name, consume(token_type::identifier, "Expected variable name"));

        expression_ptr initializer = nullptr;
        if (match(token_type::equal)) {
            JAISCRIPT_TRY_ASSIGN(initializer, expression());
        }

        init = std::make_shared<variable_decl>(name.location, type, name.lexeme, get_symbol_id(name), initializer);
        // Note: NOT consuming semicolon here - the for loop will handle it
    } else {
        // Expression-only init (e.g., function call side effect) — wrap in expression_decl
        JAISCRIPT_TRY_ASSIGN(expression_ptr expr, expression());
        JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after for loop initializer"));
        init = std::make_shared<expression_decl>(expr->location, std::move(expr));
    }

    // Consume semicolon after init
    if (init) {
        JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after for loop initializer"));
    }

    // Condition
    expression_ptr condition = nullptr;
    if (!check(token_type::semicolon)) {
        JAISCRIPT_TRY_ASSIGN(condition, expression());
    }
    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after for loop condition"));

    // Update
    expression_ptr update = nullptr;
    if (!check(token_type::right_paren)) {
        JAISCRIPT_TRY_ASSIGN(update, expression());
    }
    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after for loop clauses"));

    JAISCRIPT_TRY_ASSIGN(statement_ptr body, statement());

    return std::make_shared<for_stmt>(forToken.location, init, condition, update, body);
}

checked_result<statement_ptr> parser::return_statement() {
    token returnToken = previous();

    expression_ptr value = nullptr;
    if (!check(token_type::semicolon)) {
        JAISCRIPT_TRY_ASSIGN(value, expression());
    }

    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after return value"));

    return std::make_shared<return_stmt>(returnToken.location, value);
}

checked_result<statement_ptr> parser::break_statement() {
    token breakToken = previous();
    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after 'break'"));
    return std::make_shared<break_stmt>(breakToken.location);
}

checked_result<statement_ptr> parser::continue_statement() {
    token continueToken = previous();
    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after 'continue'"));
    return std::make_shared<continue_stmt>(continueToken.location);
}

checked_result<statement_ptr> parser::try_statement() {
    token tryToken = previous();

    // Parse try block
    JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' after 'try'"));
    JAISCRIPT_TRY_ASSIGN(statement_ptr try_block, block_statement());

    // Must have catch block
    JAISCRIPT_TRY(consume(token_type::catch_keyword, "Expected 'catch' after try block"));

    // Optional catch variable
    std::string catch_var;
    if (match(token_type::left_paren)) {
        JAISCRIPT_TRY_ASSIGN(token var_name, consume(token_type::identifier, "Expected variable name in catch"));
        catch_var = var_name.lexeme;
        JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after catch variable"));
    }

    // Parse catch block
    JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' after 'catch'"));
    JAISCRIPT_TRY_ASSIGN(statement_ptr catch_block, block_statement());

    return std::make_shared<try_stmt>(tryToken.location, try_block, catch_block, catch_var);
}

// declaration parsing implementations
checked_result<declaration_ptr> parser::class_declaration() {
    JAISCRIPT_TRY_ASSIGN(token className, consume(token_type::identifier, "Expected class name"));

    std::vector<std::string_view> base_classes;
    if (match(token_type::colon)) {
        do {
            JAISCRIPT_TRY_ASSIGN(token base_class_tok, consume(token_type::identifier, "Expected base class name"));
            // Token lexeme already points to symbolizer storage (identifiers are interned during lexing)
            base_classes.emplace_back(base_class_tok.lexeme);
        } while (match(token_type::comma));
    }

    JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' before class body"));

    // Intern the class name at parse time for fast comparisons later
    // Use intern_with_view to ensure we get a string_view pointing to symbolizer storage
    auto [class_name_id, class_name_view] = symbolizer_->intern_with_view(className.lexeme);
    auto classDecl = std::make_shared<class_decl>(className.location, class_name_view, class_name_id);
    classDecl->base_classes = std::move(base_classes);

    // Parse class members
    class_decl::member_visibility visibility = class_decl::Public;
    const bool class_entry_in_coroutine = in_coroutine_;

    while (!check(token_type::right_brace) && !is_at_end()) {
        // Error-recovery `continue`s below can leave in_coroutine_ armed; every member
        // starts from the class-entry state
        in_coroutine_ = class_entry_in_coroutine;

        // Check for visibility specifiers
        if (match(token_type::public_keyword)) {
            auto colon_result = consume(token_type::colon, "Expected ':' after 'public'");
            if (!colon_result) {
                synchronize();
                continue;
            }
            visibility = class_decl::Public;
            continue;
        }
        if (match(token_type::private_keyword)) {
            auto colon_result = consume(token_type::colon, "Expected ':' after 'private'");
            if (!colon_result) {
                synchronize();
                continue;
            }
            visibility = class_decl::Private;
            continue;
        }
        if (match(token_type::protected_keyword)) {
            auto colon_result = consume(token_type::colon, "Expected ':' after 'protected'");
            if (!colon_result) {
                synchronize();
                continue;
            }
            visibility = class_decl::Protected;
            continue;
        }

        // Coroutine methods: 'coroutine' precedes a typed or 'function'-keyword method
        // (free-function syntax parity); constructors/destructors/fields reject below
        bool member_is_coroutine = false;
        if (match(token_type::coroutine_keyword)) {
            member_is_coroutine = true;
            in_coroutine_ = true;
        }

        // Parse member declaration
        declaration_ptr member;

        // Check for constructor: ClassName(params)
        // But NOT a field like: ClassName fieldName = ...
        // We need to look ahead to distinguish
        if (check(token_type::identifier) && peek().lexeme == className.lexeme) {
            // Look ahead to see if this is followed by '(' (constructor) or identifier (field)
            size_t lookAhead = current_ + 1;
            if (lookAhead < tokens_.size() && tokens_[lookAhead].type == token_type::left_paren) {
                // This is a constructor - use class name's pre-interned symbol
                advance(); // consume class name
                auto [ctor_name_id, ctor_name_view] = symbolizer_->intern_with_view(className.lexeme);
                auto body_result = parse_function_body(ctor_name_view, ctor_name_id, nullptr, /*allow_ctor_initializers=*/true);
                if (!body_result) {
                    synchronize();
                    continue;
                }
                member = std::move(body_result.value());
                // TODO: Parse constructor delegation syntax (: base(args), : this(args))
                // Currently no support for constructor chaining
            } else {
                // This is a field with the class type, fall through to regular member parsing
                bool is_static = match(token_type::static_keyword);
                bool is_override = match(token_type::override_keyword);

                if (is_static && check(token_type::coroutine_keyword)) {
                    report_error("static coroutine methods are not supported", peek());
                    synchronize();
                    continue;
                }

                auto type_result = parse_type();
                if (!type_result) {
                    synchronize();
                    continue;
                }
                type_info_ptr type = std::move(type_result.value());

                // Continue with regular member parsing...
                if (check(token_type::identifier)) {
                    token name = advance();
                    std::string method_name(name.lexeme);

                    // Typed operator methods returning the enclosing class land HERE (the
                    // ctor lookahead saw no '('): consume the operator symbol exactly like
                    // the general typed path so `Money operator+(Money o)` parses inside
                    // class Money (open question #11, FIXED 2026-07)
                    if (method_name == "operator") {
                        auto op_name = parse_operator_method_symbol();
                        if (!op_name) {
                            synchronize();
                            continue;
                        }
                        method_name = std::move(*op_name);
                    }

                    if (match(token_type::left_paren)) {
                        // Function
                        current_--;
                        auto [fall_name_id, fall_name_view] = symbolizer_->intern_with_view(method_name);
                        auto func = std::make_shared<function_decl>(previous().location, fall_name_view);
                        func->name_id = fall_name_id;
                        func->is_static = is_static;
                        func->is_override = is_override;

                        // Validate: static methods cannot use override keyword
                        if (is_static && is_override) {
                            report_error("Static methods cannot use 'override' keyword - they are not virtual", name);
                            synchronize();
                            continue;
                        }

                        auto open_paren_result = consume(token_type::left_paren, "Expected '(' after function name");
                        if (!open_paren_result) {
                            synchronize();
                            continue;
                        }

                        auto params_result = parse_parameter_list();
                        if (!params_result) {
                            synchronize();
                            continue;
                        }
                        func->parameters = std::move(params_result.value());

                        auto close_paren_result = consume(token_type::right_paren, "Expected ')' after parameters");
                        if (!close_paren_result) {
                            synchronize();
                            continue;
                        }

                        if (match(token_type::arrow)) {
                            if (check(token_type::left_brace)) {
                                // -> { adds no information: keep the leading type
                                func->return_type = type;
                            } else {
                                auto return_type_result = parse_type();
                                if (!return_type_result) {
                                    synchronize();
                                    continue;
                                }
                                func->return_type = wrap_reference_return_type(std::move(return_type_result.value()));
                                if (type && func->return_type && func->return_type->id != type->id) {
                                    report_error("Conflicting return types '" + type->type_name + "' and '" +
                                                 func->return_type->type_name + "' (leading and trailing '->' types must match)", previous());
                                    synchronize();
                                    continue;
                                }
                            }
                        } else {
                            func->return_type = type;
                        }

                        if (match(token_type::override_keyword)) {
                            func->is_override = true;
                            // Validate: static methods cannot use override keyword
                            if (is_static) {
                                report_error("Static methods cannot use 'override' keyword - they are not virtual", previous());
                                synchronize();
                                continue;
                            }
                        }

                        if (check(token_type::colon)) {
                            // Initializer lists are constructor-only (Dev ruling 2026-07); on methods
                            // they were silently parsed and ignored - a trap
                            report_error("Constructor initializer lists (': super(...)' / ': this(...)') are only allowed on constructors", peek());
                            synchronize();
                            continue;
                        }

                        if (!match(token_type::left_brace)) {
                            report_error("Expected '{' before function body", peek());
                            synchronize();
                            continue;
                        }

                        // Enter function scope for slot-based local variable tracking
                        enter_function_scope();

                        // Assign slots to parameters
                        for (auto& param : func->parameters) {
                            if (param.symbol_id == UINT64_MAX) {
                                param.symbol_id = symbolizer_->intern(param.name);
                            }
                            param.slot_index = allocate_slot(param.symbol_id);
                        }

                        auto body_result = block_statement();
                        if (!body_result) {
                            exit_function_scope();  // Clean up on error
                            synchronize();
                            continue;
                        }
                        func->body = std::dynamic_pointer_cast<block_stmt>(body_result.value());
                        func->local_count = exit_function_scope();
                        member = func;
                    } else {
                        // Variable
                        expression_ptr init = nullptr;
                        if (match(token_type::equal)) {
                            auto init_result = expression();
                            if (!init_result) {
                                synchronize();
                                continue;
                            }
                            init = std::move(init_result.value());
                        }

                        auto semicolon_result = consume(token_type::semicolon, "Expected ';' after field declaration");
                        if (!semicolon_result) {
                            synchronize();
                            continue;
                        }

                        auto var_decl = std::make_shared<variable_decl>(name.location, type, name.lexeme, get_symbol_id(name), init);
                        var_decl->is_static = is_static;
                        member = var_decl;
                    }
                } else {
                    report_error("Expected member name", peek());
                    synchronize();
                    continue;
                }
            }
        } else if (match(token_type::tilde)) {
            auto destructor_name_result = consume(token_type::identifier, "Expected class name after '~'");
            if (!destructor_name_result) {
                synchronize();
                continue;
            }
            // Intern destructor name (e.g., "~ClassName")
            std::string dtor_name_str = "~" + std::string(className.lexeme);
            auto [dtor_name_id, dtor_name_view] = symbolizer_->intern_with_view(dtor_name_str);
            auto body_result = parse_function_body(dtor_name_view, dtor_name_id, nullptr);
            if (!body_result) {
                synchronize();
                continue;
            }
            member = std::move(body_result.value());
            // TODO: Mark destructor as virtual if class has any virtual methods
            // Currently no virtual destructor support
        } else if (check(token_type::function_keyword) &&
                   current_ + 2 < tokens_.size() &&
                   tokens_[current_ + 1].type == token_type::identifier &&
                   tokens_[current_ + 1].lexeme != "operator" &&
                   (tokens_[current_ + 2].type == token_type::equal ||
                    tokens_[current_ + 2].type == token_type::semicolon)) {
            // `function name = expr;` / `function name;` is a function-typed FIELD
            // (auto semantics, same as top level); fall through to the regular member
            // branch whose parse_type maps `function` to auto. `function operator=` is
            // excluded - that stays an operator method.
            advance(); // consume 'function'
            token name = advance();
            expression_ptr init = nullptr;
            if (match(token_type::equal)) {
                auto init_result = expression();
                if (!init_result) {
                    synchronize();
                    continue;
                }
                init = std::move(init_result.value());
            }
            auto semicolon_result = consume(token_type::semicolon, "Expected ';' after field declaration");
            if (!semicolon_result) {
                synchronize();
                continue;
            }
            auto var_decl = std::make_shared<variable_decl>(name.location, nullptr, name.lexeme, get_symbol_id(name), init);
            member = var_decl;
        } else if (match(token_type::function_keyword)) {
            // Method declaration with 'function' keyword: function name(...) or function operator=(...)
            bool is_static = false;
            bool is_override = false;

            // Check for static/override modifiers after function keyword
            while (check(token_type::static_keyword) || check(token_type::override_keyword)) {
                if (match(token_type::static_keyword)) is_static = true;
                if (match(token_type::override_keyword)) is_override = true;
            }

            std::string method_name;
            source_location method_location = peek().location;

            // Check for operator overload: function operator= or function operator+
            if (check(token_type::identifier) && peek().lexeme == "operator") {
                advance(); // consume 'operator'
                auto op_name = parse_operator_method_symbol();
                if (!op_name) {
                    synchronize();
                    continue;
                }
                method_name = std::move(*op_name);
            } else if (check(token_type::identifier)) {
                // Regular method name
                token name_tok = advance();
                method_name = std::string(name_tok.lexeme);
            } else {
                report_error("Expected method name or 'operator' after 'function'", peek());
                synchronize();
                continue;
            }

            // Intern method name and parse function body
            auto [method_name_id, method_name_view] = symbolizer_->intern_with_view(method_name);
            auto body_result = parse_function_body(method_name_view, method_name_id, nullptr);
            if (!body_result) {
                synchronize();
                continue;
            }
            auto func = std::dynamic_pointer_cast<function_decl>(body_result.value());
            if (func) {
                func->is_static = is_static;
                func->is_override = is_override;
            }
            member = std::move(body_result.value());
        } else {
            // Regular member (variable or function)
            bool is_static = match(token_type::static_keyword);
            bool is_override = match(token_type::override_keyword);

            if (is_static && check(token_type::coroutine_keyword)) {
                report_error("static coroutine methods are not supported", peek());
                synchronize();
                continue;
            }

            auto type_result = parse_type();
            if (!type_result) {
                synchronize();
                continue;
            }
            type_info_ptr type = std::move(type_result.value());

            if (check(token_type::identifier)) {
                token name = advance();
                std::string method_name(name.lexeme);

                // Check for operator overload: function operator=(Type arg) or function operator+(...)
                if (method_name == "operator") {
                    auto op_name = parse_operator_method_symbol();
                    if (!op_name) {
                        synchronize();
                        continue;
                    }
                    method_name = std::move(*op_name);
                }

                if (match(token_type::left_paren)) {
                    // Function - we need to parse parameters and check for override before body
                    current_--; // Back up to before '('

                    // Intern method name FIRST to get permanent storage for string_view
                    auto [method_name_id, method_name_view] = symbolizer_->intern_with_view(method_name);

                    // Create function declaration with interned name
                    auto func = std::make_shared<function_decl>(name.location, method_name_view);
                    func->name_id = method_name_id;
                    func->is_static = is_static;
                    func->is_override = is_override; // Set from earlier check

                    // Parse parameters
                    auto open_paren_result = consume(token_type::left_paren, "Expected '(' after function name");
                    if (!open_paren_result) {
                        synchronize();
                        continue;
                    }

                    auto params_result = parse_parameter_list();
                    if (!params_result) {
                        synchronize();
                        continue;
                    }
                    func->parameters = std::move(params_result.value());

                    auto close_paren_result = consume(token_type::right_paren, "Expected ')' after parameters");
                    if (!close_paren_result) {
                        synchronize();
                        continue;
                    }

                    // Handle trailing return type
                    if (match(token_type::arrow)) {
                        if (check(token_type::left_brace)) {
                            // -> { adds no information: keep the leading type
                            func->return_type = type;
                        } else {
                            auto return_type_result = parse_type();
                            if (!return_type_result) {
                                synchronize();
                                continue;
                            }
                            func->return_type = wrap_reference_return_type(std::move(return_type_result.value()));
                            if (type && func->return_type && func->return_type->id != type->id) {
                                report_error("Conflicting return types '" + type->type_name + "' and '" +
                                             func->return_type->type_name + "' (leading and trailing '->' types must match)", previous());
                                synchronize();
                                continue;
                            }
                        }
                    } else {
                        func->return_type = type; // Use declared type
                    }

                    // Check for override keyword after parameters (alternative position)
                    if (match(token_type::override_keyword)) {
                        func->is_override = true;
                    }

                    // Validate: static methods cannot use override keyword
                    if (func->is_static && func->is_override) {
                        report_error("Static methods cannot use 'override' keyword - they are not virtual", name);
                        synchronize();
                        continue;
                    }

                    // Parse constructor initialization list if present
                    if (check(token_type::colon)) {
                        // Initializer lists are constructor-only (Dev ruling 2026-07); on methods
                        // they were silently parsed and ignored - a trap
                        report_error("Constructor initializer lists (': super(...)' / ': this(...)') are only allowed on constructors", peek());
                        synchronize();
                        continue;
                    }

                    // Now parse the body
                    if (!match(token_type::left_brace)) {
                        report_error("Expected '{' before function body", peek());
                        synchronize();
                        continue;
                    }

                    // Enter function scope for slot-based local variable tracking
                    enter_function_scope();

                    // Assign slots to parameters
                    for (auto& param : func->parameters) {
                        if (param.symbol_id == UINT64_MAX) {
                            param.symbol_id = symbolizer_->intern(param.name);
                        }
                        param.slot_index = allocate_slot(param.symbol_id);
                    }

                    auto body_result = block_statement();
                    if (!body_result) {
                        exit_function_scope();  // Clean up on error
                        synchronize();
                        continue;
                    }
                    func->body = std::dynamic_pointer_cast<block_stmt>(body_result.value());
                    func->local_count = exit_function_scope();

                    member = func;
                } else {
                    // Variable
                    expression_ptr init = nullptr;
                    if (match(token_type::equal)) {
                        auto init_result = expression();
                        if (!init_result) {
                            synchronize();
                            continue;
                        }
                        init = std::move(init_result.value());
                    }

                    auto semicolon_result = consume(token_type::semicolon, "Expected ';' after field declaration");
                    if (!semicolon_result) {
                        synchronize();
                        continue;
                    }

                    auto var_decl = std::make_shared<variable_decl>(name.location, type, name.lexeme, get_symbol_id(name), init);
                    var_decl->is_static = is_static;
                    member = var_decl;
                }
            } else {
                report_error("Expected member name", peek());
                synchronize();
                continue;
            }
        }

        in_coroutine_ = class_entry_in_coroutine;
        if (member_is_coroutine) {
            auto* func_member = member && member->get_type() == node_type::function_decl
                ? static_cast<function_decl*>(member.get()) : nullptr;
            if (!func_member) {
                report_error("'coroutine' in a class body must be followed by a method", peek());
                synchronize();
                continue;
            }
            if (func_member->is_static) {
                report_error("static coroutine methods are not supported", peek());
                synchronize();
                continue;
            }
            if (func_member->name == className.lexeme ||
                (!func_member->name.empty() && func_member->name[0] == '~')) {
                report_error("constructors and destructors cannot be coroutines", peek());
                synchronize();
                continue;
            }
            func_member->is_coroutine = true;
        }
        classDecl->members.push_back({visibility, member});
    }

    in_coroutine_ = class_entry_in_coroutine;
    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after class body"));

    return classDecl;
}

checked_result<declaration_ptr> parser::namespace_declaration() {
    // Accept identifier or any keyword as namespace name
    // Supports: namespace string {}, namespace mylib {}, namespace my::nested::nspace {}
    std::vector<std::string> namespace_path;
    source_location start_loc = peek().location;

    // Parse first namespace name
    token first_name = advance();
    if (first_name.type == token_type::eof ||
        first_name.type == token_type::left_brace ||
        first_name.type == token_type::right_brace ||
        first_name.type == token_type::left_paren ||
        first_name.type == token_type::right_paren ||
        first_name.type == token_type::semicolon ||
        first_name.type == token_type::colon_colon) {
        report_error("Expected namespace name", first_name);
    return make_error_code(parse_error_code::unexpected_token);
    }
    namespace_path.emplace_back(first_name.lexeme);

    // Check for :: (nested namespace syntax like C++17)
    while (match(token_type::colon_colon)) {
        token next_name = advance();
        if (next_name.type == token_type::eof ||
            next_name.type == token_type::left_brace ||
            next_name.type == token_type::right_brace ||
            next_name.type == token_type::left_paren ||
            next_name.type == token_type::right_paren ||
            next_name.type == token_type::semicolon ||
            next_name.type == token_type::colon_colon) {
            report_error("Expected namespace name after '::'", next_name);
    return make_error_code(parse_error_code::unexpected_token);
        }
        namespace_path.emplace_back(next_name.lexeme);
    }

    JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' before namespace body"));

    // Build the full namespace name by prepending the current namespace context
    // For nested namespaces: namespace outer { namespace inner {} }
    // When parsing "inner", current_namespace_path_ = ["outer"], so we create "outer::inner"
    std::string full_namespace_name;

    // Prepend current namespace context
    if (!current_namespace_path_.empty()) {
        full_namespace_name = current_namespace_path_[0];
        for (size_t i = 1; i < current_namespace_path_.size(); ++i) {
            full_namespace_name += "::" + current_namespace_path_[i];
        }
        full_namespace_name += "::";
    }

    // Append the new namespace path
    full_namespace_name += namespace_path[0];
    for (size_t i = 1; i < namespace_path.size(); ++i) {
        full_namespace_name += "::" + namespace_path[i];
    }

    // Intern the namespace name at parse time for fast comparisons later
    auto [ns_id, ns_view] = symbolizer_->intern_with_view(full_namespace_name);
    auto namespace_decl_node = std::make_shared<namespace_decl>(start_loc, ns_view, ns_id);

    // Push this namespace onto the context stack for nested namespaces
    for (const auto& part : namespace_path) {
        current_namespace_path_.push_back(part);
    }

    // Parse namespace members (functions, classes, variables)
    while (!check(token_type::right_brace) && !is_at_end()) {
        // Parse any declaration (class, function, variable)
        declaration_ptr member_decl = nullptr;

        if (match(token_type::class_keyword)) {
            auto class_result = class_declaration();
            if (!class_result) {
                synchronize();
                continue;
            }
            member_decl = std::move(class_result.value());
            match(token_type::semicolon);  // Consume optional semicolon after class
        } else if (match(token_type::namespace_keyword)) {
            // Nested namespaces
            auto namespace_result = namespace_declaration();
            if (!namespace_result) {
                synchronize();
                continue;
            }
            member_decl = std::move(namespace_result.value());
        } else {
            // Function or variable declaration
            auto type_result = parse_type();
            if (!type_result) {
                synchronize();
                continue;
            }
            type_info_ptr type = std::move(type_result.value());

            if (!check(token_type::identifier)) {
                report_error("Expected member name in namespace", peek());
                synchronize();
                continue;
            }

            token member_name = advance();

            if (match(token_type::left_paren)) {
                // Function declaration
                auto func = std::make_shared<function_decl>(member_name.location, member_name.lexeme);
                func->name_id = get_symbol_id(member_name);
                func->return_type = type;

                // Parse parameters
                auto params_result = parse_parameter_list();
                if (!params_result) {
                    synchronize();
                    continue;
                }
                func->parameters = std::move(params_result.value());

                auto close_paren_result = consume(token_type::right_paren, "Expected ')' after parameters");
                if (!close_paren_result) {
                    synchronize();
                    continue;
                }

                // Check for override keyword after parameters
                bool is_override = match(token_type::override_keyword);
                func->is_override = is_override;

                // Handle trailing return type
                if (match(token_type::arrow)) {
                    if (!check(token_type::left_brace)) {
                        auto return_type_result = parse_type();
                        if (!return_type_result) {
                            synchronize();
                            continue;
                        }
                        func->return_type = wrap_reference_return_type(std::move(return_type_result.value()));
                        if (type && func->return_type && func->return_type->id != type->id) {
                            report_error("Conflicting return types '" + type->type_name + "' and '" +
                                         func->return_type->type_name + "' (leading and trailing '->' types must match)", previous());
                            synchronize();
                            continue;
                        }
                    }
                }

                // Parse function body
                auto open_brace_result = consume(token_type::left_brace, "Expected '{' before function body");
                if (!open_brace_result) {
                    synchronize();
                    continue;
                }

                // Enter function scope for slot-based local variable tracking
                enter_function_scope();

                // Assign slots to parameters
                for (auto& param : func->parameters) {
                    if (param.symbol_id == UINT64_MAX) {
                        param.symbol_id = symbolizer_->intern(param.name);
                    }
                    param.slot_index = allocate_slot(param.symbol_id);
                }

                auto body_result = block_statement();
                if (!body_result) {
                    exit_function_scope();  // Clean up on error
                    synchronize();
                    continue;
                }
                func->body = std::dynamic_pointer_cast<block_stmt>(body_result.value());
                func->local_count = exit_function_scope();

                member_decl = func;
            } else {
                // Variable declaration
                expression_ptr init = nullptr;
                if (match(token_type::equal)) {
                    auto init_result = expression();
                    if (!init_result) {
                        synchronize();
                        continue;
                    }
                    init = std::move(init_result.value());
                }

                auto semicolon_result = consume(token_type::semicolon, "Expected ';' after variable declaration");
                if (!semicolon_result) {
                    synchronize();
                    continue;
                }

                auto var_decl = std::make_shared<variable_decl>(member_name.location, type, member_name.lexeme, get_symbol_id(member_name), init);
                member_decl = var_decl;
            }
        }

        if (member_decl) {
            namespace_decl_node->declarations.push_back(member_decl);
        }
    }

    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after namespace body"));

    // Pop the namespace context (restore to parent namespace or global scope)
    for (size_t i = 0; i < namespace_path.size(); ++i) {
        current_namespace_path_.pop_back();
    }

    return namespace_decl_node;
}

// Consumes the symbol after an 'operator' method name (= + - * / [] < > <= >= == !=).
// Errors are reported here; nullopt tells the caller to synchronize. ONE consumption
// shared by every class-member operator site so their accepted sets can't drift.
std::optional<std::string> parser::parse_operator_method_symbol() {
    if (check(token_type::equal)) { advance(); return std::string("="); }
    if (check(token_type::plus)) { advance(); return std::string("+"); }
    if (check(token_type::minus)) { advance(); return std::string("-"); }
    if (check(token_type::star)) { advance(); return std::string("*"); }
    if (check(token_type::slash)) { advance(); return std::string("/"); }
    if (check(token_type::left_bracket)) {
        advance();
        if (!match(token_type::right_bracket)) {
            report_error("Expected ']' after '[' in operator[]", peek());
            return std::nullopt;
        }
        return std::string("[]");
    }
    if (check(token_type::less)) { advance(); return std::string("<"); }
    if (check(token_type::greater)) { advance(); return std::string(">"); }
    if (check(token_type::less_equal)) { advance(); return std::string("<="); }
    if (check(token_type::greater_equal)) { advance(); return std::string(">="); }
    if (check(token_type::equal_equal)) { advance(); return std::string("=="); }
    if (check(token_type::bang_equal)) { advance(); return std::string("!="); }
    report_error("Expected operator symbol after 'operator' keyword", peek());
    return std::nullopt;
}

checked_result<declaration_ptr> parser::include_declaration() {
    // include "path" or include <path> or include(expr)

    // Check for function-style syntax: include(expr)
    if (match(token_type::left_paren)) {
        // Parse the expression inside parentheses
        JAISCRIPT_TRY_ASSIGN(expression_ptr path_expr, expression());
        JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after include expression"));
        return std::make_shared<include_decl>(previous().location, path_expr);
    }

    // Original literal syntax
    std::string path;

    if (match(token_type::string_literal)) {
        // include "path"
        path = previous().string_value;
    } else if (match(token_type::less)) {
        // include <path>
        // Read until we find >
        std::string path_buffer;
        while (!check(token_type::greater) && !is_at_end()) {
            path_buffer += peek().lexeme;
            advance();
        }
        JAISCRIPT_TRY(consume(token_type::greater, "Expected '>' after include path"));
        path = path_buffer;
    } else {
        report_error("Expected string literal, '<', or '(' after include", peek());
    return make_error_code(parse_error_code::unexpected_token);
    }

    return std::make_shared<include_decl>(previous().location, path);
}

checked_result<declaration_ptr> parser::import_declaration() {
    // import "path" or import <path> or import(expr)

    // Check for function-style syntax: import(expr)
    if (match(token_type::left_paren)) {
        // Parse the expression inside parentheses
        JAISCRIPT_TRY_ASSIGN(expression_ptr path_expr, expression());
        JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after import expression"));
        return std::make_shared<import_decl>(previous().location, path_expr);
    }

    // Original literal syntax
    std::string path;

    if (match(token_type::string_literal)) {
        // import "path"
        path = previous().string_value;
    } else if (match(token_type::less)) {
        // import <path>
        // Read until we find >
        std::string path_buffer;
        while (!check(token_type::greater) && !is_at_end()) {
            path_buffer += peek().lexeme;
            advance();
        }
        JAISCRIPT_TRY(consume(token_type::greater, "Expected '>' after import path"));
        path = path_buffer;
    } else {
        report_error("Expected string literal, '<', or '(' after import", peek());
    return make_error_code(parse_error_code::unexpected_token);
    }

    return std::make_shared<import_decl>(previous().location, path);
}

type_info_ptr parser::wrap_reference_return_type(type_info_ptr return_type) {
    if (!match(token_type::ampersand)) {
        return return_type;
    }
    type_info refType(script_value_type::jai_reference_type);
    refType.type_name = return_type ? (return_type->type_name + "&") : "auto&";
    refType.type_params.push_back(return_type);
    refType.id = symbolizer_->intern(refType.canonical_name());
    return store_type_info(std::move(refType));
}

checked_result<declaration_ptr> parser::function_declaration() {
    JAISCRIPT_TRY_ASSIGN(type_info_ptr return_type, parse_type());
    return_type = wrap_reference_return_type(return_type);

    JAISCRIPT_TRY_ASSIGN(token name, consume(token_type::identifier, "Expected function name"));
    auto [name_id, name_view] = symbolizer_->intern_with_view(name.lexeme);
    return parse_function_body(name_view, name_id, return_type);
}

checked_result<declaration_ptr> parser::parse_function_body(std::string_view name, uint64_t name_id, type_info_ptr return_type, bool allow_ctor_initializers) {
    auto func = std::make_shared<function_decl>(previous().location, name, name_id);

    JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after function name"));
    JAISCRIPT_TRY_ASSIGN(func->parameters, parse_parameter_list());
    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after parameters"));

    // Handle trailing return type
    if (match(token_type::arrow)) {
        // Check if return type is specified or if we go directly to {
        if (check(token_type::left_brace)) {
            // -> { adds no information: keep the leading type when one was given
            func->return_type = return_type;
        } else {
            JAISCRIPT_TRY_ASSIGN(func->return_type, parse_type());
            func->return_type = wrap_reference_return_type(func->return_type);
            // A leading return type and a contradictory trailing one is an error
            // (Dev ruling 2026-07); a matching pair is redundant-legal.
            if (return_type && func->return_type && func->return_type->id != return_type->id) {
                report_error("Conflicting return types '" + return_type->type_name + "' and '" +
                             func->return_type->type_name + "' (leading and trailing '->' types must match)", previous());
                return make_error_code(parse_error_code::unexpected_token);
            }
        }
    } else {
        // No arrow - if we have a return type from before 'function', use it
        // Otherwise default to auto (nullptr) for inference
        func->return_type = return_type;  // nullptr means auto inference
    }

    // Parse constructor initialization list (: super(args), : this(args))
    if (check(token_type::colon) && !allow_ctor_initializers) {
        // Initializer lists are constructor-only; on free functions, destructors, and
        // function-keyword methods they were silently ignored — a trap (Dev ruling 2026-07)
        report_error("Constructor initializer lists (': super(...)' / ': this(...)') are only allowed on constructors", peek());
        return make_error_code(parse_error_code::unexpected_token);
    }
    if (match(token_type::colon)) {
        do {
            // Parse initializer target (super or this)
            if (match(token_type::super_keyword)) {
                // Parse super(args)
                JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after 'super'"));
                std::vector<expression_ptr> args;
                if (!check(token_type::right_paren)) {
                    do {
                        JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                        args.push_back(std::move(arg));
                    } while (match(token_type::comma));
                }
                JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after super arguments"));
                func->initializers.emplace_back("super", std::move(args));
            } else if (match(token_type::this_keyword)) {
                // Parse this(args)
                JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after 'this'"));
                std::vector<expression_ptr> args;
                if (!check(token_type::right_paren)) {
                    do {
                        JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                        args.push_back(std::move(arg));
                    } while (match(token_type::comma));
                }
                JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after this arguments"));
                func->initializers.emplace_back("this", std::move(args));
            } else {
                report_error("Expected 'super' or 'this' in constructor initializer list", peek());
    return make_error_code(parse_error_code::unexpected_token);
            }
        } while (match(token_type::comma));
    }

    // Enter function scope for slot-based local variable tracking
    enter_function_scope();

    // Assign slots to parameters (they get slots 0, 1, 2, ...)
    for (auto& param : func->parameters) {
        if (param.symbol_id != UINT64_MAX) {
            param.slot_index = allocate_slot(param.symbol_id);
        }
    }

    if (!match(token_type::left_brace)) {
        exit_function_scope();  // Clean up on error
        report_error("Expected '{' before function body", peek());
    return make_error_code(parse_error_code::unexpected_token);
    }
    JAISCRIPT_TRY_ASSIGN(auto body, block_statement());
    func->body = std::dynamic_pointer_cast<block_stmt>(body);

    // Exit function scope and record total slot count
    func->local_count = exit_function_scope();

    return func;
}

checked_result<declaration_ptr> parser::variable_declaration() {
    JAISCRIPT_TRY_ASSIGN(type_info_ptr type, parse_type());

    // Check for reference after type (e.g., int& x or auto& x)
    bool is_reference = false;
    if (match(token_type::ampersand)) {
        is_reference = true;
        // Create a reference type
        type_info refType(script_value_type::jai_reference_type);
        refType.type_name = type ? (type->type_name + "&") : "auto&";
        refType.type_params.push_back(type);
        refType.id = symbolizer_->intern(refType.canonical_name());
        type = store_type_info(std::move(refType));
    }

    JAISCRIPT_TRY_ASSIGN(token name, consume(token_type::identifier, "Expected variable name"));

    expression_ptr initializer = nullptr;
    if (match(token_type::equal)) {
        JAISCRIPT_TRY_ASSIGN(initializer, expression());
    } else if (check(token_type::left_brace)) {
        // Brace initialization: Type name{args...}
        // Create a new_expr for construction
        if (type) {
            advance(); // consume '{'

            std::vector<expression_ptr> arguments;
            if (!check(token_type::right_brace)) {
                arguments.reserve(4);
                do {
                    JAISCRIPT_TRY_ASSIGN(auto arg, expression());
                    arguments.push_back(std::move(arg));
                } while (match(token_type::comma));
            }

            JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after constructor arguments"));

            // Create new_expr with the variable's type
            initializer = std::make_shared<new_expr>(name.location, type, std::move(arguments));
        } else {
            // Auto type with brace initializer - parse as expression
            JAISCRIPT_TRY_ASSIGN(initializer, expression());
        }
    }

    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after variable declaration"));

    auto decl = std::make_shared<variable_decl>(name.location, type, name.lexeme, get_symbol_id(name), initializer);

    // If we're inside a function, assign a slot for this local variable
    if (in_function_scope() && decl->name_id != UINT64_MAX) {
        decl->slot_index = allocate_slot(decl->name_id);
    }

    return decl;
}

void parser::consume_greater_in_generic(const std::string& message) {
    // Handle the case where >> is tokenized as RightShift when we need >
    if (check(token_type::right_shift)) {
        // We have >>, but we only want to consume one >
        token rightShift = advance();
        
        // Push back a synthetic > token for the second >
        // This allows the parser to continue as if there were two separate > tokens
        token syntheticGreater(token_type::greater, ">", rightShift.location);
        pushed_back_token_ = syntheticGreater;
        
        // We've consumed one > (implicitly), the other is pushed back
        return;
    }

    // Normal case: just consume a > token (intentionally ignoring result for error recovery)
    (void)consume(token_type::greater, message);
}

bool parser::is_registered_template_type(const std::string& type_name) const {
    return registered_template_types_.find(type_name) != registered_template_types_.end();
}

checked_result<statement_ptr> parser::switch_statement() {
    token switchToken = previous();

    JAISCRIPT_TRY(consume(token_type::left_paren, "Expected '(' after 'switch'"));
    JAISCRIPT_TRY_ASSIGN(expression_ptr condition, expression());
    JAISCRIPT_TRY(consume(token_type::right_paren, "Expected ')' after switch condition"));

    JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{' after switch condition"));

    auto switchStmt = std::make_shared<switch_stmt>(switchToken.location, condition);

    // Parse cases and default
    while (!check(token_type::right_brace) && !is_at_end()) {
        if (match(token_type::case_keyword)) {
            // Parse case
            JAISCRIPT_TRY_ASSIGN(expression_ptr caseValue, expression());
            JAISCRIPT_TRY(consume(token_type::colon, "Expected ':' after case value"));

            auto caseStmt = std::make_shared<case_stmt>(previous().location, caseValue);

            // Set context for parsing case body
            bool oldInSwitchCase = in_switch_case_;
            in_switch_case_ = true;

            // Parse case body until we hit another case, default, or end of switch
            while (!check(token_type::case_keyword) && !check(token_type::default_keyword) &&
                   !check(token_type::right_brace) && !is_at_end()) {

                // Check for fallthrough as the last statement
                if (check(token_type::fallthrough_keyword)) {
                    auto fallthroughStmt = std::make_shared<fallthrough_stmt>(peek().location);
                    advance(); // consume 'fallthrough'
                    auto consume_result = consume(token_type::semicolon, "Expected ';' after 'fallthrough'");
                    if (!consume_result) {
                        in_switch_case_ = oldInSwitchCase;
                        return consume_result.error();
                    }
                    caseStmt->body.push_back(fallthroughStmt);
                    caseStmt->has_fallthrough = true;
                    break; // fallthrough must be the last statement in a case
                }

                // Check for break statement (it's allowed but redundant)
                if (check(token_type::break_keyword)) {
                    advance(); // consume 'break'
                    auto consume_result = consume(token_type::semicolon, "Expected ';' after 'break'");
                    if (!consume_result) {
                        in_switch_case_ = oldInSwitchCase;
                        return consume_result.error();
                    }
                    // Don't add break to the body since it's implicit
                    break;
                }

                // Parse regular statement
                auto stmt_result = statement();
                if (!stmt_result) {
                    in_switch_case_ = oldInSwitchCase;
                    return stmt_result.error();
                }
                caseStmt->body.push_back(stmt_result.value());
            }

            // Restore context
            in_switch_case_ = oldInSwitchCase;

            switchStmt->cases.push_back(caseStmt);

        } else if (match(token_type::default_keyword)) {
            // Parse default
            if (switchStmt->default_case) {
                report_error("Multiple default labels in switch statement", previous());
    return make_error_code(parse_error_code::unexpected_token);
            }

            JAISCRIPT_TRY(consume(token_type::colon, "Expected ':' after 'default'"));

            auto defaultStmt = std::make_shared<default_stmt>(previous().location);

            // Set context for parsing default body
            bool oldInSwitchCase = in_switch_case_;
            in_switch_case_ = true;

            // Parse default body
            while (!check(token_type::case_keyword) && !check(token_type::default_keyword) &&
                   !check(token_type::right_brace) && !is_at_end()) {

                // Check for break statement (allowed but redundant)
                if (check(token_type::break_keyword)) {
                    advance(); // consume 'break'
                    auto consume_result = consume(token_type::semicolon, "Expected ';' after 'break'");
                    if (!consume_result) {
                        in_switch_case_ = oldInSwitchCase;
                        return consume_result.error();
                    }
                    break;
                }

                auto stmt_result = statement();
                if (!stmt_result) {
                    in_switch_case_ = oldInSwitchCase;
                    return stmt_result.error();
                }
                defaultStmt->body.push_back(stmt_result.value());
            }

            // Restore context
            in_switch_case_ = oldInSwitchCase;

            switchStmt->default_case = defaultStmt;

        } else {
            report_error("Expected 'case' or 'default' in switch statement", peek());
    return make_error_code(parse_error_code::unexpected_token);
        }
    }

    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after switch body"));

    return switchStmt;
}

// Helper to get symbol ID from token - uses pre-interned symbol_id if available
uint64_t parser::get_symbol_id(const token& tok) const {
    // If lexer already interned this token (identifiers), use that
    if (tok.symbol_id != 0) {
        return tok.symbol_id;
    }
    // Otherwise intern now (for keywords used as identifiers, etc.)
    return symbolizer_->intern(tok.lexeme);
}

} // namespace jai