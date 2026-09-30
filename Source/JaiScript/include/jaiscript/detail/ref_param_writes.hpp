#pragma once

#ifndef __JAISCRIPT_DETAIL_REF_PARAM_WRITES_HPP__
#define __JAISCRIPT_DETAIL_REF_PARAM_WRITES_HPP__

// Inferred-const reference parameters: whether a function can write through its `T&`
// parameter. A parameter the body never writes through takes a literal or a temporary
// (the caller binds a temporary cell, C++ `const T&` without the keyword); one it writes
// through keeps needing a variable. ONE kernel for the checker (the load diagnostic) and
// both backends (the bind): each supplies how a callee name resolves to script functions.
//
// A write through parameter p (or a reference alias of it: `auto& r = p.items;`,
// `for (auto& x : p)`): an assignment, compound assignment or ++/-- whose target is rooted
// at p; a method call on a p-rooted receiver unless p is an array, map or string and the
// method is a reading builtin (size, find, keys, ...); passing a p-rooted lvalue to a script
// function's reference parameter that writes (transitively), to a handle parameter, to a
// constructor, or to a callee the kernel cannot see (a function value, a host function, a
// user method) - the printing and conversion builtins, the math:: and string:: functions
// and the container builtins take copies; returning it by reference; the same inside the
// lambdas the body creates. Recursion assumes read-only until a write is found.

#include <jaiscript/detail/ast.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace jai::detail {

	struct ref_write_callee {
		const std::vector<parameter>* params = nullptr;
		const statement* body = nullptr;
		std::string_view name;
		bool returns_reference = false;
		bool coroutine = false;
	};

	struct ref_param_write {
		bool writes = false;
		size_t line = 0;
		size_t column = 0;
	};

	// true = the name is a script function the kernel can read (out holds its overloads);
	// false = not visible (the callee-unknown rule applies)
	using ref_write_resolver = std::function<bool(const identifier_expr&, std::vector<ref_write_callee>&)>;

	inline bool ref_write_reading_method(std::string_view name) {
		static constexpr std::string_view k_reading[] = {
			"size", "empty", "length", "front", "back", "first", "last", "at", "get", "has", "contains", "index_of",
			"slice", "join", "keys", "values", "to_array", "substr", "find", "rfind", "find_first_of", "find_last_of",
			"find_first_not_of", "find_last_not_of", "starts_with", "ends_with", "split", "explode", "lines", "partition",
			"compare", "to_int", "to_float", "is_digit", "is_alpha", "is_alnum", "is_space", "is_upper", "is_lower",
			"byte_size", "utf8_length", "utf8_chars", "to_utf8", "code", "count", "lock", "expired", "use_count",
		};
		for (auto m : k_reading) { if (name == m) { return true; } }
		return false;
	}

	inline bool ref_write_writing_builtin(std::string_view name) {
		static constexpr std::string_view k_writing[] = {
			"push", "push_back", "push_front", "pop", "pop_back", "pop_front", "insert", "erase", "remove", "remove_at",
			"clear", "sort", "reverse", "resize", "set", "swap", "fill", "shuffle", "append", "extend", "reserve",
			"to_lower", "to_upper", "trim", "trim_left", "trim_right", "pad_left", "pad_right", "pad_center",
			"replace_first", "replace_last", "replace_all", "remove_prefix", "remove_suffix", "repeat", "reset",
		};
		for (auto m : k_writing) { if (name == m) { return true; } }
		return false;
	}

	// The core builtins that take their arguments as copies (printing, conversion, identity)
	inline bool ref_write_copying_builtin(std::string_view name) {
		return name == "print" || name == "println" || name == "to_string" || name == "type_of" || name == "to_json" ||
		       name == "same_object" || name == "format" || name == "to_char" || name == "int" || name == "float" ||
		       name == "string" || name == "bool" || name == "char" || name == "double" || name == "int64";
	}

	class ref_write_analysis {
	public:
		explicit ref_write_analysis(ref_write_resolver resolver) : resolver_(std::move(resolver)) {}

		ref_param_write analyze(const ref_write_callee& fn, size_t index) {
			ref_param_write none;
			if (!fn.params || index >= fn.params->size() || !(*fn.params)[index].is_reference || fn.coroutine || !fn.body) { return none; }
			for (const auto& [body, i] : in_progress_) {
				if (body == fn.body && i == index) { return none; }   // recursion: read-only until a write is found
			}
			in_progress_.emplace_back(fn.body, index);
			walk w(*this, fn, (*fn.params)[index]);
			w.stmt(fn.body);
			in_progress_.pop_back();
			return w.found;
		}

	private:
		ref_write_resolver resolver_;
		std::vector<std::pair<const statement*, size_t>> in_progress_;

		struct walk {
			ref_write_analysis& owner;
			const ref_write_callee& fn;
			const type_info* param_type;
			std::vector<std::string_view> roots;
			std::vector<std::string_view> locals;
			ref_param_write found;

			walk(ref_write_analysis& o, const ref_write_callee& f, const parameter& p) : owner(o), fn(f), param_type(p.type.get()) {
				roots.push_back(p.name);
				for (const auto& q : *f.params) { locals.push_back(q.name); }
			}

			bool is_root(std::string_view name) const {
				for (auto r : roots) { if (r == name) { return true; } }
				return false;
			}
			bool is_local(std::string_view name) const {
				for (auto l : locals) { if (l == name) { return true; } }
				return false;
			}
			void write_at(const ast_node* n) {
				if (found.writes || !n) { return; }
				found.writes = true;
				found.line = n->location.line;
				found.column = n->location.column;
			}

			// The root identifier of an lvalue chain (p, p.f, p[i], p.f[i].g, cast<T&>(p)), else null
			static const identifier_expr* chain_root(const expression* e) {
				while (e) {
					switch (e->get_type()) {
						case node_type::identifier_expr: return static_cast<const identifier_expr*>(e);
						case node_type::member_expr: {
							auto* m = static_cast<const member_expr*>(e);
							if (m->is_static) { return nullptr; }
							e = m->object.get();
							break;
						}
						case node_type::binary_expr: {
							auto* b = static_cast<const binary_expr*>(e);
							if (b->op.type != token_type::left_bracket) { return nullptr; }
							e = b->left.get();
							break;
						}
						case node_type::cast_expr: e = static_cast<const cast_expr*>(e)->operand.get(); break;
						default: return nullptr;
					}
				}
				return nullptr;
			}
			bool rooted(const expression* e) const {
				const identifier_expr* r = chain_root(e);
				return r && !r->global_qualified && is_root(r->name);
			}

			// the root's declared type is one whose methods are the builtin library's (a reading name reads)
			bool library_receiver() const {
				if (!param_type) { return false; }
				switch (param_type->base_type) {
					case script_value_type::jai_array_type:
					case script_value_type::jai_map_type:
					case script_value_type::jai_string_type:
					case script_value_type::jai_utf8_string_type:
						return true;
					default:
						return false;
				}
			}

			void argument_passed(const call_expr* c, size_t i, const expression* arg) {
				if (found.writes || !rooted(arg)) { return; }
				const expression* callee = c->callee.get();
				if (callee->get_type() == node_type::identifier_expr) {
					auto* id = static_cast<const identifier_expr*>(callee);
					std::vector<ref_write_callee> overloads;
					if (!id->global_qualified && is_local(id->name)) { write_at(arg); return; }
					if (owner.resolver_ && owner.resolver_(*id, overloads)) {
						for (const auto& o : overloads) {
							if (!o.params || i >= o.params->size()) { continue; }
							const parameter& p = (*o.params)[i];
							const type_info* t = p.type.get();
							if (t && (t->base_type == script_value_type::jai_shared_ptr_type || t->base_type == script_value_type::jai_weak_ptr_type)) {
								write_at(arg);
								return;
							}
							if (p.is_reference && owner.analyze(o, i).writes) {
								write_at(arg);
								return;
							}
						}
						return;
					}
					if (!ref_write_copying_builtin(id->name)) { write_at(arg); }
					return;
				}
				if (callee->get_type() == node_type::member_expr) {
					auto* m = static_cast<const member_expr*>(callee);
					if (m->is_static && m->object && m->object->get_type() == node_type::identifier_expr) {
						const std::string_view ns = static_cast<const identifier_expr*>(m->object.get())->name;
						if (ns == "math" || ns == "string") { return; }   // the pure namespaces take copies
					}
					// a container builtin stores a copy of its argument (a user method of the same name on a class receiver is
					// the one shape this cannot tell apart)
					if (!m->is_static && (ref_write_reading_method(m->member) || ref_write_writing_builtin(m->member))) { return; }
				}
				write_at(arg);
			}

			void expr(const expression* e) {
				if (!e || found.writes) { return; }
				switch (e->get_type()) {
					case node_type::assignment_expr: {
						auto* a = static_cast<const assignment_expr*>(e);
						if (rooted(a->target.get())) { write_at(a); return; }
						expr(a->target.get());
						expr(a->value.get());
						return;
					}
					case node_type::unary_expr: {
						auto* u = static_cast<const unary_expr*>(e);
						if ((u->op.type == token_type::plus_plus || u->op.type == token_type::minus_minus) && rooted(u->operand.get())) {
							write_at(u);
							return;
						}
						expr(u->operand.get());
						return;
					}
					case node_type::call_expr: {
						auto* c = static_cast<const call_expr*>(e);
						if (c->callee && c->callee->get_type() == node_type::member_expr) {
							auto* m = static_cast<const member_expr*>(c->callee.get());
							if (!m->is_static && rooted(m->object.get()) && !(library_receiver() && ref_write_reading_method(m->member))) {
								write_at(c);
								return;
							}
						}
						expr(c->callee.get());
						for (size_t i = 0; i < c->arguments.size() && !found.writes; ++i) {
							argument_passed(c, i, c->arguments[i].get());
							expr(c->arguments[i].get());
						}
						return;
					}
					case node_type::binary_expr: {
						auto* b = static_cast<const binary_expr*>(e);
						expr(b->left.get());
						expr(b->right.get());
						return;
					}
					case node_type::member_expr: expr(static_cast<const member_expr*>(e)->object.get()); return;
					case node_type::ternary_expr: {
						auto* t = static_cast<const ternary_expr*>(e);
						expr(t->condition.get());
						expr(t->then_expression.get());
						expr(t->else_expression.get());
						return;
					}
					case node_type::array_literal_expr:
						for (const auto& el : static_cast<const array_literal_expr*>(e)->elements) { expr(el.get()); }
						return;
					case node_type::map_literal_expr:
						for (const auto& kv : static_cast<const map_literal_expr*>(e)->entries) { expr(kv.first.get()); expr(kv.second.get()); }
						return;
					case node_type::new_expr:
						for (const auto& a : static_cast<const new_expr*>(e)->arguments) {
							if (rooted(a.get())) { write_at(a.get()); return; }
							expr(a.get());
						}
						return;
					case node_type::lambda_expr: {
						auto* l = static_cast<const lambda_expr*>(e);
						for (const auto& p : l->parameters) { locals.push_back(p.name); }
						stmt(l->body.get());
						return;
					}
					case node_type::cast_expr: expr(static_cast<const cast_expr*>(e)->operand.get()); return;
					case node_type::throw_expr: expr(static_cast<const throw_expr*>(e)->value.get()); return;
					case node_type::yield_expr: expr(static_cast<const yield_expr*>(e)->value.get()); return;
					case node_type::include_expr: expr(static_cast<const include_expr*>(e)->path_expr.get()); return;
					case node_type::import_expr:
						for (const auto& a : static_cast<const import_expr*>(e)->path_exprs) { expr(a.get()); }
						return;
					default: return;
				}
			}

			void alias(std::string_view name, const expression* source) {
				if (rooted(source)) { roots.push_back(name); }
			}

			void stmt(const statement* s) {
				if (!s || found.writes) { return; }
				switch (s->get_type()) {
					case node_type::expression_stmt: expr(static_cast<const expression_stmt*>(s)->expression.get()); return;
					case node_type::expression_decl: expr(static_cast<const expression_decl*>(s)->expression.get()); return;
					case node_type::statement_decl: stmt(static_cast<const statement_decl*>(s)->statement.get()); return;
					case node_type::block_stmt:
						for (const auto& d : static_cast<const block_stmt*>(s)->declarations) { stmt(d.get()); }
						return;
					case node_type::if_stmt: {
						auto* is = static_cast<const if_stmt*>(s);
						expr(is->condition.get());
						stmt(is->then_statement.get());
						stmt(is->else_statement.get());
						return;
					}
					case node_type::while_stmt: {
						auto* ws = static_cast<const while_stmt*>(s);
						expr(ws->condition.get());
						stmt(ws->body.get());
						return;
					}
					case node_type::for_stmt: {
						auto* fs = static_cast<const for_stmt*>(s);
						stmt(fs->initializer.get());
						expr(fs->condition.get());
						expr(fs->update.get());
						stmt(fs->body.get());
						return;
					}
					case node_type::range_for_stmt: {
						auto* rf = static_cast<const range_for_stmt*>(s);
						locals.push_back(rf->variable_name);
						if (rf->is_reference) { alias(rf->variable_name, rf->container.get()); }
						expr(rf->container.get());
						stmt(rf->body.get());
						return;
					}
					case node_type::parallel_for_stmt: {
						auto* pf = static_cast<const parallel_for_stmt*>(s);
						locals.push_back(pf->loop_param.name);
						if (pf->loop_param.is_reference) { alias(pf->loop_param.name, pf->container.get()); }
						for (size_t k = 0; k < pf->extra_params.size() && k < pf->extra_containers.size(); ++k) {
							locals.push_back(pf->extra_params[k].name);
							if (pf->extra_params[k].is_reference) { alias(pf->extra_params[k].name, pf->extra_containers[k].get()); }
							expr(pf->extra_containers[k].get());
						}
						expr(pf->container.get());
						stmt(pf->body.get());
						return;
					}
					case node_type::return_stmt: {
						auto* r = static_cast<const return_stmt*>(s);
						if ((r->binds_reference || fn.returns_reference) && rooted(r->value.get())) { write_at(r); return; }
						expr(r->value.get());
						return;
					}
					case node_type::switch_stmt: {
						auto* sw = static_cast<const switch_stmt*>(s);
						expr(sw->condition.get());
						for (const auto& c : sw->cases) {
							expr(c->value.get());
							for (const auto& st : c->body) { stmt(st.get()); }
						}
						if (sw->default_case) {
							for (const auto& st : sw->default_case->body) { stmt(st.get()); }
						}
						return;
					}
					case node_type::try_stmt: {
						auto* tr = static_cast<const try_stmt*>(s);
						stmt(tr->try_block.get());
						stmt(tr->catch_block.get());
						return;
					}
					case node_type::variable_decl: {
						auto* vd = static_cast<const variable_decl*>(s);
						locals.push_back(vd->name);
						if (vd->type && vd->type->is_reference()) { alias(vd->name, vd->initializer.get()); }
						expr(vd->initializer.get());
						return;
					}
					case node_type::destructuring_decl: {
						auto* dd = static_cast<const destructuring_decl*>(s);
						for (const auto& [name, id] : dd->names) { locals.push_back(name); }
						expr(dd->initializer.get());
						return;
					}
					default: return;
				}
			}
		};
	};

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_REF_PARAM_WRITES_HPP__
