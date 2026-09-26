// Inlined leaves (docs/typed_regions_design.md, "Inlined leaves"): a plain identifier call the checker resolved to one small free
// script function compiles at its call site as the callee's body. The call becomes a temp: the operands the statement evaluates
// before the call go into typed temps first, in program order (the spill), then the graft declares the result temp (a guard on
// the callee's identity, the by-value parameters as declarations in slots above the caller's frame, a reference parameter renamed
// to the caller's variable, the body with every return a store into the result and a jump to the join; behind the join's jump the
// fallback, the real call declared into the same temp), and the statement runs once with the call and the spilled operands
// replaced by their temps. The caller's AST is never changed: the interpreter walks it, and the fallback compiles it.
#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>

#include <algorithm>
#include <functional>

namespace jai::vm {

namespace {

constexpr size_t k_inline_max_nodes = 512;   // the cloned body and the cloned statement together
constexpr size_t k_inline_max_hoists = 8;    // calls hoisted out of one statement

struct inline_context {
	size_t slot_offset = 0;                                        // the callee's slots move above the caller's frame
	std::vector<std::shared_ptr<identifier_expr>> renames;         // per callee slot: the caller's identifier a reference parameter takes, or null
	std::vector<uint8_t> ref_slots;                                // per callee slot: a reference parameter
	std::shared_ptr<identifier_expr> result;                       // the result temp (null: a void callee)
	type_info_ptr result_type;
	std::string_view callee_name;                                  // the return site's owner (an unproven return converts at the result's declaration)
	size_t nodes = 0;
	size_t budget = k_inline_max_nodes;                            // the body's clone is bounded; the statement's clone is compiled once whatever its size
	bool ok = true;
	const char* why = nullptr;                                     // the first rule that kept the call
	bool writes_global = false;
	bool writes_member = false;
	bool writes_ref = false;                                       // a store or an increment through a reference parameter: the caller's variable or element
	std::vector<std::shared_ptr<ast_node>> pins;
	std::vector<uint64_t> declared_names;                          // the callee's locals: declared in the caller for the graft's compile
	const call_expr* replace = nullptr;                            // the statement's clone: this call becomes the result temp
	bool in_loop = false;                                          // the graft sits inside a loop: every landing must be a block instruction
	int loop_depth = 0;                                            // loops of the callee's own the clone is inside (a break or continue binds there)
	std::vector<std::pair<const expression*, std::shared_ptr<identifier_expr>>> spills;   // the statement's clone: a spilled operand becomes its temp
	std::shared_ptr<identifier_expr> receiver;                     // a method graft: `this` and the bare fields read through the caller's receiver
	bool returns_proven = false;                                   // an untyped lambda whose returns the checker proved one scalar kind
	const class_definition* field_class = nullptr;
	std::vector<uint64_t> field_ids;
	template <class T, class... A> std::shared_ptr<T> make(A&&... a) {
		auto n = std::make_shared<T>(std::forward<A>(a)...);
		pins.push_back(n);
		if (++nodes > budget) { fail("a body too large"); }
		return n;
	}
	void fail(const char* w) { if (ok) { why = w; } ok = false; }
};

bool scalar_type(const type_info* t) {
	if (!t) return false;
	switch (t->base_type) {
	case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type: return true;
	default: return false;
	}
}
bool any_type(const type_info* t) { return !t || t->base_type == script_value_type::jai_any_type; }
bool void_type(const type_info* t) { return t && t->base_type == script_value_type::jai_null_type; }   // `void` is the null type
bool var_reference(const type_info* t) { return t && t->is_reference() && any_type(t->element_type().get()); }
bool math_call(const call_expr* ce) {
	return ce->callee && ce->callee->get_type() == node_type::member_expr && detail::math_intrinsic_for_call(static_cast<const member_expr*>(ce->callee.get())) != detail::math_fn::none;
}
bool logical_op(token_type t) { return t == token_type::ampersand_ampersand || t == token_type::pipe_pipe; }
bool incdec_op(token_type t) { return t == token_type::plus_plus || t == token_type::minus_minus; }

// an expression whose value lands in a slot by a block instruction (the typed declaration rows, the fused binary, the proven element
// read, the math row): a slot identifier or a literal; arithmetic, a comparison, a shift or a unary of those; an element read of a
// proven kind over a named container; a math intrinsic of those. A bare global read, a member read, a cast, a ternary, a store or
// any other call lands on the general form, which a block does not run.
bool landable(const expression* e);
// an operand of a block instruction: a landable value, or a global read (the fused binary's environment rows)
bool block_operand(const expression* e) { return e && (e->get_type() == node_type::identifier_expr || landable(e)); }
bool landable(const expression* e) {
	if (!e) return false;
	switch (e->get_type()) {
	case node_type::literal_expr: return true;
	case node_type::identifier_expr: return static_cast<const identifier_expr*>(e)->slot_index != SIZE_MAX;
	case node_type::member_expr: { auto* m = static_cast<const member_expr*>(e); return !m->is_static && m->object && m->object->get_type() == node_type::identifier_expr; }   // a member site over a named instance
	case node_type::binary_expr: {
		auto* b = static_cast<const binary_expr*>(e);
		if (b->op.type == token_type::left_bracket) {
			const uint8_t cls = b->static_operand_class;
			return (cls == operand_class_int_int || cls == operand_class_float_float) && b->left && b->left->get_type() == node_type::identifier_expr && block_operand(b->right.get());
		}
		return block_operand(b->left.get()) && block_operand(b->right.get());
	}
	case node_type::unary_expr: { auto* u = static_cast<const unary_expr*>(e); return !incdec_op(u->op.type) && block_operand(u->operand.get()); }
	case node_type::ternary_expr: {   // the condition's jumps and one landing per arm: a declaration or a slot store from it is a block's
		auto* x = static_cast<const ternary_expr*>(e);
		return block_operand(x->condition.get()) && block_operand(x->then_expression.get()) && block_operand(x->else_expression.get());
	}
	case node_type::call_expr: {
		auto* ce = static_cast<const call_expr*>(e);
		if (!math_call(ce)) return false;
		for (const auto& a : ce->arguments) { if (!block_operand(a.get())) return false; }
		return true;
	}
	default: return false;
	}
}
// a body statement a block runs: a store of a landable value into a slot, a renamed slot, a global, a field or an element; an increment
// of a slot identifier
bool landable_stmt(const expression* e) {
	if (!e) return false;
	switch (e->get_type()) {
	case node_type::assignment_expr: {
		auto* a = static_cast<const assignment_expr*>(e);
		const node_type tk = a->target->get_type();
		if (tk == node_type::binary_expr) { auto* b = static_cast<const binary_expr*>(a->target.get()); if (b->op.type != token_type::left_bracket || !landable(b->right.get())) return false; }
		else if (tk == node_type::member_expr) { auto* m = static_cast<const member_expr*>(a->target.get()); if (m->is_static || !m->object || m->object->get_type() != node_type::identifier_expr) return false; }
		else if (tk != node_type::identifier_expr) return false;
		return block_operand(a->value.get());
	}
	case node_type::unary_expr: { auto* u = static_cast<const unary_expr*>(e); return incdec_op(u->op.type) && u->operand && u->operand->get_type() == node_type::identifier_expr && static_cast<const identifier_expr*>(u->operand.get())->slot_index != SIZE_MAX; }
	default: return false;
	}
}

expression_ptr clone_expr(const expression_ptr& e, inline_context& c);

std::shared_ptr<identifier_expr> copy_identifier(const identifier_expr* r, const source_location& loc, inline_context& c) {
	auto n = c.make<identifier_expr>(loc, r->name, r->symbol_id);
	n->slot_index = r->slot_index; n->global_qualified = r->global_qualified; n->names_value_decl = r->names_value_decl; n->names_known_value = r->names_known_value;
	n->proven_class = r->proven_class; n->proven_slot = r->proven_slot; n->proven_epoch = r->proven_epoch; n->proven_field_type = r->proven_field_type; n->proven_field_write = r->proven_field_write;
	return n;
}

std::shared_ptr<identifier_expr> clone_identifier(const identifier_expr* id, inline_context& c) {
	if (id->slot_index != SIZE_MAX && id->slot_index < c.renames.size() && c.renames[id->slot_index]) { return copy_identifier(c.renames[id->slot_index].get(), id->location, c); }
	auto n = copy_identifier(id, id->location, c);
	n->slot_index = id->slot_index == SIZE_MAX ? SIZE_MAX : id->slot_index + c.slot_offset;
	return n;
}

// a store's target: a global, the caller's variable through a reference parameter (renamed or aliased), a field or an element
void note_store(const expression* target, inline_context& c) {
	if (target->get_type() == node_type::identifier_expr) {
		auto* t = static_cast<const identifier_expr*>(target);
		const bool renamed = t->slot_index != SIZE_MAX && t->slot_index < c.renames.size() && c.renames[t->slot_index];
		if (t->slot_index == SIZE_MAX || renamed) { c.writes_global = true; }
		if (t->slot_index != SIZE_MAX && t->slot_index < c.ref_slots.size() && c.ref_slots[t->slot_index]) { c.writes_ref = true; }
		return;
	}
	c.writes_member = true;
}

// the expression forms an inlined body and an inlined statement may hold: no call but a math intrinsic, no lambda, no `this`, no
// literal container, no yield, no throw, no include. The clone keeps every proof the checker stamped, so the graft compiles as
// typed as the original.
expression_ptr clone_expr(const expression_ptr& e, inline_context& c) {
	if (!c.ok || !e) return nullptr;
	for (const auto& s : c.spills) { if (s.first == e.get()) { return copy_identifier(s.second.get(), e->location, c); } }
	switch (e->get_type()) {
	case node_type::literal_expr: ++c.nodes; return e;   // immutable: shared as it is
	case node_type::identifier_expr: {
		auto* id = static_cast<const identifier_expr*>(e.get());
		if (c.receiver && id->slot_index == SIZE_MAX) {
			// a method's bare name: a field the checker proved (no user accessor, the method's own class) reads through the receiver;
			// anything else (a global, an unproven member) keeps the call
			if (!id->this_field || id->global_qualified) { c.fail("a bare name in a method the graft cannot resolve"); return nullptr; }
			if (std::find(c.field_ids.begin(), c.field_ids.end(), id->symbol_id) == c.field_ids.end()) { c.field_ids.push_back(id->symbol_id); }
			auto n = c.make<member_expr>(id->location, copy_identifier(c.receiver.get(), id->location, c), id->name, id->symbol_id, false, false);
			n->proven_class = id->proven_class; n->proven_slot = id->proven_slot; n->proven_epoch = id->proven_epoch; n->proven_field_type = id->proven_field_type; n->proven_field_write = id->proven_field_write;
			return n;
		}
		return clone_identifier(id, c);
	}
	case node_type::this_expr:
		if (!c.receiver) { c.fail("an expression the graft does not take"); return nullptr; }
		return copy_identifier(c.receiver.get(), e->location, c);
	case node_type::binary_expr: {
		auto* b = static_cast<const binary_expr*>(e.get());
		auto l = clone_expr(b->left, c); auto r = clone_expr(b->right, c);
		if (!c.ok) return nullptr;
		auto n = c.make<binary_expr>(b->location, l, b->op, r);
		n->transient_element_read = b->transient_element_read;
		n->static_operand_class = b->static_operand_class;
		return n;
	}
	case node_type::unary_expr: {
		auto* u = static_cast<const unary_expr*>(e.get());
		if (incdec_op(u->op.type) && u->operand) { note_store(u->operand.get(), c); }   // an identifier's increment (any other target desugars to a compound store)
		auto o = clone_expr(u->operand, c);
		if (!c.ok) return nullptr;
		return c.make<unary_expr>(u->location, u->op, o, u->is_postfix);
	}
	case node_type::assignment_expr: {
		auto* a = static_cast<const assignment_expr*>(e.get());
		const node_type tk = a->target->get_type();
		if (tk != node_type::identifier_expr && tk != node_type::member_expr && tk != node_type::binary_expr) { c.fail("a store target the graft does not take"); return nullptr; }
		note_store(a->target.get(), c);
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
		n->null_safe = m->null_safe; n->getter_id = m->getter_id;
		n->proven_class = m->proven_class; n->proven_slot = m->proven_slot; n->proven_epoch = m->proven_epoch; n->proven_field_type = m->proven_field_type; n->proven_field_write = m->proven_field_write;
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
		if (c.replace && ce == c.replace) { ++c.nodes; return c.result ? copy_identifier(c.result.get(), e->location, c) : nullptr; }   // the statement: the call is its temp (null: a void call alone)
		if (!c.replace && !math_call(ce)) { c.fail("a call in the body"); return nullptr; }   // in a body, any other call (a script or host function, a method) is an effect the graft does not take; the statement's clone keeps its calls (the plan ordered them)
		std::vector<expression_ptr> args;
		for (const auto& a : ce->arguments) { args.push_back(clone_expr(a, c)); }
		auto callee = clone_expr(ce->callee, c);
		if (!c.ok) return nullptr;
		auto n = c.make<call_expr>(ce->location, callee, std::move(args));
		n->static_arg_proofs = ce->static_arg_proofs; n->proven_callee_body = ce->proven_callee_body; n->proven_callee_decl = ce->proven_callee_decl;
		// a replaced argument is the hoisted leaf's result temp, a slot of its return type: the outer call's proof for it is the types' agreement
		if (c.replace && c.result && c.result_type && ce->proven_callee_decl) {
			for (size_t i = 0; i < ce->arguments.size() && i < ce->proven_callee_decl->parameters.size(); ++i) {
				if (ce->arguments[i].get() != c.replace) continue;
				const parameter& p = ce->proven_callee_decl->parameters[i];
				if (p.is_reference || !p.type || !scalar_type(p.type.get()) || p.type->base_type != c.result_type->base_type) continue;
				if (n->static_arg_proofs.size() < ce->arguments.size()) { n->static_arg_proofs.resize(ce->arguments.size(), proof_none); }
				n->static_arg_proofs[i] = proof_same_type;
			}
		}
		return n;
	}
	default:
		c.fail("an expression the graft does not take"); return nullptr;
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

std::vector<declaration_ptr> clone_body(const std::vector<declaration_ptr>& decls, inline_context& c, bool final_position);

// the statements a body may hold: a block, an if, an expression statement and a return. A return declares the result temp from its
// value (the checker's proof, or the return site's conversion) and jumps to the graft's join, unless it is the body's final
// statement, where the join follows anyway
statement_ptr clone_stmt(const statement_ptr& s, inline_context& c, bool final_position) {
	if (!c.ok || !s) return nullptr;
	switch (s->get_type()) {
	case node_type::block_stmt: {
		auto* b = static_cast<const block_stmt*>(s.get());
		std::vector<declaration_ptr> ds = clone_body(b->declarations, c, final_position);
		if (!c.ok) return nullptr;
		return c.make<block_stmt>(b->location, std::move(ds));
	}
	case node_type::if_stmt: {
		auto* i = static_cast<const if_stmt*>(s.get());
		auto cond = clone_expr(i->condition, c);
		statement_ptr t = clone_stmt(i->then_statement, c, final_position);
		statement_ptr el = i->else_statement ? clone_stmt(i->else_statement, c, final_position) : nullptr;
		if (!c.ok) return nullptr;
		return c.make<if_stmt>(i->location, cond, t, el);
	}
	case node_type::return_stmt: {
		auto* r = static_cast<const return_stmt*>(s.get());
		if (c.loop_depth > 0) { c.fail("a return inside a loop"); return nullptr; }
		std::vector<declaration_ptr> ds;
		if (!c.result) {
			if (r->value) { c.fail("a value returned from a void callee"); return nullptr; }
		} else {
			if (!r->value || r->binds_reference) { c.fail("a return without a value or by reference"); return nullptr; }
			const bool proven_return = r->static_return_proof == proof_same_type || c.returns_proven;
			if (c.in_loop && !proven_return) { c.fail("an unproven return in a loop"); return nullptr; }   // the result's declaration must be a block instruction: proven and landable
			expression_ptr v = clone_expr(r->value, c);
			if (!c.ok) return nullptr;
			if (c.in_loop && !landable(v.get())) { c.fail("a return the block cannot land"); return nullptr; }
			auto rd = c.make<variable_decl>(r->location, c.result_type, c.result->name, c.result->symbol_id, v);   // `T __h = e;`: this path's definition of the result
			rd->slot_index = c.result->slot_index;
			if (proven_return) { rd->static_init_proof = proof_same_type; }
			else { rd->site_kind = variable_decl::site_return; rd->site_owner = c.callee_name; }   // the callee's return converted here, with the return's text
			ds.push_back(rd);
		}
		if (!final_position) {
			auto ex = c.make<return_stmt>(r->location, nullptr);
			ex->inline_exit = true;
			ds.push_back(c.make<statement_decl>(r->location, ex));
		}
		return c.make<block_stmt>(r->location, std::move(ds));
	}
	case node_type::expression_stmt: {
		auto* es = static_cast<const expression_stmt*>(s.get());
		auto e = clone_expr(es->expression, c);
		if (!c.ok) return nullptr;
		if (c.in_loop && !landable_stmt(e.get())) { c.fail("a statement the block cannot land"); return nullptr; }
		std::vector<declaration_ptr> ds; ds.push_back(c.make<expression_decl>(es->location, e, false));
		return c.make<block_stmt>(es->location, std::move(ds));
	}
	case node_type::for_stmt: {
		auto* fs = static_cast<const for_stmt*>(s.get());
		declaration_ptr init;
		if (fs->initializer) {
			std::vector<declaration_ptr> one = clone_body({fs->initializer}, c, false);
			if (!c.ok) return nullptr;
			if (one.size() != 1) { c.fail("a loop the graft does not take"); return nullptr; }
			init = one[0];
		}
		if (c.in_loop && fs->condition && !landable(fs->condition.get())) { c.fail("a loop the block cannot land"); return nullptr; }
		if (c.in_loop && fs->update && !landable_stmt(fs->update.get())) { c.fail("a loop the block cannot land"); return nullptr; }
		auto cond = fs->condition ? clone_expr(fs->condition, c) : nullptr;
		auto upd = fs->update ? clone_expr(fs->update, c) : nullptr;
		++c.loop_depth;
		statement_ptr body = clone_stmt(fs->body, c, false);
		--c.loop_depth;
		if (!c.ok) return nullptr;
		return c.make<for_stmt>(fs->location, init, cond, upd, body);
	}
	case node_type::while_stmt: {
		auto* ws = static_cast<const while_stmt*>(s.get());
		if (c.in_loop && !landable(ws->condition.get())) { c.fail("a loop the block cannot land"); return nullptr; }
		auto cond = clone_expr(ws->condition, c);
		++c.loop_depth;
		statement_ptr body = clone_stmt(ws->body, c, false);
		--c.loop_depth;
		if (!c.ok) return nullptr;
		return c.make<while_stmt>(ws->location, cond, body);
	}
	case node_type::break_stmt:
	case node_type::continue_stmt:
		if (c.loop_depth == 0) { c.fail("a statement the graft does not take"); return nullptr; }
		return s;   // no state: shared as it is
	default:
		c.fail("a statement the graft does not take"); return nullptr;
	}
}

std::vector<declaration_ptr> clone_body(const std::vector<declaration_ptr>& decls, inline_context& c, bool final_position) {
	std::vector<declaration_ptr> out;
	for (size_t k = 0; k < decls.size() && c.ok; ++k) {
		const declaration_ptr& d = decls[k];
		const bool last = final_position && k + 1 == decls.size();
		switch (d->get_type()) {
		case node_type::variable_decl: {
			auto* v = static_cast<const variable_decl*>(d.get());
			if (v->slot_index == SIZE_MAX || v->is_static || v->ref_escaping || v->function_spelling) { c.fail("a declaration the graft does not take"); return out; }
			if (v->type && v->type->is_reference()) { c.fail("a reference declaration"); return out; }
			if (c.in_loop && v->initializer && !landable(v->initializer.get())) { c.fail("a declaration the block cannot land"); return out; }   // a declaration the block cannot land: the call stays
			expression_ptr init = v->initializer ? clone_expr(v->initializer, c) : nullptr;
			if (!c.ok) return out;
			auto n = c.make<variable_decl>(v->location, v->type, v->name, v->name_id, init);
			n->slot_index = v->slot_index + c.slot_offset;
			n->static_init_proof = v->static_init_proof;
			n->decl_fast_flags = v->decl_fast_flags;
			c.declared_names.push_back(v->name_id);
			out.push_back(n);
			break;
		}
		case node_type::expression_decl: {
			auto* ed = static_cast<const expression_decl*>(d.get());
			if (ed->implicit_return) { c.fail("an implicit return"); return out; }
			if (c.in_loop && !landable_stmt(ed->expression.get())) { c.fail("a statement the block cannot land"); return out; }
			auto e = clone_expr(ed->expression, c);
			if (!c.ok) return out;
			out.push_back(c.make<expression_decl>(ed->location, e, false));
			break;
		}
		case node_type::statement_decl: {
			const statement_ptr& s = static_cast<const statement_decl*>(d.get())->statement;
			if (!s) break;
			statement_ptr n = clone_stmt(s, c, last);
			if (!c.ok) return out;
			out.push_back(c.make<statement_decl>(s->location, n));
			break;
		}
		default:
			c.fail("a declaration the graft does not take"); return out;
		}
	}
	return out;
}

// the calls of an expression in the order they run: a callee's calls, then the arguments' in order, then the call itself; a math
// intrinsic is no call. A node the graft's clone cannot hold ends the scan.
void call_events(const expression_ptr& e, std::vector<std::shared_ptr<call_expr>>& out, bool& unsupported) {
	if (!e || unsupported) return;
	switch (e->get_type()) {
	case node_type::literal_expr: case node_type::identifier_expr: return;
	case node_type::binary_expr: { auto* b = static_cast<const binary_expr*>(e.get()); call_events(b->left, out, unsupported); call_events(b->right, out, unsupported); return; }
	case node_type::unary_expr: call_events(static_cast<const unary_expr*>(e.get())->operand, out, unsupported); return;
	case node_type::member_expr: { auto* m = static_cast<const member_expr*>(e.get()); if (!m->is_static) { call_events(m->object, out, unsupported); } return; }
	case node_type::ternary_expr: { auto* t = static_cast<const ternary_expr*>(e.get()); call_events(t->condition, out, unsupported); call_events(t->then_expression, out, unsupported); call_events(t->else_expression, out, unsupported); return; }
	case node_type::cast_expr: call_events(static_cast<const cast_expr*>(e.get())->operand, out, unsupported); return;
	case node_type::assignment_expr: { auto* a = static_cast<const assignment_expr*>(e.get()); call_events(a->value, out, unsupported); call_events(a->target, out, unsupported); return; }
	case node_type::call_expr: {
		auto ce = std::static_pointer_cast<call_expr>(e);
		if (ce->callee && ce->callee->get_type() == node_type::member_expr) { auto* m = static_cast<const member_expr*>(ce->callee.get()); if (!m->is_static) { call_events(m->object, out, unsupported); } }
		else { call_events(ce->callee, out, unsupported); }
		for (const auto& a : ce->arguments) { call_events(a, out, unsupported); }
		if (!math_call(ce.get())) { out.push_back(ce); }
		return;
	}
	default: unsupported = true; return;
	}
}

// the ancestors of a node, the root first, the node last
bool find_path(const expression_ptr& root, const expression* target, std::vector<expression_ptr>& path) {
	if (!root) return false;
	path.push_back(root);
	if (root.get() == target) return true;
	auto down = [&](const expression_ptr& x) { return find_path(x, target, path); };
	bool found = false;
	switch (root->get_type()) {
	case node_type::binary_expr: { auto* b = static_cast<const binary_expr*>(root.get()); found = down(b->left) || down(b->right); break; }
	case node_type::unary_expr: found = down(static_cast<const unary_expr*>(root.get())->operand); break;
	case node_type::member_expr: found = down(static_cast<const member_expr*>(root.get())->object); break;
	case node_type::ternary_expr: { auto* t = static_cast<const ternary_expr*>(root.get()); found = down(t->condition) || down(t->then_expression) || down(t->else_expression); break; }
	case node_type::cast_expr: found = down(static_cast<const cast_expr*>(root.get())->operand); break;
	case node_type::assignment_expr: { auto* a = static_cast<const assignment_expr*>(root.get()); found = down(a->value) || down(a->target); break; }
	case node_type::call_expr: { auto* ce = static_cast<const call_expr*>(root.get()); found = down(ce->callee); for (size_t i = 0; !found && i < ce->arguments.size(); ++i) { found = down(ce->arguments[i]); } break; }
	default: break;
	}
	if (!found) path.pop_back();
	return found;
}

// the type a spilled operand's temp takes, from the checker's operand class: a comparison or a logical operator yields bool, an
// element read its proven kind, arithmetic the class's result; jai_null_type when the subtree is no spill
script_value_type spill_kind(const expression* e) {
	switch (e->get_type()) {
	case node_type::binary_expr: {
		auto* b = static_cast<const binary_expr*>(e);
		const uint8_t cls = b->static_operand_class;
		if (logical_op(b->op.type)) return script_value_type::jai_bool_type;
		if (cls == operand_class_unknown) return script_value_type::jai_null_type;
		if (b->returns_bool()) return script_value_type::jai_bool_type;
		if (b->op.type == token_type::left_bracket) {
			return cls == operand_class_int_int ? script_value_type::jai_int_type : cls == operand_class_float_float ? script_value_type::jai_float_type : script_value_type::jai_null_type;
		}
		switch (b->op.type) {
		case token_type::plus: case token_type::minus: case token_type::star: case token_type::slash: case token_type::percent:
			if (cls == operand_class_int_int) return script_value_type::jai_int_type;
			if (cls == operand_class_float_float || cls == operand_class_int_float || cls == operand_class_float_int) return script_value_type::jai_float_type;
			if (cls == operand_class_string_string && b->op.type == token_type::plus) return script_value_type::jai_string_type;
			return script_value_type::jai_null_type;
		case token_type::ampersand: case token_type::pipe: case token_type::caret: case token_type::left_shift: case token_type::right_shift:
			return cls == operand_class_int_int ? script_value_type::jai_int_type : script_value_type::jai_null_type;
		default: return script_value_type::jai_null_type;
		}
	}
	case node_type::unary_expr: return static_cast<const unary_expr*>(e)->op.type == token_type::bang ? script_value_type::jai_bool_type : script_value_type::jai_null_type;
	default: return script_value_type::jai_null_type;
	}
}

} // namespace

// the callee's shape and the site's arguments (the statement's position is the caller's business): one small body of the forms the
// clone takes, every by-value parameter a scalar the checker proved or a `var` taking a scalar local or a literal, every reference
// parameter a `var&` bound to a plain identifier or an element lvalue
const char* vm_compiler::inline_eligible(const call_expr* call, const function_decl* fn) const {
	if (!ir_passes_ || !typed_lowering_ || !callable_.active) return "the middle end is off";
	if (!fn || !fn->body) return "a callee without a body";
	if (fn->is_coroutine || fn->is_static) return "a coroutine or a static callee";
	if (fn->body.get() == callable_body_) return "a recursive call";
	if (call->arguments.size() != fn->parameters.size()) return "an arity the callee does not declare";
	const type_info* rt = fn->return_type.get();
	if (!(void_type(rt) || scalar_type(rt) || (!rt && fn->graft_return_kind != 0))) return "a result the graft does not declare";   // a string result declares through the general DECL_VAR, which opens the enclosing block
	if (!void_type(rt) && !terminates(fn->body.get())) return "a body that falls out without a return";   // the result is defined on every path or the callee is not a leaf the graft takes
	for (size_t i = 0; i < fn->parameters.size(); ++i) {
		const parameter& p = fn->parameters[i];
		const expression* a = call->arguments[i].get();
		if (p.slot_index == SIZE_MAX || p.slot_index >= fn->local_count || p.default_value) return "a parameter with a default or without a slot";
		if (p.is_reference) {
			const bool typed_to_name = a->get_type() == node_type::identifier_expr;   // a typed one bound to a caller's local is that local: the declared type is the checker's (no bind or store checks it)
			if (!(!p.type || any_type(p.type.get()) || var_reference(p.type.get()) || typed_to_name) || p.ref_escaping) return "a reference parameter that is not a var&";   // `var& p` (the parser keeps the referent's type and the reference flag apart)
			if (a->get_type() == node_type::identifier_expr) {
				if (static_cast<const identifier_expr*>(a)->global_qualified) return "a reference argument spelled ::name";
				if (static_cast<const identifier_expr*>(a)->slot_index == SIZE_MAX) return "a reference argument that is not a local";   // a global, a field or a method name binds through the caller's rules
				continue;
			}
			if (a->get_type() == node_type::binary_expr && static_cast<const binary_expr*>(a)->op.type == token_type::left_bracket) continue;
			return "a reference argument that is not a name or an element";
		}
		if (p.ref_escaping) return "a parameter that escapes";
		const bool proven = i < call->static_arg_proofs.size() && call->static_arg_proofs[i] == proof_same_type;
		if (scalar_type(p.type.get())) { if (!proven) return "an unproven argument"; continue; }   // an unproven argument would bind out of the call's order (every argument before the first binding)
		if (any_type(p.type.get())) {
			if (a->get_type() == node_type::literal_expr) continue;
			if (a->get_type() == node_type::identifier_expr) {
				auto* id = static_cast<const identifier_expr*>(a);
				if (id->slot_index != SIZE_MAX && callable_.primitive_slots.count(static_cast<uint32_t>(id->slot_index))) continue;
			}
			return "a var parameter bound to more than a scalar local or a literal";
		}
		return "a parameter type the graft does not take";
	}
	return nullptr;
}

// the first call the statement runs is a leaf the graft takes, in an unconditional position, with every operand evaluated before
// it a literal, an identifier the body leaves alone, or a stamped subtree that spills into a typed temp. Any other first call ends
// the statement's hoisting: a call after it would run behind an effect.
bool vm_compiler::plan_hoist(const expression_ptr& root, bool statement_position, hoist_plan& out) {
	std::vector<std::shared_ptr<call_expr>> events; bool unsupported = false;
	call_events(root, events, unsupported);
	if (events.empty()) return false;
	const std::shared_ptr<call_expr>& call = events.front();
	out.call = call;   // the call the plan is about, named with the rule that keeps it when the plan fails
	if (unsupported) { out.why = "a statement the graft does not take"; return false; }
	const function_decl* fn = nullptr;
	out.receiver = nullptr;
	if (call->callee && call->callee->get_type() == node_type::member_expr && call->proven_method_decl) {
		auto* m = static_cast<const member_expr*>(call->callee.get());
		auto* recv = static_cast<identifier_expr*>(m->object.get());
		const uint32_t rs = identifier_slot_operand(recv);
		if (rs == k_invalid_u32 || rs >= k_typed_slot_mask) { out.why = "a receiver that is not a local"; return false; }
		fn = call->proven_method_decl;
		out.receiver = std::static_pointer_cast<identifier_expr>(m->object);
	} else {
		if (!call->callee || call->callee->get_type() != node_type::identifier_expr) { out.why = "a callee that is not a name"; return false; }
		if (!call->proven_callee_decl) { out.why = "an unresolved callee"; return false; }
		fn = call->proven_callee_decl;
	}
	if (const char* why = inline_eligible(call.get(), fn)) { out.why = why; return false; }
	if (void_type(fn->return_type.get()) && !(statement_position && root.get() == call.get())) { out.why = "a void result used as a value"; return false; }   // a void result is a value nowhere but alone in a statement
	// the body clones, and its writes decide what may stand before the call
	bool in_loop = false;
	for (const loop_context& lc : loops_) { if (lc.kind == region_kind::loop || lc.kind == region_kind::range_for) { in_loop = true; break; } }
	if (in_loop) {   // inside a loop every parameter's argument lands by a block instruction, or the call stays (its landing converts what a declaration cannot)
		for (size_t i = 0; i < fn->parameters.size(); ++i) { if (!fn->parameters[i].is_reference && !landable(call->arguments[i].get())) { out.why = "an argument the block cannot land"; return false; } }
	}
	inline_context probe; probe.renames.assign(fn->local_count, nullptr); probe.result_type = graft_result_type(fn); probe.callee_name = fn->name; probe.in_loop = in_loop;
	probe.returns_proven = fn->graft_return_kind != 0;
	probe.receiver = out.receiver;
	probe.ref_slots.assign(fn->local_count, 0);
	for (const parameter& p : fn->parameters) { if (p.is_reference && p.slot_index < fn->local_count) { probe.ref_slots[p.slot_index] = 1; } }
	if (!void_type(fn->return_type.get())) { auto r = std::make_shared<identifier_expr>(fn->body->location, std::string_view("\x01inl"), uint64_t(1)); probe.result = r; probe.pins.push_back(r); }
	(void)clone_body(fn->body->declarations, probe, true);
	if (!probe.ok) { out.why = probe.why ? probe.why : "a body the graft does not take"; return false; }
	// the operands before the call: the left siblings along the path from the root
	std::vector<expression_ptr> path;
	if (!find_path(root, call.get(), path)) { out.why = "a position the graft does not take"; return false; }
	std::vector<expression_ptr> pre;
	for (size_t k = 0; k + 1 < path.size(); ++k) {
		const expression_ptr& a = path[k]; const expression* x = path[k + 1].get();
		switch (a->get_type()) {
		case node_type::binary_expr: {
			auto* b = static_cast<const binary_expr*>(a.get());
			if (x == b->right.get()) { if (logical_op(b->op.type)) { out.why = "an operand behind && or ||"; return false; } pre.push_back(b->left); }
			break;
		}
		case node_type::ternary_expr: { auto* t = static_cast<const ternary_expr*>(a.get()); if (x != t->condition.get()) { out.why = "a call in a ternary's arm"; return false; } break; }
		case node_type::assignment_expr: { auto* s = static_cast<const assignment_expr*>(a.get()); if (x != s->value.get()) { out.why = "a call in a store's target"; return false; } break; }
		case node_type::call_expr: {
			auto* ce = static_cast<const call_expr*>(a.get());
			if (x == ce->callee.get()) break;
			if (!ce->callee) { out.why = "a callee that is not a name"; return false; }
			if (ce->callee->get_type() == node_type::member_expr) { if (!math_call(ce)) { out.why = "a method call's argument"; return false; } }   // a method's receiver and lookup stand before its arguments
			else if (ce->callee->get_type() == node_type::identifier_expr) { pre.push_back(ce->callee); }
			else { out.why = "an argument of a computed callee"; return false; }
			for (const auto& arg : ce->arguments) { if (arg.get() == x) break; pre.push_back(arg); }
			break;
		}
		case node_type::unary_expr: case node_type::cast_expr: case node_type::member_expr: break;
		default: out.why = "a position the graft does not take"; return false;
		}
	}
	out.spills.clear();
	bool free_identifier = false, free_global = false;
	for (const expression_ptr& p : pre) {
		switch (p->get_type()) {
		case node_type::literal_expr: break;
		case node_type::identifier_expr: { free_identifier = true; if (static_cast<const identifier_expr*>(p.get())->slot_index == SIZE_MAX) { free_global = true; } break; }
		default: {
			const script_value_type kind = spill_kind(p.get());
			if (kind == script_value_type::jai_null_type || !landable(p.get())) { out.why = "an operand before the call the block cannot spill"; return false; }   // a spill is a block instruction of a known kind
			out.spills.emplace_back(p, kind);
			break;
		}
		}
	}
	if (free_identifier && probe.writes_ref) { out.why = "a name before the call the body may write through its reference"; return false; }   // an identifier before the call may be the referent
	if (free_global && (probe.writes_global || probe.writes_member)) { out.why = "a global before the call the body writes"; return false; }
	if (out.receiver && free_identifier && probe.writes_member) { out.why = "a name before the call a method may write"; return false; }   // an identifier before the call may alias the receiver's instance
	out.fn = fn; out.in_loop = in_loop; out.why = nullptr;
	return true;
}

// the graft's result type: the declared one, or for an untyped lambda the kind the checker proved its returns yield
type_info_ptr vm_compiler::graft_result_type(const function_decl* fn) {
	if (fn->return_type || fn->graft_return_kind == 0) return fn->return_type;
	return inline_type(fn->graft_return_kind == 1 ? script_value_type::jai_int_type : fn->graft_return_kind == 2 ? script_value_type::jai_float_type : script_value_type::jai_bool_type);
}

type_info_ptr vm_compiler::inline_type(script_value_type kind) {
	for (auto& t : chunk_->owned_types) { if (t->base_type == kind) return type_info_ptr(t.get()); }
	const char* name = kind == script_value_type::jai_int_type ? "int" : kind == script_value_type::jai_float_type ? "float" : kind == script_value_type::jai_bool_type ? "bool" : "string";
	chunk_->owned_types.push_back(std::make_unique<type_info>(kind, std::string_view(name)));
	return type_info_ptr(chunk_->owned_types.back().get());
}

std::shared_ptr<identifier_expr> vm_compiler::inline_temp(const source_location& loc) {
	if (inline_symbol_ == 0) { inline_symbol_ = symbolizer_->intern("\x01inl"); }
	callable_.declared.insert(inline_symbol_);   // a private name no source spells: declared for the callable once
	auto t = std::make_shared<identifier_expr>(loc, std::string_view("\x01inl"), inline_symbol_);
	t->slot_index = temp_next_++; t->names_value_decl = true;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	chunk_->nodes.push_back(t);
	return t;
}

// the spills, the graft into the result temp, the statement rewritten over the temps
expression_ptr vm_compiler::compile_hoist(const hoist_plan& plan, const expression_ptr& root) {
	const function_decl* fn = plan.fn; const std::shared_ptr<call_expr>& call = plan.call;
	const bool was_off = inlining_off_;
	inlining_off_ = true;   // the spills, the graft's statements and the fallback compile plainly
	inline_context sc;      // the statement's clone: the spilled operands and the call become temps
	sc.budget = SIZE_MAX;
	for (const auto& [sub, kind] : plan.spills) {
		auto tid = inline_temp(sub->location);
		auto sd = std::make_shared<variable_decl>(sub->location, inline_type(kind), std::string_view("\x01inl"), inline_symbol_, sub);
		sd->slot_index = tid->slot_index; sd->static_init_proof = proof_same_type;
		chunk_->nodes.push_back(sd);
		compile_variable_decl(sd);
		sc.spills.emplace_back(sub.get(), tid);
	}
	std::shared_ptr<identifier_expr> result;
	if (!void_type(fn->return_type.get())) { result = inline_temp(call->location); }

	chunk::inline_site site;
	if (plan.receiver) {
		auto* m = static_cast<const member_expr*>(call->callee.get());
		site.method = true;
		site.receiver_slot = identifier_slot_operand(plan.receiver.get());
		site.member_id = m->member_id;
		site.name_symbol = add_symbol(m->member_id);
	} else {
		site.callee = make_fused_operand(call->callee);
		site.name_symbol = add_symbol(static_cast<identifier_expr*>(call->callee.get())->symbol_id);
	}
	site.body = fn->body.get();
	site.decl = fn;
	site.call_stmt = current_stmt_;
	const uint32_t site_index = static_cast<uint32_t>(chunk_->inline_sites.size());
	chunk_->inline_sites.push_back(site);
	const size_t guard_ip = emit(opcode::op_inline_guard, site_index, 0, 0);
	chunk_->inline_sites[site_index].guard_ip = static_cast<uint32_t>(guard_ip);
	chunk_->inline_sites[site_index].begin_ip = static_cast<uint32_t>(guard_ip + 1);

	const size_t base = temp_next_;
	temp_next_ += fn->local_count;   // the callee's slots
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	inline_context ctx;
	ctx.slot_offset = base;
	ctx.renames.assign(fn->local_count, nullptr);
	ctx.ref_slots.assign(fn->local_count, 0);
	for (const parameter& p : fn->parameters) { if (p.is_reference && p.slot_index < fn->local_count) { ctx.ref_slots[p.slot_index] = 1; } }
	ctx.result = result; ctx.result_type = graft_result_type(fn); ctx.callee_name = fn->name; ctx.in_loop = plan.in_loop;
	ctx.returns_proven = fn->graft_return_kind != 0;
	ctx.receiver = plan.receiver;
	// the parameters: a by-value parameter is a declaration of the callee's type from the argument (the assignment table
	// bind_parameters runs; the checker proved the type, so nothing converts); a reference parameter bound to an identifier is a
	// renaming, bound to an element lvalue an alias declaration (a rebind)
	std::vector<std::shared_ptr<variable_decl>> params;
	for (size_t i = 0; i < fn->parameters.size(); ++i) {
		const parameter& p = fn->parameters[i];
		const expression_ptr& a = call->arguments[i];
		if (p.is_reference && a->get_type() == node_type::identifier_expr) {
			auto* id = static_cast<const identifier_expr*>(a.get());
			ctx.renames[p.slot_index] = copy_identifier(id, id->location, ctx);
			continue;
		}
		auto d = std::make_shared<variable_decl>(call->location, p.type, p.name, p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name), a);
		d->slot_index = base + p.slot_index;
		ctx.pins.push_back(d);
		params.push_back(d);
	}
	std::vector<declaration_ptr> body = clone_body(fn->body->declarations, ctx, true);
	chunk_->inline_sites[site_index].field_class = ctx.field_class;
	chunk_->inline_sites[site_index].field_ids = ctx.field_ids;
	// the callee's names count as declared while the graft compiles (a slot identifier compiles to its slot only under a declared
	// name); the names the caller did not declare leave before the fallback, which compiles the call as the caller's
	std::vector<uint64_t> added;
	auto declare = [&](uint64_t id) { if (id != UINT64_MAX && callable_.declared.insert(id).second) { added.push_back(id); } };
	for (const parameter& p : fn->parameters) { declare(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name)); }
	for (const uint64_t id : ctx.declared_names) { declare(id); }
	loops_.push_back({});
	loops_.back().kind = region_kind::graft;
	loops_.back().scope_depth = scope_depth_;
	for (const auto& d : params) { compile_variable_decl(d); }
	for (const auto& d : body) { compile_declaration(d, false); }
	std::vector<size_t> exits = std::move(loops_.back().break_patches);
	loops_.pop_back();
	for (const uint64_t id : added) { callable_.declared.erase(id); }
	for (auto& n : ctx.pins) { chunk_->nodes.push_back(n); }
	const size_t jump_ip = emit(opcode::op_jump, 0, 0, 0);
	chunk_->inline_sites[site_index].fallback_ip = static_cast<uint32_t>(chunk_->code.size());
	chunk_->code[guard_ip].b = static_cast<uint32_t>(chunk_->code.size());
	// the fallback: the real call, declared into the result temp as whatever it returns (the callee is not the one the guard knew)
	if (result) {
		auto fd = std::make_shared<variable_decl>(call->location, nullptr, std::string_view("\x01inl"), inline_symbol_, call);
		fd->slot_index = result->slot_index;
		chunk_->nodes.push_back(fd);
		compile_variable_decl(fd);
	} else if (!compile_no_result_expression(call)) {
		compile_expression(call, true);
		emit(opcode::op_pop);
	}
	const size_t join = chunk_->code.size();
	chunk_->inline_sites[site_index].join_ip = static_cast<uint32_t>(join);
	patch_jump(jump_ip, join);
	for (size_t at : exits) { patch_jump(at, join); }
	temp_next_ = base;   // the callee's slots go; the temps stay for the statement
	for (size_t s = base; s < base + fn->local_count; ++s) { callable_.primitive_slots.erase(static_cast<uint32_t>(s)); }   // another graft may put anything in these slots
	if (result) { callable_.primitive_slots.erase(static_cast<uint32_t>(result->slot_index)); }   // the fallback declares it as anything
	inlining_off_ = was_off;
	sc.replace = call.get(); sc.result = result; sc.result_type = graft_result_type(fn);   // a replaced argument's proof reads the type
	expression_ptr replaced = clone_expr(root, sc);
	for (auto& n : sc.pins) { chunk_->nodes.push_back(n); }
	return sc.ok ? replaced : nullptr;
}

// every leaf the statement calls, first to last, into temps; the expression left for the statement to compile (the original when
// nothing was hoisted; null when the statement was one void call)
expression_ptr vm_compiler::hoist_leaf_calls(const expression_ptr& e, bool statement_position, bool& changed) {
	changed = false;
	if (inlining_off_ || !e || !ir_passes_ || !typed_lowering_ || !callable_.active) return e;
	expression_ptr cur = e;
	for (size_t round = 0; round < k_inline_max_hoists && cur; ++round) {
		hoist_plan plan;
		if (!plan_hoist(cur, statement_position && round == 0, plan)) {
			if (plan.why && plan.call && plan.call->callee && plan.call->callee->get_type() == node_type::identifier_expr) {   // the kept call, named for the listing and the profile
				chunk_->inline_refusals.push_back({std::string(static_cast<const identifier_expr*>(plan.call->callee.get())->name), plan.why, static_cast<uint32_t>(plan.call->location.line)});
			} else if (plan.why && plan.call && plan.call->proven_method_decl && plan.call->callee->get_type() == node_type::member_expr) {
				chunk_->inline_refusals.push_back({"." + std::string(static_cast<const member_expr*>(plan.call->callee.get())->member), plan.why, static_cast<uint32_t>(plan.call->location.line)});
			}
			break;
		}
		cur = compile_hoist(plan, cur);
		changed = true;
	}
	return cur;
}

// a graft's return: the scopes between it and the graft's start pop, and the jump lands on the join
void vm_compiler::compile_graft_exit() {
	size_t target = SIZE_MAX;
	for (size_t idx = loops_.size(); idx-- > 0;) { if (loops_[idx].kind == region_kind::graft) { target = idx; break; } }
	if (target == SIZE_MAX) return;
	emit_region_exits(target);
	auto& region = loops_[target];
	if (scope_depth_ > region.scope_depth) { emit(opcode::op_scope_pop_n, static_cast<uint32_t>(scope_depth_ - region.scope_depth)); }
	region.break_patches.push_back(emit(opcode::op_jump, k_invalid_u32));
}

// an expression statement: `f(...);`, `v = f(...);`, `s = s + f(...);`, `a.push(f(...));`
bool vm_compiler::try_inline_expression(const expression_ptr& e) {
	if (inlining_off_ || !e) return false;
	const size_t mark = temp_next_;
	bool changed = false;
	expression_ptr h = hoist_leaf_calls(e, true, changed);
	if (!changed) return false;
	if (h && h->get_type() != node_type::identifier_expr && !compile_no_result_expression(h)) { compile_expression(h, true); emit(opcode::op_pop); }   // a bare temp: the result dropped
	temp_next_ = mark;
	return true;
}

// a declaration whose initializer calls a leaf: `T v = f(...);`, `T v = a + f(...);`
bool vm_compiler::try_inline_variable_decl(const std::shared_ptr<variable_decl>& decl) {
	if (inlining_off_ || !decl->initializer || decl->slot_index == SIZE_MAX) return false;
	if (decl->type && decl->type->is_reference()) return false;
	const size_t mark = temp_next_;
	bool changed = false;
	expression_ptr h = hoist_leaf_calls(decl->initializer, false, changed);
	if (!changed) return false;
	auto nd = std::make_shared<variable_decl>(decl->location, decl->type, decl->name, decl->name_id, h);
	nd->slot_index = decl->slot_index; nd->is_static = decl->is_static; nd->function_spelling = decl->function_spelling; nd->ref_escaping = decl->ref_escaping;
	nd->static_init_proof = decl->static_init_proof; nd->decl_fast_flags = decl->decl_fast_flags; nd->site_kind = decl->site_kind; nd->site_owner = decl->site_owner;
	chunk_->nodes.push_back(nd);
	compile_variable_decl(nd);
	temp_next_ = mark;
	return true;
}

// a return whose value calls a leaf: `return f(...);`, `return a + f(...);`
bool vm_compiler::try_inline_return(const statement_ptr& stmt) {
	if (inlining_off_) return false;
	auto* rs = static_cast<return_stmt*>(stmt.get());
	if (!rs->value || rs->binds_reference) return false;
	const size_t mark = temp_next_;
	bool changed = false;
	expression_ptr h = hoist_leaf_calls(rs->value, false, changed);
	if (!changed) return false;
	auto nr = std::make_shared<return_stmt>(rs->location, h);
	nr->static_return_proof = rs->static_return_proof; nr->return_operand_class = rs->return_operand_class;
	chunk_->nodes.push_back(nr);
	compile_statement(nr);
	temp_next_ = mark;
	return true;
}

// a condition (an if, a while, a for): the grafts precede the condition's jumps; the caller frees the temps after them
expression_ptr vm_compiler::hoisted_condition(const expression_ptr& cond) {
	bool changed = false;
	expression_ptr h = hoist_leaf_calls(cond, false, changed);
	return h ? h : cond;
}

// a for's update: a statement position
void vm_compiler::compile_hoisted_update(const expression_ptr& update) {
	const size_t mark = temp_next_;
	bool changed = false;
	expression_ptr u = hoist_leaf_calls(update, true, changed);
	if (u && u->get_type() != node_type::identifier_expr && !compile_no_result_expression(u)) { compile_expression(u, true); emit(opcode::op_pop); }
	temp_next_ = mark;
}

} // namespace jai::vm
