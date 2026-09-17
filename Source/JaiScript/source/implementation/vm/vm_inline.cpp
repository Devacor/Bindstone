// Inlined leaves (docs/typed_regions_design.md, "Inlined leaves"): a plain identifier call the checker resolved to one small free
// script function compiles at its call site as the callee's body: a guard on the callee's identity, the by-value parameters as
// declarations in slots above the caller's frame, a reference parameter renamed to the caller's variable, the body with its
// returns converted to a store into the result slot, the caller's statement with the call replaced by the result, and behind a
// jump the original statement as the guard's fallback. The caller's AST is never changed: the interpreter walks it, and the
// fallback compiles it.
#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>

#include <algorithm>
#include <functional>

namespace jai::vm {

namespace {

constexpr size_t k_inline_max_statements = 12;
constexpr size_t k_inline_max_nodes = 256;   // the converted body: a rest folded into two arms is cloned twice

struct inline_context {
	size_t slot_offset = 0;                                        // the callee's slots move above the caller's frame
	std::vector<std::shared_ptr<identifier_expr>> renames;         // per callee slot: the caller's identifier a reference parameter takes, or null
	std::shared_ptr<identifier_expr> result;                       // the result slot (null: a void callee)
	type_info_ptr result_type;
	size_t nodes = 0;
	bool ok = true;
	bool writes_global = false;
	bool writes_member = false;
	std::vector<std::shared_ptr<ast_node>> pins;
	std::vector<uint64_t> declared_names;                          // the callee's locals: declared in the caller for the graft's compile (a slot identifier compiles to its slot only when its name is declared)
	const call_expr* replace = nullptr;                            // the caller's statement clone: this call becomes the result
	template <class T, class... A> std::shared_ptr<T> make(A&&... a) {
		auto n = std::make_shared<T>(std::forward<A>(a)...);
		pins.push_back(n);
		if (++nodes > k_inline_max_nodes) { ok = false; }
		return n;
	}
	void fail() { ok = false; }
};

// the statements that follow a folded if, pending for every path that falls through: the original declarations from an index, then
// the enclosing pending rest (a nested fold)
struct pending_rest {
	const std::vector<declaration_ptr>* decls = nullptr;
	size_t from = 0;
	const pending_rest* outer = nullptr;
};

bool scalar_type(const type_info* t) {
	if (!t) return false;
	switch (t->base_type) {
	case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type: return true;
	default: return false;
	}
}
bool string_type(const type_info* t) { return t && t->base_type == script_value_type::jai_string_type; }
bool any_type(const type_info* t) { return !t || t->base_type == script_value_type::jai_any_type; }
bool void_type(const type_info* t) { return t && t->base_type == script_value_type::jai_null_type; }   // `void` is the null type
bool var_reference(const type_info* t) { return t && t->is_reference() && any_type(t->element_type().get()); }
bool math_call(const call_expr* ce) {
	return ce->callee && ce->callee->get_type() == node_type::member_expr && detail::math_intrinsic_for_call(static_cast<const member_expr*>(ce->callee.get())) != detail::math_fn::none;
}

expression_ptr clone_expr(const expression_ptr& e, inline_context& c);

std::shared_ptr<identifier_expr> clone_identifier(const identifier_expr* id, inline_context& c) {
	if (id->slot_index != SIZE_MAX && id->slot_index < c.renames.size() && c.renames[id->slot_index]) {
		const identifier_expr* r = c.renames[id->slot_index].get();
		auto n = c.make<identifier_expr>(id->location, r->name, r->symbol_id);
		n->slot_index = r->slot_index; n->global_qualified = r->global_qualified; n->names_value_decl = r->names_value_decl; n->names_known_value = r->names_known_value;
		return n;
	}
	auto n = c.make<identifier_expr>(id->location, id->name, id->symbol_id);
	n->slot_index = id->slot_index == SIZE_MAX ? SIZE_MAX : id->slot_index + c.slot_offset;
	n->global_qualified = id->global_qualified; n->names_value_decl = id->names_value_decl; n->names_known_value = id->names_known_value;
	return n;
}

// the expression forms an inlined body and an inlined statement may hold: no call but a math intrinsic, no lambda, no `this`, no
// literal container, no yield, no throw, no include
expression_ptr clone_expr(const expression_ptr& e, inline_context& c) {
	if (!c.ok || !e) return nullptr;
	switch (e->get_type()) {
	case node_type::literal_expr: ++c.nodes; return e;   // immutable: shared as it is
	case node_type::identifier_expr: return clone_identifier(static_cast<const identifier_expr*>(e.get()), c);
	case node_type::binary_expr: {
		auto* b = static_cast<const binary_expr*>(e.get());
		auto l = clone_expr(b->left, c); auto r = clone_expr(b->right, c);
		if (!c.ok) return nullptr;
		auto n = c.make<binary_expr>(b->location, l, b->op, r);
		n->transient_element_read = b->transient_element_read;
		return n;
	}
	case node_type::unary_expr: {
		auto* u = static_cast<const unary_expr*>(e.get());
		auto o = clone_expr(u->operand, c);
		if (!c.ok) return nullptr;
		return c.make<unary_expr>(u->location, u->op, o, u->is_postfix);
	}
	case node_type::assignment_expr: {
		auto* a = static_cast<const assignment_expr*>(e.get());
		if (a->target->get_type() == node_type::identifier_expr) {
			auto* t = static_cast<const identifier_expr*>(a->target.get());
			const bool renamed = t->slot_index != SIZE_MAX && t->slot_index < c.renames.size() && c.renames[t->slot_index];
			if (t->slot_index == SIZE_MAX || renamed) { c.writes_global = true; }   // a global, or the caller's variable through a reference parameter
		} else if (a->target->get_type() == node_type::member_expr || a->target->get_type() == node_type::binary_expr) { c.writes_member = true; }   // a field or an element
		else { c.fail(); return nullptr; }
		auto t = clone_expr(a->target, c); auto v = clone_expr(a->value, c);
		if (!c.ok) return nullptr;
		auto n = c.make<assignment_expr>(a->location, t, a->op, v);
		n->typed_store_provable = a->typed_store_provable; n->static_store_proof = a->static_store_proof; n->static_operand_class = a->static_operand_class;
		n->postfix_old_result = a->postfix_old_result; n->incdec_desugar = a->incdec_desugar;
		return n;
	}
	case node_type::member_expr: {
		auto* m = static_cast<const member_expr*>(e.get());
		auto o = clone_expr(m->object, c);
		if (!c.ok) return nullptr;
		auto n = c.make<member_expr>(m->location, o, m->member, m->member_id, m->is_arrow, m->is_static);
		n->null_safe = m->null_safe;
		return n;
	}
	case node_type::ternary_expr: {
		auto* t = static_cast<const ternary_expr*>(e.get());
		auto cond = clone_expr(t->condition, c); auto a = clone_expr(t->then_expression, c); auto b = clone_expr(t->else_expression, c);
		if (!c.ok) return nullptr;
		return c.make<ternary_expr>(t->location, cond, a, b);
	}
	case node_type::cast_expr: {
		auto* k = static_cast<const cast_expr*>(e.get());
		auto o = clone_expr(k->operand, c);
		if (!c.ok) return nullptr;
		return c.make<cast_expr>(k->location, k->target, k->by_reference, o);
	}
	case node_type::call_expr: {
		auto* ce = static_cast<const call_expr*>(e.get());
		if (c.replace && ce == c.replace) { ++c.nodes; return c.result; }   // the caller's statement: the call is the result (null: dropped)
		if (!math_call(ce)) { c.fail(); return nullptr; }   // any other call (a script or host function, a method) is an effect the graft does not take
		std::vector<expression_ptr> args;
		for (const auto& a : ce->arguments) { args.push_back(clone_expr(a, c)); }
		auto callee = clone_expr(ce->callee, c);
		if (!c.ok) return nullptr;
		auto n = c.make<call_expr>(ce->location, callee, std::move(args));
		n->static_arg_proofs = ce->static_arg_proofs; n->proven_callee_body = ce->proven_callee_body; n->proven_callee_decl = ce->proven_callee_decl;
		return n;
	}
	default:
		c.fail(); return nullptr;
	}
}

bool may_return(const statement* s) {
	if (!s) return false;
	switch (s->get_type()) {
	case node_type::return_stmt: return true;
	case node_type::block_stmt:
		for (const auto& d : static_cast<const block_stmt*>(s)->declarations) {
			if (d->get_type() == node_type::statement_decl && may_return(static_cast<const statement_decl*>(d.get())->statement.get())) return true;
		}
		return false;
	case node_type::if_stmt: { auto* i = static_cast<const if_stmt*>(s); return may_return(i->then_statement.get()) || may_return(i->else_statement.get()); }
	default: return false;
	}
}
// control never falls out of the statement (every path ends in a return)
bool terminates(const statement* s) {
	if (!s) return false;
	switch (s->get_type()) {
	case node_type::return_stmt: return true;
	case node_type::block_stmt:
		for (const auto& d : static_cast<const block_stmt*>(s)->declarations) { if (d->get_type() == node_type::statement_decl && terminates(static_cast<const statement_decl*>(d.get())->statement.get())) return true; }
		return false;
	case node_type::if_stmt: { auto* i = static_cast<const if_stmt*>(s); return i->else_statement && terminates(i->then_statement.get()) && terminates(i->else_statement.get()); }
	default: return false;
	}
}

std::vector<declaration_ptr> clone_body(const std::vector<declaration_ptr>& decls, size_t from, inline_context& c, const pending_rest* rest);

// an arm of a folded if: a block clones with the pending rest at its fall-through, a return becomes the result store, a plain
// statement is followed by the rest, a nested if folds the rest into its own arms
statement_ptr clone_arm(const statement_ptr& s, inline_context& c, const pending_rest* rest) {
	if (!c.ok || !s) return nullptr;
	switch (s->get_type()) {
	case node_type::block_stmt: {
		auto* b = static_cast<const block_stmt*>(s.get());
		std::vector<declaration_ptr> ds = clone_body(b->declarations, 0, c, rest);
		if (!c.ok) return nullptr;
		return c.make<block_stmt>(b->location, std::move(ds));
	}
	case node_type::if_stmt: {
		auto* i = static_cast<const if_stmt*>(s.get());
		auto cond = clone_expr(i->condition, c);
		if (!c.ok) return nullptr;
		statement_ptr t = clone_arm(i->then_statement, c, rest);
		statement_ptr el = i->else_statement ? clone_arm(i->else_statement, c, rest) : nullptr;
		if (!c.ok) return nullptr;
		if (!i->else_statement && rest) {   // the else path: the rest alone
			std::vector<declaration_ptr> ds = clone_body(*rest->decls, rest->from, c, rest->outer);
			if (!c.ok) return nullptr;
			if (!ds.empty()) { el = c.make<block_stmt>(i->location, std::move(ds)); }
		}
		return c.make<if_stmt>(i->location, cond, t, el);
	}
	case node_type::return_stmt: {
		auto* r = static_cast<const return_stmt*>(s.get());
		std::vector<declaration_ptr> ds;
		if (!c.result) {
			if (r->value) { c.fail(); return nullptr; }
		} else {
			if (!r->value || r->binds_reference) { c.fail(); return nullptr; }
			if (r->static_return_proof != proof_same_type && c.result_type && !any_type(c.result_type.get())) { c.fail(); return nullptr; }   // a return the checker did not prove converts at the callee's door with the call's text: not a graft
			expression_ptr v = clone_expr(r->value, c);
			if (!c.ok) return nullptr;
			auto rd = c.make<variable_decl>(r->location, c.result_type, c.result->name, c.result->symbol_id, v);   // `T __r = e;`: the path's one definition of the result
			rd->slot_index = c.result->slot_index;
			ds.push_back(rd);
		}
		return c.make<block_stmt>(r->location, std::move(ds));   // the path ends here: no rest
	}
	case node_type::expression_stmt: {
		auto* es = static_cast<const expression_stmt*>(s.get());
		auto e = clone_expr(es->expression, c);
		if (!c.ok) return nullptr;
		std::vector<declaration_ptr> ds; ds.push_back(c.make<expression_decl>(es->location, e, false));
		if (rest) { std::vector<declaration_ptr> tail = clone_body(*rest->decls, rest->from, c, rest->outer); for (auto& d : tail) ds.push_back(d); }
		if (!c.ok) return nullptr;
		return c.make<block_stmt>(es->location, std::move(ds));
	}
	default:
		c.fail(); return nullptr;
	}
}

// a body's declarations from an index: the statements clone in order; a return ends the list as the result store; an if whose arms
// may return takes what follows it into its arms (each path falls through into the rest once); the pending rest is appended where
// the list itself falls through
std::vector<declaration_ptr> clone_body(const std::vector<declaration_ptr>& decls, size_t from, inline_context& c, const pending_rest* rest) {
	std::vector<declaration_ptr> out;
	for (size_t k = from; k < decls.size() && c.ok; ++k) {
		const declaration_ptr& d = decls[k];
		switch (d->get_type()) {
		case node_type::variable_decl: {
			auto* v = static_cast<const variable_decl*>(d.get());
			if (v->slot_index == SIZE_MAX || v->is_static || v->ref_escaping || v->function_spelling) { c.fail(); return out; }
			expression_ptr init = v->initializer ? clone_expr(v->initializer, c) : nullptr;
			if (!c.ok) return out;
			auto n = c.make<variable_decl>(v->location, v->type, v->name, v->name_id, init);
			n->slot_index = v->slot_index + c.slot_offset;
			c.declared_names.push_back(v->name_id);
			out.push_back(n);
			break;
		}
		case node_type::expression_decl: {
			auto* ed = static_cast<const expression_decl*>(d.get());
			if (ed->implicit_return) { c.fail(); return out; }
			auto e = clone_expr(ed->expression, c);
			if (!c.ok) return out;
			out.push_back(c.make<expression_decl>(ed->location, e, false));
			break;
		}
		case node_type::statement_decl: {
			const statement_ptr& s = static_cast<const statement_decl*>(d.get())->statement;
			if (!s) break;
			if (s->get_type() == node_type::return_stmt) {
				statement_ptr r = clone_arm(s, c, nullptr);
				if (!c.ok) return out;
				out.push_back(c.make<statement_decl>(s->location, r));
				return out;   // nothing after a return runs, and the pending rest is not reached on this path
			}
			if (s->get_type() == node_type::if_stmt && may_return(s.get())) {
				pending_rest here{ &decls, k + 1, rest };
				statement_ptr folded = clone_arm(s, c, &here);
				if (!c.ok) return out;
				out.push_back(c.make<statement_decl>(s->location, folded));
				return out;   // the rest went into the arms
			}
			if (s->get_type() == node_type::block_stmt && may_return(s.get())) { c.fail(); return out; }   // a return inside a bare nested block: not a shape the rule folds
			if (s->get_type() == node_type::if_stmt) {
				auto* i = static_cast<const if_stmt*>(s.get());
				auto cond = clone_expr(i->condition, c);
				statement_ptr t = clone_arm(i->then_statement, c, nullptr);
				statement_ptr el = i->else_statement ? clone_arm(i->else_statement, c, nullptr) : nullptr;
				if (!c.ok) return out;
				out.push_back(c.make<statement_decl>(s->location, c.make<if_stmt>(i->location, cond, t, el)));
				break;
			}
			if (s->get_type() == node_type::expression_stmt || s->get_type() == node_type::block_stmt) {
				statement_ptr n = clone_arm(s, c, nullptr);
				if (!c.ok) return out;
				out.push_back(c.make<statement_decl>(s->location, n));
				break;
			}
			c.fail(); return out;
		}
		default:
			c.fail(); return out;
		}
	}
	if (rest && c.ok) {   // the list fell through: the pending rest follows
		std::vector<declaration_ptr> tail = clone_body(*rest->decls, rest->from, c, rest->outer);
		for (auto& d : tail) out.push_back(d);
	}
	return out;
}

// the leaves an expression reads, in evaluation order, up to the call (left to right, a store's target after its value, as both
// backends evaluate): locals and literals hoist past anything; a global or a member only past a body that writes neither
struct hoist_scan {
	const call_expr* call = nullptr;
	bool before_call = true;
	bool other_call = false;
	bool global_before = false;
	bool member_before = false;
	bool unsupported = false;
	void visit(const expression* e) {
		if (!e || unsupported) return;
		switch (e->get_type()) {
		case node_type::literal_expr: return;
		case node_type::identifier_expr: if (before_call && static_cast<const identifier_expr*>(e)->slot_index == SIZE_MAX) { global_before = true; } return;
		case node_type::binary_expr: { auto* b = static_cast<const binary_expr*>(e); visit(b->left.get()); visit(b->right.get()); return; }
		case node_type::unary_expr: visit(static_cast<const unary_expr*>(e)->operand.get()); return;
		case node_type::member_expr: { auto* m = static_cast<const member_expr*>(e); if (m->is_static) return; if (before_call) { member_before = true; } visit(m->object.get()); return; }
		case node_type::ternary_expr: { auto* t = static_cast<const ternary_expr*>(e); visit(t->condition.get()); visit(t->then_expression.get()); visit(t->else_expression.get()); return; }
		case node_type::cast_expr: visit(static_cast<const cast_expr*>(e)->operand.get()); return;
		case node_type::assignment_expr: {
			auto* a = static_cast<const assignment_expr*>(e);
			visit(a->value.get());
			if (a->target->get_type() == node_type::member_expr) { visit(static_cast<const member_expr*>(a->target.get())->object.get()); }
			else if (a->target->get_type() != node_type::identifier_expr) { unsupported = true; }
			return;
		}
		case node_type::call_expr: {
			auto* ce = static_cast<const call_expr*>(e);
			if (ce == call) { for (const auto& a : ce->arguments) { visit(a.get()); } before_call = false; return; }
			if (math_call(ce)) { for (const auto& a : ce->arguments) { visit(a.get()); } return; }
			other_call = true; return;
		}
		default: unsupported = true; return;
		}
	}
};

// the one plain identifier call in an expression whose callee the checker resolved (null when there is none or more than one)
std::shared_ptr<call_expr> single_resolved_call(const expression_ptr& e, size_t& count) {
	std::shared_ptr<call_expr> found;
	std::function<void(const expression_ptr&)> walk = [&](const expression_ptr& x) {
		if (!x) return;
		switch (x->get_type()) {
		case node_type::binary_expr: { auto* b = static_cast<binary_expr*>(x.get()); walk(b->left); walk(b->right); return; }
		case node_type::unary_expr: walk(static_cast<unary_expr*>(x.get())->operand); return;
		case node_type::member_expr: walk(static_cast<member_expr*>(x.get())->object); return;
		case node_type::ternary_expr: { auto* t = static_cast<ternary_expr*>(x.get()); walk(t->condition); walk(t->then_expression); walk(t->else_expression); return; }
		case node_type::cast_expr: walk(static_cast<cast_expr*>(x.get())->operand); return;
		case node_type::assignment_expr: { auto* a = static_cast<assignment_expr*>(x.get()); walk(a->target); walk(a->value); return; }
		case node_type::call_expr: {
			auto ce = std::static_pointer_cast<call_expr>(x);
			if (ce->callee && ce->callee->get_type() == node_type::identifier_expr && ce->proven_callee_decl) { ++count; found = ce; }
			for (const auto& a : ce->arguments) { walk(a); }
			return;
		}
		default: return;
		}
	};
	walk(e);
	return count == 1 ? found : nullptr;
}

} // namespace

// the callee's shape and the site's arguments (the statement's position is the caller's business): one small body of the forms the
// clone takes, every by-value parameter a scalar or a string the checker proved or a `var` taking a scalar local or a literal, every
// reference parameter a `var&` bound to a plain identifier or an element lvalue
bool vm_compiler::inline_eligible(const call_expr* call, const function_decl* fn) const {
	if (!ir_passes_ || !typed_lowering_ || !callable_.active || !fn || !fn->body || fn->is_coroutine || fn->is_static) return false;
	if (fn->body.get() == callable_body_) return false;   // no recursion
	if (fn->body->declarations.size() > k_inline_max_statements) return false;
	if (call->arguments.size() != fn->parameters.size()) return false;
	const type_info* rt = fn->return_type.get();
	if (!(void_type(rt) || scalar_type(rt))) return false;   // a string result declares through the general DECL_VAR, which opens the enclosing block
	if (!void_type(rt) && !terminates(fn->body.get())) return false;   // the result slot is defined on every path or the callee is not a leaf the graft takes
	for (size_t i = 0; i < fn->parameters.size(); ++i) {
		const parameter& p = fn->parameters[i];
		const expression* a = call->arguments[i].get();
		if (p.slot_index == SIZE_MAX || p.slot_index >= fn->local_count || p.default_value) return false;
		if (p.is_reference) {
			if (!(!p.type || any_type(p.type.get()) || var_reference(p.type.get())) || p.ref_escaping) return false;   // `var& p` (the parser keeps the referent's type and the reference flag apart); a typed reference converts at its bind: not a graft
			if (a->get_type() == node_type::identifier_expr) { if (static_cast<const identifier_expr*>(a)->global_qualified) return false; continue; }
			if (a->get_type() == node_type::binary_expr && static_cast<const binary_expr*>(a)->op.type == token_type::left_bracket) continue;
			return false;
		}
		if (p.ref_escaping) return false;
		const bool proven = i < call->static_arg_proofs.size() && call->static_arg_proofs[i] == proof_same_type;
		if (scalar_type(p.type.get())) { if (!proven) return false; continue; }   // a string parameter would declare through the general DECL_VAR: not a graft
		if (any_type(p.type.get())) {
			if (a->get_type() == node_type::literal_expr) continue;
			if (a->get_type() == node_type::identifier_expr) {
				auto* id = static_cast<const identifier_expr*>(a);
				if (id->slot_index != SIZE_MAX && callable_.primitive_slots.count(static_cast<uint32_t>(id->slot_index))) continue;
			}
			return false;
		}
		return false;
	}
	return true;
}

// the statement holds one resolved call in a position the graft may take: the call's arguments before its body, the leaves the
// statement evaluates before the call unchanged by the body
std::shared_ptr<call_expr> vm_compiler::inline_call_of(const expression_ptr& e, const function_decl** fn_out) const {
	if (!e) return nullptr;
	size_t count = 0;
	std::shared_ptr<call_expr> call = single_resolved_call(e, count);
	if (!call) return nullptr;
	const function_decl* fn = call->proven_callee_decl;
	if (!inline_eligible(call.get(), fn)) return nullptr;
	hoist_scan hs; hs.call = call.get(); hs.visit(e.get());
	if (hs.unsupported || hs.other_call) return nullptr;
	// the body's writes decide what may stand before the call in the statement; the probe also proves the body clones
	inline_context probe; probe.renames.assign(fn->local_count, nullptr); probe.result_type = fn->return_type;
	if (!void_type(fn->return_type.get())) { auto r = std::make_shared<identifier_expr>(fn->body->location, std::string_view("\x01inl"), uint64_t(1)); probe.result = r; probe.pins.push_back(r); }
	(void)clone_body(fn->body->declarations, 0, probe, nullptr);
	if (!probe.ok) return nullptr;
	if ((hs.global_before || hs.member_before) && (probe.writes_global || probe.writes_member)) return nullptr;
	*fn_out = fn;
	return call;
}

// the graft: the guard, the result and the parameters, the body, the statement with the result, the jump, the fallback, the join
void vm_compiler::compile_inlined(const std::shared_ptr<call_expr>& call, const function_decl* fn, const expression_ptr& stmt_expr,
                                  const std::function<void(const expression_ptr&)>& emit_replaced, const std::function<void()>& emit_original) {
	chunk::inline_site site;
	site.callee = make_fused_operand(call->callee);
	site.name_symbol = add_symbol(static_cast<identifier_expr*>(call->callee.get())->symbol_id);
	site.body = fn->body.get();
	site.decl = fn;
	site.call_stmt = current_stmt_;
	const uint32_t site_index = static_cast<uint32_t>(chunk_->inline_sites.size());
	chunk_->inline_sites.push_back(site);
	const size_t guard_ip = emit(opcode::op_inline_guard, site_index, 0, 0);
	chunk_->inline_sites[site_index].guard_ip = static_cast<uint32_t>(guard_ip);
	chunk_->inline_sites[site_index].begin_ip = static_cast<uint32_t>(guard_ip + 1);

	const size_t base = temp_next_;
	temp_next_ += fn->local_count + 1;   // the callee's slots and the result
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	const bool was_off = inlining_off_;
	inlining_off_ = true;   // the graft's statements and the fallback compile plainly
	if (inline_symbol_ == 0) { inline_symbol_ = symbolizer_->intern("\x01inl"); }

	inline_context ctx;
	ctx.slot_offset = base;
	ctx.renames.assign(fn->local_count, nullptr);
	ctx.result_type = fn->return_type;
	std::shared_ptr<variable_decl> result_decl;
	if (!void_type(fn->return_type.get())) {
		auto r = std::make_shared<identifier_expr>(call->location, std::string_view("\x01inl"), inline_symbol_);
		r->slot_index = base + fn->local_count; r->names_value_decl = true;
		ctx.result = r; ctx.pins.push_back(r);
		result_decl = std::make_shared<variable_decl>(call->location, fn->return_type, std::string_view("\x01inl"), inline_symbol_, nullptr);
		result_decl->slot_index = r->slot_index;
		ctx.pins.push_back(result_decl);
	}
	// the parameters: a by-value parameter is a declaration of the callee's type from the argument (the assignment table
	// bind_parameters runs; the checker proved the type, so nothing converts); a reference parameter bound to an identifier is a
	// renaming, bound to an element lvalue an alias declaration (a rebind)
	std::vector<std::shared_ptr<variable_decl>> params;
	for (size_t i = 0; i < fn->parameters.size(); ++i) {
		const parameter& p = fn->parameters[i];
		const expression_ptr& a = call->arguments[i];
		if (p.is_reference && a->get_type() == node_type::identifier_expr) {
			auto* id = static_cast<const identifier_expr*>(a.get());
			auto r = std::make_shared<identifier_expr>(id->location, id->name, id->symbol_id);
			r->slot_index = id->slot_index; r->global_qualified = id->global_qualified; r->names_value_decl = id->names_value_decl; r->names_known_value = id->names_known_value;
			ctx.renames[p.slot_index] = r; ctx.pins.push_back(r);
			continue;
		}
		auto d = std::make_shared<variable_decl>(call->location, p.type, p.name, p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name), a);
		d->slot_index = base + p.slot_index;
		ctx.pins.push_back(d);
		params.push_back(d);
	}
	std::vector<declaration_ptr> body = clone_body(fn->body->declarations, 0, ctx, nullptr);
	// the callee's names count as declared while the graft compiles (a slot identifier compiles to its slot only under a declared
	// name); the names the caller did not declare leave before the fallback, which compiles the original statement as the caller's
	std::vector<uint64_t> added;
	auto declare = [&](uint64_t id) { if (id != UINT64_MAX && callable_.declared.insert(id).second) { added.push_back(id); } };
	declare(inline_symbol_);
	for (const parameter& p : fn->parameters) { declare(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name)); }
	for (const uint64_t id : ctx.declared_names) { declare(id); }
	if (result_decl) { compile_variable_decl(result_decl); }   // `T __r;`: the declared default
	for (const auto& d : params) { compile_variable_decl(d); }
	for (const auto& d : body) { compile_declaration(d, false); }
	// the caller's statement with the call replaced by the result
	inline_context sc; sc.replace = call.get(); sc.result = ctx.result; sc.result_type = fn->return_type;
	expression_ptr replaced = clone_expr(stmt_expr, sc);
	emit_replaced(replaced);
	for (const uint64_t id : added) { callable_.declared.erase(id); }
	for (auto& n : sc.pins) { chunk_->nodes.push_back(n); }
	for (auto& n : ctx.pins) { chunk_->nodes.push_back(n); }
	const size_t jump_ip = emit(opcode::op_jump, 0, 0, 0);
	chunk_->inline_sites[site_index].fallback_ip = static_cast<uint32_t>(chunk_->code.size());
	chunk_->code[guard_ip].b = static_cast<uint32_t>(chunk_->code.size());
	emit_original();
	chunk_->inline_sites[site_index].join_ip = static_cast<uint32_t>(chunk_->code.size());
	patch_jump(jump_ip, chunk_->code.size());
	temp_next_ = base;
	inlining_off_ = was_off;
}

// an expression statement: `f(...);`, `v = f(...);`, `s = s + f(...);`
bool vm_compiler::try_inline_expression(const expression_ptr& e) {
	if (inlining_off_ || !e) return false;
	const function_decl* fn = nullptr;
	std::shared_ptr<call_expr> call = inline_call_of(e, &fn);
	if (!call) return false;
	compile_inlined(call, fn, e,
		[&](const expression_ptr& replaced) {
			if (!replaced || replaced->get_type() == node_type::identifier_expr) return;   // the call alone: the result is dropped
			if (compile_no_result_expression(replaced)) return;
			compile_expression(replaced, true);
			emit(opcode::op_pop);
		},
		[&]() {
			if (compile_no_result_expression(e)) return;
			compile_expression(e, true);
			emit(opcode::op_pop);
		});
	return true;
}

// a declaration whose initializer holds the call: `T v = f(...);`
bool vm_compiler::try_inline_variable_decl(const std::shared_ptr<variable_decl>& decl) {
	if (inlining_off_ || !decl->initializer || decl->slot_index == SIZE_MAX) return false;
	if (decl->type && decl->type->is_reference()) return false;
	const function_decl* fn = nullptr;
	std::shared_ptr<call_expr> call = inline_call_of(decl->initializer, &fn);
	if (!call) return false;
	compile_inlined(call, fn, decl->initializer,
		[&](const expression_ptr& replaced) {
			auto nd = std::make_shared<variable_decl>(decl->location, decl->type, decl->name, decl->name_id, replaced);
			nd->slot_index = decl->slot_index; nd->is_static = decl->is_static; nd->function_spelling = decl->function_spelling; nd->ref_escaping = decl->ref_escaping;
			chunk_->nodes.push_back(nd);
			compile_variable_decl(nd);
		},
		[&]() { compile_variable_decl(decl); });
	return true;
}

// a return whose value holds the call: `return f(...);`
bool vm_compiler::try_inline_return(const statement_ptr& stmt) {
	if (inlining_off_) return false;
	auto* rs = static_cast<return_stmt*>(stmt.get());
	if (!rs->value || rs->binds_reference) return false;
	const function_decl* fn = nullptr;
	std::shared_ptr<call_expr> call = inline_call_of(rs->value, &fn);
	if (!call) return false;
	compile_inlined(call, fn, rs->value,
		[&](const expression_ptr& replaced) {
			auto nr = std::make_shared<return_stmt>(rs->location, replaced);
			nr->static_return_proof = rs->static_return_proof; nr->return_operand_class = rs->return_operand_class;
			chunk_->nodes.push_back(nr);
			compile_statement(nr);
		},
		[&]() { compile_statement(stmt); });
	return true;
}

} // namespace jai::vm
