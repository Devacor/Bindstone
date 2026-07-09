#pragma once

#ifndef __JAISCRIPT_DETAIL_TRANSIENT_READ_HPP__
#define __JAISCRIPT_DETAIL_TRANSIENT_READ_HPP__

#include <jaiscript/detail/ast.hpp>

namespace jai::detail {

    // ---- Transient-read pass (element-read overhead, docs/element_read_overhead_design.md) ----
    // Stamps binary_expr::transient_element_read on subscript READS whose result is provably
    // consumed as a transient value, so both backends (vm_compiler emit / interpreter subscript
    // branch - parity by construction, ONE classifier) can push a shallow element copy instead
    // of minting a heap reference_holder. Default is MINT-WHEN-UNKNOWN: a position is only
    // marked when enumerated below; every lvalue-consuming context (store/compound target,
    // ref-param, reference-decl initializer, reference-return, mutating receiver, ++/--,
    // range-for subject, call argument, base of an outer lvalue chain) is simply never marked.
    //
    // CHECKLIST - any NEW syntax that can consume a subscript as an lvalue (bind a reference,
    // write through it, or expose its identity) must keep its operand positions UNMARKED here.
    //
    // Marked positions (the transient set):
    //   - operands of builtin binary value ops (arith/compare/bitwise/shift/spaceship);
    //     the LEFT operand only when the right subtree is callout_free (its copy is held
    //     while the right side evaluates - user code running there could observe use_count)
    //   - operands of && and || (each is truthiness-tested before anything else runs)
    //   - conditions of if / while / for / ternary (consumed immediately by is_truthy)
    //   - the index/key expression of ANY subscript (always dereferenced before use)
    //   - the base of a subscript chain that is itself marked (grid[y][x] as an operand),
    //     again only when the index subtree is callout_free
    //   - the VALUE of an assignment (simple or compound) when the target subtree is
    //     callout_free (both backends deref the RHS before storing - byte-parallel)
    //   - both branches of a ternary propagate the ternary's own consumer context
    //
    // Runtime gate: the mark only takes effect while the engine has no GLOBAL
    // binary-operator or "[]" registration (has_custom_binary_ops, latched at the
    // register_overload_impl / add_global chokepoints) - globally overridden ops receive
    // operand copies whose transient +1 would be observable via use_count()/cpp_ref_count
    // inside the override, so the mint path is kept in that case.
    //
    // Class operator/"[]" METHODS deliberately do NOT latch the gate (an engine-wide
    // latch would kill the elision for any host binding operator types). Consequences,
    // both deliberate: (1) a subscript operand feeding a class operator method is READ
    // (copied) at operand-evaluation time - the interpreter's historical order, which the
    // old VM's late deref silently diverged from; (2) use_count observed inside a class
    // operator method may sit one higher than the pre-elision VM - calibration, not
    // semantics, per the standing transient-copy ruling.
    //
    // Unary value ops are deliberately NOT in the set: -/~ do not deref references today
    // (a subscript operand errors on both backends), and marking would make that shape's
    // behavior depend on the runtime gate.
    struct transient_read_marker {

        static bool is_value_binary_op(token_type t) noexcept {
            switch (t) {
                case token_type::plus:
                case token_type::minus:
                case token_type::star:
                case token_type::slash:
                case token_type::percent:
                case token_type::less:
                case token_type::less_equal:
                case token_type::greater:
                case token_type::greater_equal:
                case token_type::equal_equal:
                case token_type::bang_equal:
                case token_type::spaceship:
                case token_type::ampersand:
                case token_type::pipe:
                case token_type::caret:
                case token_type::left_shift:
                case token_type::right_shift:
                    return true;
                default:
                    return false;
            }
        }

        // No user code can run while evaluating this subtree (builtin subscripts included:
        // an object "[]" override flips the engine's runtime gate, closing that hole).
        static bool callout_free(const expression* e) noexcept {
            if (!e) {
                return true;
            }
            switch (e->get_type()) {
                case node_type::literal_expr:
                case node_type::identifier_expr:
                case node_type::this_expr:
                case node_type::super_expr:
                    return true;
                case node_type::binary_expr: {
                    auto* b = static_cast<const binary_expr*>(e);
                    if (b->op.type == token_type::left_bracket || is_value_binary_op(b->op.type) ||
                        b->op.type == token_type::ampersand_ampersand || b->op.type == token_type::pipe_pipe) {
                        return callout_free(b->left.get()) && callout_free(b->right.get());
                    }
                    return false;
                }
                case node_type::unary_expr: {
                    auto* u = static_cast<const unary_expr*>(e);
                    if (u->op.type == token_type::bang || u->op.type == token_type::minus ||
                        u->op.type == token_type::plus || u->op.type == token_type::tilde) {
                        return callout_free(u->operand.get());
                    }
                    return false;   // ++/-- mutate; anything else is unknown
                }
                case node_type::ternary_expr: {
                    auto* t = static_cast<const ternary_expr*>(e);
                    return callout_free(t->condition.get()) && callout_free(t->then_expression.get()) &&
                           callout_free(t->else_expression.get());
                }
                default:
                    return false;   // member (getters), call, assignment, new, lambda, literals, ...
            }
        }

        // transient_ctx: this expression's RESULT is consumed as a transient value by its parent.
        void walk_expr(expression* e, bool transient_ctx) {
            if (!e) {
                return;
            }
            switch (e->get_type()) {
                case node_type::binary_expr: {
                    auto* b = static_cast<binary_expr*>(e);
                    if (b->op.type == token_type::left_bracket) {
                        if (transient_ctx) {
                            b->transient_element_read = true;
                        }
                        // Chain base: transient only when this read is, and the index can't
                        // run user code while the base's copy is held. Index: always deref'd
                        // to a scalar/key immediately, in read AND store-target position.
                        walk_expr(b->left.get(), transient_ctx && callout_free(b->right.get()));
                        walk_expr(b->right.get(), true);
                        return;
                    }
                    if (b->op.type == token_type::ampersand_ampersand || b->op.type == token_type::pipe_pipe) {
                        walk_expr(b->left.get(), true);    // truthiness-tested before right runs
                        walk_expr(b->right.get(), true);
                        return;
                    }
                    if (is_value_binary_op(b->op.type)) {
                        walk_expr(b->left.get(), callout_free(b->right.get()));
                        walk_expr(b->right.get(), true);   // consumed by the op immediately
                        return;
                    }
                    walk_expr(b->left.get(), false);
                    walk_expr(b->right.get(), false);
                    return;
                }
                case node_type::unary_expr:
                    walk_expr(static_cast<unary_expr*>(e)->operand.get(), false);
                    return;
                case node_type::assignment_expr: {
                    auto* a = static_cast<assignment_expr*>(e);
                    walk_expr(a->target.get(), false);     // store/compound target: always mint
                    walk_expr(a->value.get(), callout_free(a->target.get()));
                    return;
                }
                case node_type::call_expr: {
                    auto* c = static_cast<call_expr*>(e);
                    walk_expr(c->callee.get(), false);
                    for (const auto& arg : c->arguments) {
                        walk_expr(arg.get(), false);       // ref-params: always mint
                    }
                    return;
                }
                case node_type::member_expr:
                    walk_expr(static_cast<member_expr*>(e)->object.get(), false);
                    return;
                case node_type::lambda_expr: {
                    auto* l = static_cast<lambda_expr*>(e);
                    for (auto& p : l->parameters) {
                        walk_expr(p.default_value.get(), false);
                    }
                    walk_stmt(l->body.get());
                    return;
                }
                case node_type::new_expr:
                    for (const auto& arg : static_cast<new_expr*>(e)->arguments) {
                        walk_expr(arg.get(), false);
                    }
                    return;
                case node_type::include_expr:
                    walk_expr(static_cast<include_expr*>(e)->path_expr.get(), false);
                    return;
                case node_type::ternary_expr: {
                    auto* t = static_cast<ternary_expr*>(e);
                    walk_expr(t->condition.get(), true);   // consumed before either branch runs
                    walk_expr(t->then_expression.get(), transient_ctx);
                    walk_expr(t->else_expression.get(), transient_ctx);
                    return;
                }
                case node_type::array_literal_expr:
                    for (const auto& el : static_cast<array_literal_expr*>(e)->elements) {
                        walk_expr(el.get(), false);
                    }
                    return;
                case node_type::map_literal_expr:
                    for (const auto& [k, v] : static_cast<map_literal_expr*>(e)->entries) {
                        walk_expr(k.get(), false);
                        walk_expr(v.get(), false);
                    }
                    return;
                case node_type::throw_expr:
                    walk_expr(static_cast<throw_expr*>(e)->value.get(), false);
                    return;
                case node_type::yield_expr:
                    walk_expr(static_cast<yield_expr*>(e)->value.get(), false);
                    return;
                default:
                    return;   // literal/identifier/this/super: leaves
            }
        }

        void walk_stmt(statement* s) {
            if (!s) {
                return;
            }
            switch (s->get_type()) {
                case node_type::expression_stmt:
                    walk_expr(static_cast<expression_stmt*>(s)->expression.get(), false);
                    return;
                case node_type::block_stmt:
                    for (const auto& d : static_cast<block_stmt*>(s)->declarations) {
                        walk_stmt(d.get());
                    }
                    return;
                case node_type::if_stmt: {
                    auto* i = static_cast<if_stmt*>(s);
                    walk_expr(i->condition.get(), true);
                    walk_stmt(i->then_statement.get());
                    walk_stmt(i->else_statement.get());
                    return;
                }
                case node_type::while_stmt: {
                    auto* w = static_cast<while_stmt*>(s);
                    walk_expr(w->condition.get(), true);
                    walk_stmt(w->body.get());
                    return;
                }
                case node_type::for_stmt: {
                    auto* f = static_cast<for_stmt*>(s);
                    walk_stmt(f->initializer.get());
                    walk_expr(f->condition.get(), true);
                    walk_expr(f->update.get(), false);
                    walk_stmt(f->body.get());
                    return;
                }
                case node_type::range_for_stmt: {
                    auto* r = static_cast<range_for_stmt*>(s);
                    walk_expr(r->container.get(), false);  // subject may be iterated by reference
                    walk_stmt(r->body.get());
                    return;
                }
                case node_type::return_stmt:
                    walk_expr(static_cast<return_stmt*>(s)->value.get(), false);   // ref-returns: mint
                    return;
                case node_type::try_stmt: {
                    auto* t = static_cast<try_stmt*>(s);
                    walk_stmt(t->try_block.get());
                    walk_stmt(t->catch_block.get());
                    return;
                }
                case node_type::switch_stmt: {
                    auto* sw = static_cast<switch_stmt*>(s);
                    walk_expr(sw->condition.get(), false); // subject is held across case expressions
                    for (const auto& c : sw->cases) {
                        walk_stmt(c.get());
                    }
                    walk_stmt(sw->default_case.get());
                    return;
                }
                case node_type::case_stmt: {
                    auto* c = static_cast<case_stmt*>(s);
                    walk_expr(c->value.get(), false);
                    for (const auto& b : c->body) {
                        walk_stmt(b.get());
                    }
                    return;
                }
                case node_type::default_stmt:
                    for (const auto& b : static_cast<default_stmt*>(s)->body) {
                        walk_stmt(b.get());
                    }
                    return;
                case node_type::statement_decl:
                    walk_stmt(static_cast<statement_decl*>(s)->statement.get());
                    return;
                case node_type::variable_decl:
                    // Initializers stay unmarked: ref decls bind them, typed decls key off
                    // the reference-init shape (interpreter/vm decl twins)
                    walk_expr(static_cast<variable_decl*>(s)->initializer.get(), false);
                    return;
                case node_type::function_decl: {
                    auto* fd = static_cast<function_decl*>(s);
                    for (auto& p : fd->parameters) {
                        walk_expr(p.default_value.get(), false);
                    }
                    for (const auto& ci : fd->initializers) {
                        for (const auto& arg : ci.arguments) {
                            walk_expr(arg.get(), false);
                        }
                    }
                    walk_stmt(fd->body.get());
                    return;
                }
                case node_type::class_decl:
                    for (const auto& m : static_cast<class_decl*>(s)->members) {
                        walk_stmt(m.declaration.get());
                    }
                    return;
                case node_type::namespace_decl:
                    for (const auto& d : static_cast<namespace_decl*>(s)->declarations) {
                        walk_stmt(d.get());
                    }
                    return;
                case node_type::expression_decl:
                    walk_expr(static_cast<expression_decl*>(s)->expression.get(), false);
                    return;
                case node_type::destructuring_decl:
                    walk_expr(static_cast<destructuring_decl*>(s)->initializer.get(), false);
                    return;
                case node_type::include_decl:
                    walk_expr(static_cast<include_decl*>(s)->path_expr.get(), false);
                    return;
                case node_type::import_decl:
                    walk_expr(static_cast<import_decl*>(s)->path_expr.get(), false);
                    return;
                default:
                    return;   // break/continue/fallthrough/enum: leaves
            }
        }
    };

    inline void mark_transient_reads(const std::vector<declaration_ptr>& program) {
        transient_read_marker marker;
        for (const auto& d : program) {
            marker.walk_stmt(d.get());
        }
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_TRANSIENT_READ_HPP__
