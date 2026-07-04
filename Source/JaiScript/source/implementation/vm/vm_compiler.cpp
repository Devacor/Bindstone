#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <cassert>
#include <functional>

namespace jai::vm {

namespace {

	// Shared body walker mirroring the interpreter's lambda-analysis coverage
	// (interpreter.cpp find_identifiers / collect_outer_slots_*). Never descends
	// into nested lambda_expr or function_decl bodies.
	struct body_walker {
		std::function<void(identifier_expr*)> on_identifier;
		std::function<void(uint64_t)> on_declared;
		string_symbolizer* symbolizer;

		void expr(const expression_ptr& e) const {
			if (!e) return;
			switch (e->get_type()) {
			case node_type::identifier_expr:
				if (on_identifier) on_identifier(static_cast<identifier_expr*>(e.get()));
				return;
			case node_type::binary_expr: {
				auto* b = static_cast<binary_expr*>(e.get());
				expr(b->left);
				expr(b->right);
				return;
			}
			case node_type::unary_expr:
				expr(static_cast<unary_expr*>(e.get())->operand);
				return;
			case node_type::call_expr: {
				auto* c = static_cast<call_expr*>(e.get());
				expr(c->callee);
				for (const auto& a : c->arguments) expr(a);
				return;
			}
			case node_type::member_expr:
				expr(static_cast<member_expr*>(e.get())->object);
				return;
			case node_type::assignment_expr: {
				auto* a = static_cast<assignment_expr*>(e.get());
				expr(a->target);
				expr(a->value);
				return;
			}
			case node_type::ternary_expr: {
				auto* t = static_cast<ternary_expr*>(e.get());
				expr(t->condition);
				expr(t->then_expression);
				expr(t->else_expression);
				return;
			}
			case node_type::array_literal_expr:
				for (const auto& el : static_cast<array_literal_expr*>(e.get())->elements) expr(el);
				return;
			case node_type::map_literal_expr:
				for (const auto& kv : static_cast<map_literal_expr*>(e.get())->entries) {
					expr(kv.first);
					expr(kv.second);
				}
				return;
			case node_type::new_expr:
				for (const auto& a : static_cast<new_expr*>(e.get())->arguments) expr(a);
				return;
			case node_type::throw_expr:
				expr(static_cast<throw_expr*>(e.get())->value);
				return;
			case node_type::yield_expr:
				expr(static_cast<yield_expr*>(e.get())->value);
				return;
			default:
				return;
			}
		}

		void stmt(const statement_ptr& s) const {
			if (!s) return;
			switch (s->get_type()) {
			case node_type::expression_stmt:
				expr(static_cast<expression_stmt*>(s.get())->expression);
				return;
			case node_type::expression_decl:
				expr(static_cast<expression_decl*>(s.get())->expression);
				return;
			case node_type::statement_decl:
				stmt(static_cast<statement_decl*>(s.get())->statement);
				return;
			case node_type::block_stmt:
				for (const auto& d : static_cast<block_stmt*>(s.get())->declarations) stmt(d);
				return;
			case node_type::if_stmt: {
				auto* is = static_cast<if_stmt*>(s.get());
				expr(is->condition);
				stmt(is->then_statement);
				stmt(is->else_statement);
				return;
			}
			case node_type::while_stmt: {
				auto* ws = static_cast<while_stmt*>(s.get());
				expr(ws->condition);
				stmt(ws->body);
				return;
			}
			case node_type::for_stmt: {
				auto* fs = static_cast<for_stmt*>(s.get());
				stmt(fs->initializer);
				expr(fs->condition);
				expr(fs->update);
				stmt(fs->body);
				return;
			}
			case node_type::range_for_stmt: {
				auto* rf = static_cast<range_for_stmt*>(s.get());
				if (on_declared && rf->variable_name_id != UINT64_MAX) on_declared(rf->variable_name_id);
				expr(rf->container);
				stmt(rf->body);
				return;
			}
			case node_type::return_stmt:
				expr(static_cast<return_stmt*>(s.get())->value);
				return;
			case node_type::switch_stmt: {
				auto* sw = static_cast<switch_stmt*>(s.get());
				expr(sw->condition);
				for (const auto& c : sw->cases) {
					expr(c->value);
					for (const auto& st : c->body) stmt(st);
				}
				if (sw->default_case) {
					for (const auto& st : sw->default_case->body) stmt(st);
				}
				return;
			}
			case node_type::try_stmt: {
				auto* tr = static_cast<try_stmt*>(s.get());
				stmt(tr->try_block);
				stmt(tr->catch_block);
				return;
			}
			case node_type::variable_decl: {
				auto* vd = static_cast<variable_decl*>(s.get());
				if (on_declared) {
					uint64_t id = vd->name_id != UINT64_MAX ? vd->name_id : symbolizer->intern(std::string(vd->name));
					on_declared(id);
				}
				expr(vd->initializer);
				return;
			}
			case node_type::destructuring_decl: {
				auto* dd = static_cast<destructuring_decl*>(s.get());
				if (on_declared) {
					for (const auto& [name, id] : dd->names) on_declared(id);
				}
				expr(dd->initializer);
				return;
			}
			default:
				return;
			}
		}
	};

	inline bool is_lvalue_shaped(const expression* e) {
		if (!e) return false;
		if (e->get_type() == node_type::identifier_expr || e->get_type() == node_type::member_expr) return true;
		if (e->get_type() == node_type::binary_expr) {
			return static_cast<const binary_expr*>(e)->op.type == token_type::left_bracket;
		}
		return false;
	}

	inline std::shared_ptr<block_stmt> as_block(const statement_ptr& body, const source_location& loc) {
		if (auto block = std::dynamic_pointer_cast<block_stmt>(body)) {
			return block;
		}
		std::vector<declaration_ptr> stmts;
		if (body) {
			stmts.push_back(std::make_shared<statement_decl>(loc, body));
		}
		return std::make_shared<block_stmt>(loc, std::move(stmts));
	}

	// True when executing the node can define a name directly into the CURRENT scope,
	// so an enclosing block must own a real environment. Blocks whose statements all
	// report false compile without op_scope_push/pop (hot for loop bodies).
	bool declares_in_current_scope(const ast_node* node) {
		if (!node) return false;
		switch (node->get_type()) {
		case node_type::expression_stmt:
		case node_type::expression_decl:
		case node_type::return_stmt:
		case node_type::break_stmt:
		case node_type::continue_stmt:
		case node_type::fallthrough_stmt:
		case node_type::block_stmt:       // owns its own scope
		case node_type::for_stmt:         // compile_for pushes its own scope
		case node_type::range_for_stmt:   // loop variable lives in the iteration scope
			return false;
		case node_type::statement_decl:
			return declares_in_current_scope(static_cast<const statement_decl*>(node)->statement.get());
		case node_type::if_stmt: {
			auto* branch = static_cast<const if_stmt*>(node);
			return declares_in_current_scope(branch->then_statement.get()) ||
			       declares_in_current_scope(branch->else_statement.get());
		}
		case node_type::while_stmt:
			return declares_in_current_scope(static_cast<const while_stmt*>(node)->body.get());
		default:
			return true;   // declarations, include/import, switch/try: keep the scope
		}
	}

	// Lazy-env gate for function-body chunks: false only when EVERY op is on the explicit
	// safe list (fail-closed: anything that can capture the frame env's identity — refs,
	// closures, try records, scope pushes, decls into the env — keeps the eager env).
	bool body_needs_frame_env(const chunk& body) {
		for (const auto& ins : body.code) {
			switch (ins.op) {
			case opcode::op_const:
			case opcode::op_null:
			case opcode::op_true:
			case opcode::op_false:
			case opcode::op_pop:
			case opcode::op_dup:
			case opcode::op_to_bool:
			case opcode::op_load:
			case opcode::op_store:
			case opcode::op_compound_store:
			case opcode::op_incdec:
			case opcode::op_binary:
			case opcode::op_binary_fused:
			case opcode::op_index:
			case opcode::op_index_assign:
			case opcode::op_index_compound:
			case opcode::op_unary:
			case opcode::op_array:
			case opcode::op_map:
			case opcode::op_jump:
			case opcode::op_jump_if_false:
			case opcode::op_jump_if_true:
			case opcode::op_loop_back:
			case opcode::op_call:
			case opcode::op_return:
			case opcode::op_call_method:
			case opcode::op_this:
			case opcode::op_super:
			case opcode::op_from_this:
			case opcode::op_null_guard:
			case opcode::op_throw:
			case opcode::op_case_eq:
			case opcode::op_cfor_prep:
			case opcode::op_cfor_back:
			case opcode::op_cfor_pop:
			case opcode::op_halt:
				break;
			case opcode::op_decl_var: {
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.a].get());
				assert(decl->slot_index != SIZE_MAX && "parser slots every function-body decl");
				if (decl->slot_index == SIZE_MAX) return true;
				break;
			}
			case opcode::op_destructure: {
				for (const auto& name : body.destructure_protos[ins.a].names) {
					if (name.second == SIZE_MAX) return true;
				}
				break;
			}
			default:
				return true;
			}
		}
		return false;
	}

	inline uint32_t compound_kind_for(token_type op) {
		switch (op) {
		case token_type::plus_equal: return compound_plus;
		case token_type::minus_equal: return compound_minus;
		case token_type::star_equal: return compound_star;
		case token_type::slash_equal: return compound_slash;
		case token_type::percent_equal: return compound_percent;
		default: return compound_plus;
		}
	}

} // namespace

size_t vm_compiler::emit(opcode op, uint32_t a, uint32_t b, uint32_t c) {
	chunk_->code.push_back({op, a, b, c});
	chunk_->stmt_nodes.push_back(current_stmt_);
	return chunk_->code.size() - 1;
}

void vm_compiler::patch_jump(size_t at, size_t target) {
	chunk_->code[at].a = static_cast<uint32_t>(target);
}

uint32_t vm_compiler::add_constant(const script_value& v) {
	chunk_->constants.push_back(v);
	return static_cast<uint32_t>(chunk_->constants.size() - 1);
}

uint32_t vm_compiler::add_symbol(uint64_t id) {
	chunk_->symbols.push_back(id);
	return static_cast<uint32_t>(chunk_->symbols.size() - 1);
}

uint32_t vm_compiler::add_message(std::string msg) {
	chunk_->messages.push_back(std::move(msg));
	return static_cast<uint32_t>(chunk_->messages.size() - 1);
}

uint32_t vm_compiler::add_node(std::shared_ptr<ast_node> node) {
	chunk_->nodes.push_back(std::move(node));
	return static_cast<uint32_t>(chunk_->nodes.size() - 1);
}

void vm_compiler::emit_unsupported(const char* construct, const char* stage) {
	std::string msg = std::string("vm backend: ") + construct + " not implemented yet (stage " + stage + ")";
	emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::unsupported_operation),
	     add_message(std::move(msg)), k_invalid_u32);
	// Keep the value-stack shape consistent for expression positions
	emit(opcode::op_null);
}

uint32_t vm_compiler::identifier_slot_operand(identifier_expr* ident) const {
	if (ident->slot_index == SIZE_MAX) return k_invalid_u32;
	if (callable_.active && callable_.declared.find(ident->symbol_id) == callable_.declared.end()) {
		// Slot belongs to an enclosing function's frame (or interpreter-patched state):
		// resolve through the environment instead of a foreign slot.
		return k_invalid_u32;
	}
	if (!callable_.active) return k_invalid_u32;
	return static_cast<uint32_t>(ident->slot_index);
}

bool vm_compiler::symbol_is_env_only(uint64_t symbol_id) const {
	return !callable_.active || callable_.declared.find(symbol_id) == callable_.declared.end();
}

uint32_t vm_compiler::binary_shape(binary_expr* expr) {
	const auto lt = expr->left->get_type();
	const auto rt = expr->right->get_type();
	if (lt == node_type::identifier_expr && rt == node_type::identifier_expr) return binary_shape_ident_ident;
	if (lt == node_type::identifier_expr && rt == node_type::literal_expr) return binary_shape_ident_literal;
	if (lt == node_type::literal_expr && rt == node_type::identifier_expr) return binary_shape_literal_ident;
	return binary_shape_none;
}

std::shared_ptr<chunk> vm_compiler::compile_program(const std::vector<declaration_ptr>& declarations) {
	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	chunk_->pinned_decls = declarations;
	loops_.clear();
	callable_ = {};
	scope_depth_ = 0;
	current_stmt_ = nullptr;

	for (const auto& decl : declarations) {
		compile_declaration(decl, true);
	}
	emit(opcode::op_halt);
	chunk_ = nullptr;
	return result;
}

std::shared_ptr<chunk> vm_compiler::compile_callable(std::string_view name,
                                                     const std::vector<parameter>& parameters,
                                                     const std::shared_ptr<block_stmt>& body,
                                                     size_t local_count) {
	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	chunk_->function_name = std::string(name);
	chunk_->local_count = local_count;
	chunk_->is_function_body = true;
	loops_.clear();
	scope_depth_ = 0;
	current_stmt_ = nullptr;

	callable_ = {};
	callable_.active = true;
	for (const auto& p : parameters) {
		uint64_t id = p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name);
		callable_.declared.insert(id);
	}
	{
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_declared = [this](uint64_t id) { callable_.declared.insert(id); };
		walker.stmt(body);
	}

	// Default arguments compile as standalone expression chunks evaluated at call time
	chunk_->param_default_chunks.resize(parameters.size());
	for (size_t i = 0; i < parameters.size(); ++i) {
		if (parameters[i].default_value) {
			auto saved_chunk = chunk_;
			auto saved_stmt = current_stmt_;
			auto default_chunk = std::make_shared<chunk>();
			chunk_ = default_chunk.get();
			compile_expression(parameters[i].default_value);
			emit(opcode::op_halt);
			chunk_ = saved_chunk;
			current_stmt_ = saved_stmt;
			chunk_->param_default_chunks[i] = std::move(default_chunk);
		}
	}

	for (const auto& decl : body->declarations) {
		compile_declaration(decl, false);
	}
	emit(opcode::op_halt);
	result->needs_frame_env = body_needs_frame_env(*result);
	chunk_ = nullptr;
	return result;
}

std::shared_ptr<chunk> vm_compiler::compile_standalone_expression(const expression_ptr& expr) {
	auto saved_chunk = chunk_;
	auto saved_stmt = current_stmt_;
	auto saved_callable = std::move(callable_);
	auto saved_loops = std::move(loops_);
	auto saved_depth = scope_depth_;

	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	callable_ = {};
	loops_.clear();
	scope_depth_ = 0;
	current_stmt_ = expr.get();
	chunk_->nodes.push_back(expr);
	compile_expression(expr);
	emit(opcode::op_halt);

	chunk_ = saved_chunk;
	current_stmt_ = saved_stmt;
	callable_ = std::move(saved_callable);
	loops_ = std::move(saved_loops);
	scope_depth_ = saved_depth;
	return result;
}

std::shared_ptr<chunk> vm_compiler::compile_standalone_declaration(const declaration_ptr& decl) {
	auto saved_chunk = chunk_;
	auto saved_stmt = current_stmt_;
	auto saved_callable = std::move(callable_);
	auto saved_loops = std::move(loops_);
	auto saved_depth = scope_depth_;

	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	callable_ = {};
	loops_.clear();
	scope_depth_ = 0;
	current_stmt_ = decl.get();
	chunk_->nodes.push_back(decl);
	compile_declaration(decl, false);
	emit(opcode::op_halt);

	chunk_ = saved_chunk;
	current_stmt_ = saved_stmt;
	callable_ = std::move(saved_callable);
	loops_ = std::move(saved_loops);
	scope_depth_ = saved_depth;
	return result;
}

void vm_compiler::compile_declaration(const declaration_ptr& decl, bool top_level) {
	current_stmt_ = decl.get();
	switch (decl->get_type()) {
	case node_type::variable_decl:
		compile_variable_decl(std::static_pointer_cast<variable_decl>(decl));
		return;
	case node_type::function_decl:
		compile_function_decl(std::static_pointer_cast<function_decl>(decl));
		return;
	case node_type::expression_decl: {
		auto* ed = static_cast<expression_decl*>(decl.get());
		const bool implicit = top_level && ed->implicit_return;
		if (!implicit && compile_no_result_expression(ed->expression)) return;
		compile_expression(ed->expression, !implicit);
		emit(implicit ? opcode::op_implicit_return : opcode::op_pop);
		return;
	}
	case node_type::statement_decl:
		compile_statement(static_cast<statement_decl*>(decl.get())->statement);
		return;
	case node_type::destructuring_decl: {
		auto* dd = static_cast<destructuring_decl*>(decl.get());
		compile_expression(dd->initializer);
		destructure_proto proto;
		for (size_t i = 0; i < dd->names.size(); ++i) {
			size_t slot = i < dd->slot_indices.size() ? dd->slot_indices[i] : SIZE_MAX;
			if (slot != SIZE_MAX && symbol_is_env_only(dd->names[i].second)) {
				slot = SIZE_MAX;
			}
			proto.names.emplace_back(dd->names[i].second, slot);
		}
		chunk_->destructure_protos.push_back(std::move(proto));
		emit(opcode::op_destructure, static_cast<uint32_t>(chunk_->destructure_protos.size() - 1));
		return;
	}
	case node_type::class_decl:
		emit(opcode::op_class_decl, add_node(decl));
		return;
	case node_type::namespace_decl:
		emit(opcode::op_namespace_decl, add_node(decl));
		return;
	case node_type::enum_decl:
		emit(opcode::op_enum_decl, add_node(decl));
		return;
	case node_type::include_decl: {
		auto* inc = static_cast<include_decl*>(decl.get());
		uint32_t path_msg = k_invalid_u32;
		uint32_t is_expr = 0;
		if (inc->path_expr) {
			compile_expression(inc->path_expr);
			is_expr = 1;
		} else {
			path_msg = add_message(inc->path);
		}
		emit(opcode::op_include, path_msg, is_expr);
		emit(opcode::op_pop);
		return;
	}
	case node_type::import_decl: {
		auto* imp = static_cast<import_decl*>(decl.get());
		uint32_t path_msg = k_invalid_u32;
		uint32_t is_expr = 0;
		if (imp->path_expr) {
			compile_expression(imp->path_expr);
			is_expr = 1;
		} else {
			path_msg = add_message(imp->path);
		}
		emit(opcode::op_import, path_msg, is_expr);
		emit(opcode::op_pop);
		return;
	}
	default:
		compile_statement(std::static_pointer_cast<statement>(decl));
		return;
	}
}

void vm_compiler::compile_statement(const statement_ptr& stmt) {
	if (!stmt) return;
	current_stmt_ = stmt.get();
	switch (stmt->get_type()) {
	case node_type::expression_stmt: {
		const auto& e = static_cast<expression_stmt*>(stmt.get())->expression;
		if (compile_no_result_expression(e)) return;
		compile_expression(e, true);
		emit(opcode::op_pop);
		return;
	}
	case node_type::block_stmt:
		compile_block(static_cast<block_stmt*>(stmt.get()));
		return;
	case node_type::if_stmt:
		compile_if(static_cast<if_stmt*>(stmt.get()));
		return;
	case node_type::while_stmt:
		compile_while(static_cast<while_stmt*>(stmt.get()));
		return;
	case node_type::for_stmt:
		compile_for(static_cast<for_stmt*>(stmt.get()));
		return;
	case node_type::return_stmt: {
		auto* rs = static_cast<return_stmt*>(stmt.get());
		if (rs->value) {
			compile_expression(rs->value);
		} else {
			emit(opcode::op_null);
		}
		emit_region_exits(SIZE_MAX);
		emit(opcode::op_return, 1);
		return;
	}
	case node_type::break_stmt:
		compile_break();
		return;
	case node_type::continue_stmt:
		compile_continue();
		return;
	case node_type::try_stmt:
		compile_try(static_cast<try_stmt*>(stmt.get()));
		return;
	case node_type::switch_stmt:
		compile_switch(static_cast<switch_stmt*>(stmt.get()));
		return;
	case node_type::range_for_stmt:
		compile_range_for(static_cast<range_for_stmt*>(stmt.get()));
		return;
	case node_type::fallthrough_stmt:
		// Sets the runtime fallthrough flag: the [value, ft] pair of the innermost
		// switch sits on top of the value stack at every statement boundary
		emit(opcode::op_pop);
		emit(opcode::op_true);
		return;
	case node_type::case_stmt:
	case node_type::default_stmt:
		emit_unsupported("switch", "B3");
		emit(opcode::op_pop);
		return;
	default:
		if (auto decl = std::dynamic_pointer_cast<declaration>(stmt)) {
			compile_declaration(decl, false);
			return;
		}
		emit_unsupported("statement", "B2");
		emit(opcode::op_pop);
		return;
	}
}

void vm_compiler::compile_block(block_stmt* block) {
	bool needs_scope = false;
	for (const auto& decl : block->declarations) {
		if (declares_in_current_scope(decl.get())) {
			needs_scope = true;
			break;
		}
	}
	if (!needs_scope) {
		for (const auto& decl : block->declarations) {
			compile_declaration(decl, false);
		}
		return;
	}
	emit(opcode::op_scope_push);
	++scope_depth_;
	for (const auto& decl : block->declarations) {
		compile_declaration(decl, false);
	}
	--scope_depth_;
	emit(opcode::op_scope_pop);
}

void vm_compiler::compile_if(if_stmt* stmt) {
	const uint32_t proved = expression_returns_bool(stmt->condition.get()) ? 1u : 0u;
	compile_expression(stmt->condition);
	size_t jump_false = emit(opcode::op_jump_if_false, k_invalid_u32, proved);
	compile_statement(stmt->then_statement);
	if (stmt->else_statement) {
		size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
		patch_jump(jump_false, chunk_->code.size());
		compile_statement(stmt->else_statement);
		patch_jump(jump_end, chunk_->code.size());
	} else {
		patch_jump(jump_false, chunk_->code.size());
	}
}

void vm_compiler::compile_while(while_stmt* stmt) {
	const uint32_t proved = expression_returns_bool(stmt->condition.get()) ? 1u : 0u;
	const size_t top = chunk_->code.size();
	compile_expression(stmt->condition);
	size_t exit_jump = emit(opcode::op_jump_if_false, k_invalid_u32, proved);

	loops_.push_back({});
	loops_.back().continue_target = top;
	loops_.back().continue_is_loop_back = true;
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);
	emit(opcode::op_loop_back, static_cast<uint32_t>(top));

	const size_t end = chunk_->code.size();
	patch_jump(exit_jump, end);
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, end);
	}
	loops_.pop_back();
}

void vm_compiler::compile_for(for_stmt* stmt) {
	if (compile_counted_for(stmt)) {
		return;
	}

	emit(opcode::op_scope_push);
	++scope_depth_;

	if (stmt->initializer) {
		compile_declaration(stmt->initializer, false);
	}

	const size_t top = chunk_->code.size();
	size_t exit_jump = k_invalid_u32;
	if (stmt->condition) {
		const uint32_t proved = expression_returns_bool(stmt->condition.get()) ? 1u : 0u;
		compile_expression(stmt->condition);
		exit_jump = emit(opcode::op_jump_if_false, k_invalid_u32, proved);
	}

	loops_.push_back({});
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);

	const size_t continue_target = chunk_->code.size();
	for (size_t at : loops_.back().continue_patches) {
		patch_jump(at, continue_target);
	}
	if (stmt->update) {
		if (!compile_no_result_expression(stmt->update)) {
			compile_expression(stmt->update, true);
			emit(opcode::op_pop);
		}
	}
	emit(opcode::op_loop_back, static_cast<uint32_t>(top));

	const size_t end = chunk_->code.size();
	if (exit_jump != k_invalid_u32) {
		patch_jump(exit_jump, end);
	}
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, end);
	}
	loops_.pop_back();

	--scope_depth_;
	emit(opcode::op_scope_pop);
}

// Counting-loop codegen (mirrors the interpreter's visit_for_stmt fast path):
//   for (var/auto/int i = <int literal>; i cmp <int literal|ident>; i ++/--/+=/-= <int literal|ident>)
// Fast iterations run through op_cfor_prep/op_cfor_back with cached int pointers; when
// runtime types don't cooperate the SAME loop runs its generic cond/update bytecode.
bool vm_compiler::compile_counted_for(for_stmt* stmt) {
	if (!stmt->initializer || !stmt->condition || !stmt->update) return false;

	// Initializer: variable_decl with an int-literal initializer
	if (stmt->initializer->get_type() != node_type::variable_decl) return false;
	auto* init_var = static_cast<variable_decl*>(stmt->initializer.get());
	if (!init_var->initializer || init_var->initializer->get_type() != node_type::literal_expr) return false;
	auto* init_lit = static_cast<literal_expr*>(init_var->initializer.get());
	if (init_lit->value.raw_storage_index() != script_value::TYPEID_INT) return false;
	const uint64_t var_id = init_var->name_id;
	if (var_id == UINT64_MAX) return false;

	// Condition: var cmp (int literal | identifier)
	if (stmt->condition->get_type() != node_type::binary_expr) return false;
	auto* cond = static_cast<binary_expr*>(stmt->condition.get());
	switch (cond->op.type) {
	case token_type::less: case token_type::less_equal:
	case token_type::greater: case token_type::greater_equal:
	case token_type::equal_equal: case token_type::bang_equal:
		break;
	default:
		return false;
	}
	if (cond->left->get_type() != node_type::identifier_expr) return false;
	auto* cond_var = static_cast<identifier_expr*>(cond->left.get());
	if (cond_var->symbol_id != var_id) return false;
	if (cond->right->get_type() == node_type::literal_expr) {
		if (static_cast<literal_expr*>(cond->right.get())->value.raw_storage_index() != script_value::TYPEID_INT) return false;
	} else if (cond->right->get_type() != node_type::identifier_expr) {
		return false;
	}

	// Update: ++var / --var / var += x / var -= x  (x = int literal | identifier)
	bool subtract = false;
	const expression* step_expr = nullptr;   // null = literal 1
	if (stmt->update->get_type() == node_type::unary_expr) {
		auto* un = static_cast<unary_expr*>(stmt->update.get());
		if (un->op.type != token_type::plus_plus && un->op.type != token_type::minus_minus) return false;
		if (un->operand->get_type() != node_type::identifier_expr) return false;
		if (static_cast<identifier_expr*>(un->operand.get())->symbol_id != var_id) return false;
		subtract = un->op.type == token_type::minus_minus;
	} else if (stmt->update->get_type() == node_type::assignment_expr) {
		auto* assign = static_cast<assignment_expr*>(stmt->update.get());
		if (assign->op.type != token_type::plus_equal && assign->op.type != token_type::minus_equal) return false;
		if (assign->target->get_type() != node_type::identifier_expr) return false;
		if (static_cast<identifier_expr*>(assign->target.get())->symbol_id != var_id) return false;
		subtract = assign->op.type == token_type::minus_equal;
		if (assign->value->get_type() == node_type::literal_expr) {
			if (static_cast<literal_expr*>(assign->value.get())->value.raw_storage_index() != script_value::TYPEID_INT) return false;
		} else if (assign->value->get_type() != node_type::identifier_expr) {
			return false;
		}
		step_expr = assign->value.get();
	} else {
		return false;
	}

	// === Pattern matched: emit the counted form ===
	emit(opcode::op_scope_push);
	++scope_depth_;

	compile_declaration(stmt->initializer, false);

	counted_for_proto proto;
	proto.var = make_fused_operand(cond->left.get());
	proto.end = make_fused_operand(cond->right.get());
	if (step_expr) {
		proto.step = make_fused_operand(step_expr);
	} else {
		proto.step.const_index = add_constant(script_value(static_cast<script_int>(1), static_cast<engine*>(nullptr)));
	}
	proto.cmp = static_cast<uint8_t>(cond->op.type);
	proto.step_subtract = subtract;
	chunk_->counted_for_protos.push_back(proto);
	const uint32_t proto_idx = static_cast<uint32_t>(chunk_->counted_for_protos.size() - 1);

	emit(opcode::op_cfor_prep, proto_idx);

	const size_t generic_cond = chunk_->code.size();
	{
		const uint32_t proved = expression_returns_bool(stmt->condition.get()) ? 1u : 0u;
		compile_expression(stmt->condition);
		emit(opcode::op_jump_if_false, k_invalid_u32, proved);   // patched to exit_pop below
	}
	const size_t generic_exit_jump = chunk_->code.size() - 1;

	const size_t body_start = chunk_->code.size();

	loops_.push_back({});
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);

	const size_t continue_target = chunk_->code.size();
	for (size_t at : loops_.back().continue_patches) {
		patch_jump(at, continue_target);
	}
	emit(opcode::op_cfor_back, proto_idx);

	const size_t generic_update = chunk_->code.size();
	if (!compile_no_result_expression(stmt->update)) {
		compile_expression(stmt->update, true);
		emit(opcode::op_pop);
	}
	emit(opcode::op_loop_back, static_cast<uint32_t>(generic_cond));

	const size_t exit_pop = chunk_->code.size();
	emit(opcode::op_cfor_pop);
	const size_t end = chunk_->code.size();

	patch_jump(generic_exit_jump, exit_pop);
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, exit_pop);
	}
	loops_.pop_back();

	auto& stored = chunk_->counted_for_protos[proto_idx];
	stored.body_ip = static_cast<uint32_t>(body_start);
	stored.exit_ip = static_cast<uint32_t>(end);
	stored.generic_cond_ip = static_cast<uint32_t>(generic_cond);
	stored.generic_update_ip = static_cast<uint32_t>(generic_update);

	--scope_depth_;
	emit(opcode::op_scope_pop);
	return true;
}

void vm_compiler::emit_region_exits(size_t target_region) {
	for (size_t idx = loops_.size(); idx-- > 0;) {
		if (target_region != SIZE_MAX && idx <= target_region) break;
		switch (loops_[idx].kind) {
		case region_kind::try_block:
			emit(opcode::op_try_pop);
			break;
		case region_kind::catch_block:
			emit(opcode::op_catch_end);
			break;
		case region_kind::switch_ctx:
			// Discard the switch's [value, ft] stack pair only for jumps that stay in
			// the frame; return relies on frame teardown truncating the value stack
			if (target_region != SIZE_MAX) {
				emit(opcode::op_pop);
				emit(opcode::op_pop);
			}
			break;
		default:
			break;
		}
	}
}

void vm_compiler::compile_break() {
	size_t target = SIZE_MAX;
	for (size_t idx = loops_.size(); idx-- > 0;) {
		const auto k = loops_[idx].kind;
		if (k == region_kind::loop || k == region_kind::range_for || k == region_kind::switch_ctx) {
			target = idx;
			break;
		}
	}
	if (target == SIZE_MAX) {
		emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::break_outside_loop), k_invalid_u32, k_invalid_u32);
		return;
	}
	emit_region_exits(target);
	auto& region = loops_[target];
	if (scope_depth_ > region.scope_depth) {
		emit(opcode::op_scope_pop_n, static_cast<uint32_t>(scope_depth_ - region.scope_depth));
	}
	if (region.kind == region_kind::switch_ctx) {
		emit(opcode::op_pop);
		emit(opcode::op_pop);
	}
	region.break_patches.push_back(emit(opcode::op_jump, k_invalid_u32));
}

void vm_compiler::compile_continue() {
	size_t target = SIZE_MAX;
	for (size_t idx = loops_.size(); idx-- > 0;) {
		const auto k = loops_[idx].kind;
		if (k == region_kind::loop || k == region_kind::range_for) {
			target = idx;
			break;
		}
	}
	if (target == SIZE_MAX) {
		emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::continue_outside_loop), k_invalid_u32, k_invalid_u32);
		return;
	}
	emit_region_exits(target);
	auto& region = loops_[target];
	if (scope_depth_ > region.scope_depth) {
		emit(opcode::op_scope_pop_n, static_cast<uint32_t>(scope_depth_ - region.scope_depth));
	}
	if (region.kind == region_kind::range_for) {
		emit(opcode::op_jump, static_cast<uint32_t>(region.continue_target));
	} else if (region.continue_is_loop_back) {
		emit(opcode::op_loop_back, static_cast<uint32_t>(region.continue_target));
	} else {
		region.continue_patches.push_back(emit(opcode::op_jump, k_invalid_u32));
	}
}

void vm_compiler::compile_range_for(range_for_stmt* stmt) {
	compile_expression(stmt->container);

	iter_proto proto;
	proto.var_symbol = stmt->variable_name_id != UINT64_MAX
		? stmt->variable_name_id
		: symbolizer_->intern(std::string(stmt->variable_name));
	if (stmt->variable_slot_index != SIZE_MAX && callable_.active &&
	    callable_.declared.find(proto.var_symbol) != callable_.declared.end()) {
		proto.slot = stmt->variable_slot_index;
	}
	proto.is_reference = stmt->is_reference;
	chunk_->iter_protos.push_back(proto);
	const uint32_t proto_idx = static_cast<uint32_t>(chunk_->iter_protos.size() - 1);

	emit(opcode::op_iter_init, proto_idx);
	++scope_depth_;                               // loop-variable scope pushed by op_iter_init

	const size_t loop_top = chunk_->code.size();
	emit(opcode::op_iter_next, proto_idx);        // budget check lives here (one per iteration)
	size_t exit_jump = emit(opcode::op_jump_if_false, k_invalid_u32, 1);

	loops_.push_back({});
	loops_.back().kind = region_kind::range_for;
	loops_.back().continue_target = loop_top;
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);
	emit(opcode::op_jump, static_cast<uint32_t>(loop_top));

	const size_t exit_ip = chunk_->code.size();
	patch_jump(exit_jump, exit_ip);
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, exit_ip);
	}
	loops_.pop_back();

	emit(opcode::op_iter_pop);
	--scope_depth_;
}

void vm_compiler::compile_try(try_stmt* stmt) {
	const uint32_t catch_sym = add_symbol(symbolizer_->intern(stmt->catch_var));
	size_t try_push = emit(opcode::op_try_push, k_invalid_u32, catch_sym);

	loops_.push_back({});
	loops_.back().kind = region_kind::try_block;
	loops_.back().scope_depth = scope_depth_;
	compile_statement(stmt->try_block);
	loops_.pop_back();

	emit(opcode::op_try_pop);
	size_t jump_end = emit(opcode::op_jump, k_invalid_u32);

	patch_jump(try_push, chunk_->code.size());
	loops_.push_back({});
	loops_.back().kind = region_kind::catch_block;
	loops_.back().scope_depth = scope_depth_;
	compile_statement(stmt->catch_block);
	loops_.pop_back();

	emit(opcode::op_catch_end);
	patch_jump(jump_end, chunk_->code.size());
}

void vm_compiler::compile_switch(switch_stmt* stmt) {
	compile_expression(stmt->condition);

	loops_.push_back({});
	loops_.back().kind = region_kind::switch_ctx;
	loops_.back().scope_depth = scope_depth_;
	const size_t region_index = loops_.size() - 1;

	// Mirrors the interpreter's case loop: value evaluated per case (side effects
	// included on the fallthrough entry path), == failure counts as a non-match,
	// break-by-default, runtime fallthrough flag, default checked after all cases
	std::vector<size_t> end_jumps;
	size_t prev_test_fail = SIZE_MAX;
	size_t prev_ft_jump = SIZE_MAX;

	for (size_t i = 0; i < stmt->cases.size(); ++i) {
		const auto& case_node = stmt->cases[i];
		current_stmt_ = case_node.get();

		size_t ft_to_body = SIZE_MAX;
		if (prev_ft_jump != SIZE_MAX) {
			patch_jump(prev_ft_jump, chunk_->code.size());
			compile_expression(case_node->value);
			emit(opcode::op_pop);
			emit(opcode::op_false);
			ft_to_body = emit(opcode::op_jump, k_invalid_u32);
			prev_ft_jump = SIZE_MAX;
		}

		if (prev_test_fail != SIZE_MAX) {
			patch_jump(prev_test_fail, chunk_->code.size());
		}
		emit(opcode::op_dup);
		compile_expression(case_node->value);
		emit(opcode::op_case_eq);
		prev_test_fail = emit(opcode::op_jump_if_false, k_invalid_u32, 1);
		emit(opcode::op_false);

		if (ft_to_body != SIZE_MAX) {
			patch_jump(ft_to_body, chunk_->code.size());
		}
		emit(opcode::op_scope_push);
		++scope_depth_;
		for (const auto& body_stmt : case_node->body) {
			compile_statement(body_stmt);
		}
		--scope_depth_;
		emit(opcode::op_scope_pop);

		current_stmt_ = case_node.get();
		prev_ft_jump = emit(opcode::op_jump_if_true, k_invalid_u32, 1);
		emit(opcode::op_pop);
		end_jumps.push_back(emit(opcode::op_jump, k_invalid_u32));
	}

	current_stmt_ = stmt;
	if (stmt->default_case) {
		const size_t default_entry = chunk_->code.size();
		if (prev_test_fail != SIZE_MAX) patch_jump(prev_test_fail, default_entry);
		if (prev_ft_jump != SIZE_MAX) patch_jump(prev_ft_jump, default_entry);
		emit(opcode::op_false);
		emit(opcode::op_scope_push);
		++scope_depth_;
		for (const auto& body_stmt : stmt->default_case->body) {
			compile_statement(body_stmt);
		}
		--scope_depth_;
		emit(opcode::op_scope_pop);
		current_stmt_ = stmt;
		emit(opcode::op_pop);
		emit(opcode::op_pop);
	} else {
		const size_t exit_pop = chunk_->code.size();
		if (prev_test_fail != SIZE_MAX) patch_jump(prev_test_fail, exit_pop);
		if (prev_ft_jump != SIZE_MAX) patch_jump(prev_ft_jump, exit_pop);
		emit(opcode::op_pop);
	}

	const size_t end = chunk_->code.size();
	for (size_t at : end_jumps) {
		patch_jump(at, end);
	}
	for (size_t at : loops_[region_index].break_patches) {
		patch_jump(at, end);
	}
	loops_.pop_back();
}

bool vm_compiler::compile_no_result_expression(const expression_ptr& expr) {
	if (expr->get_type() == node_type::assignment_expr) {
		auto* assign = static_cast<assignment_expr*>(expr.get());
		if (assign->target->get_type() != node_type::identifier_expr) return false;
		auto* ident = static_cast<identifier_expr*>(assign->target.get());
		compile_expression(assign->value);
		if (assign->op.type == token_type::equal) {
			uint32_t flags = store_flag_no_result;
			if (is_lvalue_shaped(assign->value.get())) flags |= store_flag_rhs_lvalue;
			emit(opcode::op_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
		} else {
			emit(opcode::op_compound_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident),
			     compound_kind_for(assign->op.type) | compound_flag_no_result);
		}
		return true;
	}
	if (expr->get_type() == node_type::unary_expr) {
		auto* un = static_cast<unary_expr*>(expr.get());
		if ((un->op.type == token_type::plus_plus || un->op.type == token_type::minus_minus) &&
		    un->operand->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(un->operand.get());
			uint32_t flags = incdec_flag_no_result;
			if (un->is_postfix) flags |= incdec_flag_postfix;
			if (un->op.type == token_type::plus_plus) flags |= incdec_flag_increment;
			emit(opcode::op_incdec, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
			return true;
		}
	}
	return false;
}

void vm_compiler::compile_expression(const expression_ptr& expr, bool as_statement) {
	switch (expr->get_type()) {
	case node_type::literal_expr:
		emit(opcode::op_const, add_constant(static_cast<literal_expr*>(expr.get())->value));
		return;
	case node_type::identifier_expr:
		compile_identifier_load(static_cast<identifier_expr*>(expr.get()));
		return;
	case node_type::binary_expr:
		compile_binary(std::static_pointer_cast<binary_expr>(expr));
		return;
	case node_type::unary_expr:
		compile_unary(std::static_pointer_cast<unary_expr>(expr));
		return;
	case node_type::assignment_expr:
		compile_assignment(std::static_pointer_cast<assignment_expr>(expr), as_statement);
		return;
	case node_type::call_expr:
		compile_call(std::static_pointer_cast<call_expr>(expr));
		return;
	case node_type::lambda_expr:
		compile_lambda(std::static_pointer_cast<lambda_expr>(expr));
		return;
	case node_type::ternary_expr: {
		auto* t = static_cast<ternary_expr*>(expr.get());
		const uint32_t proved = expression_returns_bool(t->condition.get()) ? 1u : 0u;
		compile_expression(t->condition);
		size_t jump_false = emit(opcode::op_jump_if_false, k_invalid_u32, proved);
		compile_expression(t->then_expression);
		size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
		patch_jump(jump_false, chunk_->code.size());
		compile_expression(t->else_expression);
		patch_jump(jump_end, chunk_->code.size());
		return;
	}
	case node_type::array_literal_expr: {
		auto* arr = static_cast<array_literal_expr*>(expr.get());
		for (const auto& el : arr->elements) {
			compile_expression(el);
		}
		emit(opcode::op_array, static_cast<uint32_t>(arr->elements.size()));
		return;
	}
	case node_type::map_literal_expr: {
		auto* m = static_cast<map_literal_expr*>(expr.get());
		for (const auto& kv : m->entries) {
			compile_expression(kv.first);
			compile_expression(kv.second);
		}
		emit(opcode::op_map, static_cast<uint32_t>(m->entries.size()));
		return;
	}
	case node_type::member_expr:
		compile_member(std::static_pointer_cast<member_expr>(expr));
		return;
	case node_type::new_expr:
		compile_new(std::static_pointer_cast<new_expr>(expr));
		return;
	case node_type::this_expr:
		emit(opcode::op_this);
		return;
	case node_type::super_expr:
		emit(opcode::op_super);
		return;
	case node_type::throw_expr: {
		auto* te = static_cast<throw_expr*>(expr.get());
		uint32_t has_value = 0;
		if (te->value) {
			compile_expression(te->value);
			has_value = 1;
		}
		emit(opcode::op_throw, has_value, add_node(expr));
		return;
	}
	case node_type::yield_expr: {
		auto* ye = static_cast<yield_expr*>(expr.get());
		uint32_t has_value = 0;
		if (ye->value) {
			compile_expression(ye->value);
			has_value = 1;
		}
		emit(opcode::op_yield, has_value);
		return;
	}
	default:
		emit_unsupported("expression", "B2");
		return;
	}
}

void vm_compiler::compile_identifier_load(identifier_expr* ident) {
	uint32_t flags = 0;
	if (!ident->name.empty() && (ident->name.front() == 'w' || ident->name.front() == 's') &&
	    (ident->name.find("weak_ptr<") == 0 || ident->name.find("shared_ptr<") == 0)) {
		flags |= load_flag_type_ctor;
	}
	emit(opcode::op_load, identifier_slot_operand(ident), add_symbol(ident->symbol_id), flags);
}

void vm_compiler::compile_binary(const std::shared_ptr<binary_expr>& expr) {
	if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
		compile_expression(expr->left);
		if (expr->op.type == token_type::ampersand_ampersand) {
			size_t jump_false = emit(opcode::op_jump_if_false, k_invalid_u32, 0);
			compile_expression(expr->right);
			emit(opcode::op_to_bool);
			size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
			patch_jump(jump_false, chunk_->code.size());
			emit(opcode::op_false);
			patch_jump(jump_end, chunk_->code.size());
		} else {
			size_t jump_true = emit(opcode::op_jump_if_true, k_invalid_u32, 0);
			compile_expression(expr->right);
			emit(opcode::op_to_bool);
			size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
			patch_jump(jump_true, chunk_->code.size());
			emit(opcode::op_true);
			patch_jump(jump_end, chunk_->code.size());
		}
		return;
	}

	if (expr->op.type == token_type::left_bracket) {
		compile_expression(expr->left);
		compile_expression(expr->right);
		uint32_t flags = is_lvalue_shaped(expr->left.get()) ? index_flag_lvalue_shape : 0;
		emit(opcode::op_index, flags);
		return;
	}

	const uint32_t shape = binary_shape(expr.get());
	if (shape != binary_shape_none) {
		fused_binary_proto proto;
		proto.op = static_cast<uint8_t>(expr->op.type);
		proto.left = make_fused_operand(expr->left.get());
		proto.right = make_fused_operand(expr->right.get());
		chunk_->fused_binary_protos.push_back(proto);
		emit(opcode::op_binary_fused, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), shape);
		return;
	}

	compile_expression(expr->left);
	compile_expression(expr->right);
	emit(opcode::op_binary, static_cast<uint32_t>(expr->op.type), binary_shape_none);
}

fused_operand vm_compiler::make_fused_operand(const expression* e) {
	fused_operand out;
	if (e->get_type() == node_type::identifier_expr) {
		auto* ident = const_cast<identifier_expr*>(static_cast<const identifier_expr*>(e));
		out.slot = identifier_slot_operand(ident);
		out.symbol = add_symbol(ident->symbol_id);
		if (!ident->name.empty() && (ident->name.front() == 'w' || ident->name.front() == 's') &&
		    (ident->name.find("weak_ptr<") == 0 || ident->name.find("shared_ptr<") == 0)) {
			out.load_flags |= load_flag_type_ctor;
		}
	} else {
		out.const_index = add_constant(static_cast<const literal_expr*>(e)->value);
	}
	return out;
}

void vm_compiler::compile_unary(const std::shared_ptr<unary_expr>& expr) {
	if (expr->op.type == token_type::plus_plus || expr->op.type == token_type::minus_minus) {
		if (expr->operand->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(expr->operand.get());
			uint32_t flags = 0;
			if (expr->is_postfix) flags |= incdec_flag_postfix;
			if (expr->op.type == token_type::plus_plus) flags |= incdec_flag_increment;
			emit(opcode::op_incdec, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
		} else {
			compile_expression(expr->operand);
			emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::invalid_assignment_target), k_invalid_u32, k_invalid_u32);
		}
		return;
	}
	compile_expression(expr->operand);
	emit(opcode::op_unary, static_cast<uint32_t>(expr->op.type));
}

void vm_compiler::compile_assignment(const std::shared_ptr<assignment_expr>& expr, bool as_statement) {
	const bool compound = expr->op.type != token_type::equal;
	if (expr->target->get_type() == node_type::identifier_expr) {
		auto* ident = static_cast<identifier_expr*>(expr->target.get());
		compile_expression(expr->value);
		if (!compound) {
			uint32_t flags = is_lvalue_shaped(expr->value.get()) ? store_flag_rhs_lvalue : 0;
			emit(opcode::op_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
		} else {
			uint32_t kind;
			switch (expr->op.type) {
			case token_type::plus_equal: kind = compound_plus; break;
			case token_type::minus_equal: kind = compound_minus; break;
			case token_type::star_equal: kind = compound_star; break;
			case token_type::slash_equal: kind = compound_slash; break;
			case token_type::percent_equal: kind = compound_percent; break;
			default: kind = compound_plus; break;
			}
			if (!as_statement) kind |= compound_flag_result_needed;
			emit(opcode::op_compound_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident), kind);
		}
		return;
	}

	if (expr->target->get_type() == node_type::binary_expr &&
	    static_cast<binary_expr*>(expr->target.get())->op.type == token_type::left_bracket) {
		auto* sub = static_cast<binary_expr*>(expr->target.get());
		if (!compound) {
			compile_expression(expr->value);
			compile_expression(sub->left);
			compile_expression(sub->right);
			uint32_t flags = index_flag_lvalue_write;
			if (is_lvalue_shaped(sub->left.get())) flags |= index_flag_lvalue_shape;
			emit(opcode::op_index, flags);
			emit(opcode::op_index_assign);
		} else {
			compile_expression(sub->left);
			compile_expression(sub->right);
			uint32_t flags = is_lvalue_shaped(sub->left.get()) ? index_flag_lvalue_shape : 0;
			emit(opcode::op_index, flags);
			compile_expression(expr->value);
			uint32_t kind;
			switch (expr->op.type) {
			case token_type::plus_equal: kind = compound_plus; break;
			case token_type::minus_equal: kind = compound_minus; break;
			case token_type::star_equal: kind = compound_star; break;
			case token_type::slash_equal: kind = compound_slash; break;
			case token_type::percent_equal: kind = compound_percent; break;
			default: kind = compound_plus; break;
			}
			emit(opcode::op_index_compound, kind);
		}
		return;
	}

	if (expr->target->get_type() == node_type::member_expr) {
		auto member = std::static_pointer_cast<member_expr>(expr->target);
		if (!compound) {
			compile_expression(expr->value);
			if (member->is_static) {
				emit(opcode::op_set_static, add_node(member));
			} else {
				compile_expression(member->object);
				emit(opcode::op_set_member, add_node(member));
			}
		} else {
			if (member->is_static) {
				emit(opcode::op_get_static, add_node(member));
			} else {
				compile_expression(member->object);
				emit(opcode::op_get_member, add_node(member));
			}
			compile_expression(expr->value);
			// Interpreter re-evaluates the object expression for the write-back
			compile_expression(member->object);
			uint32_t kind;
			switch (expr->op.type) {
			case token_type::plus_equal: kind = compound_plus; break;
			case token_type::minus_equal: kind = compound_minus; break;
			case token_type::star_equal: kind = compound_star; break;
			case token_type::slash_equal: kind = compound_slash; break;
			case token_type::percent_equal: kind = compound_percent; break;
			default: kind = compound_plus; break;
			}
			emit(opcode::op_member_compound, add_node(member), 0, kind);
		}
		return;
	}

	emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::unsupported_operation),
	     add_message("Complex assignment targets not yet implemented"), k_invalid_u32);
	emit(opcode::op_null);
}

void vm_compiler::compile_call(const std::shared_ptr<call_expr>& expr) {
	if (expr->callee->get_type() == node_type::identifier_expr) {
		auto* ident = static_cast<identifier_expr*>(expr->callee.get());
		if (ident->name == "weak_from_this" || ident->name == "shared_from_this") {
			if (!expr->arguments.empty()) {
				emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::argument_count_mismatch),
				     add_message("{0}() takes no arguments"), add_symbol(ident->symbol_id));
				emit(opcode::op_null);
				return;
			}
			emit(opcode::op_from_this, ident->name == "weak_from_this" ? 1u : 0u, add_symbol(ident->symbol_id));
			return;
		}
	}
	if (expr->callee->get_type() == node_type::member_expr) {
		auto member = std::static_pointer_cast<member_expr>(expr->callee);
		if (!member->is_static) {
			compile_expression(member->object);
			size_t null_guard = SIZE_MAX;
			if (member->null_safe) {
				null_guard = emit(opcode::op_null_guard, k_invalid_u32);
			}
			call_site site;
			site.member_node = add_node(member);
			if (member->object->get_type() == node_type::identifier_expr) {
				site.receiver_symbol = static_cast<identifier_expr*>(member->object.get())->symbol_id;
			}
			site.arg_symbols.reserve(expr->arguments.size());
			site.arg_slots.reserve(expr->arguments.size());
			for (const auto& arg : expr->arguments) {
				if (arg->get_type() == node_type::identifier_expr) {
					auto* arg_ident = static_cast<identifier_expr*>(arg.get());
					site.arg_symbols.push_back(arg_ident->symbol_id);
					site.arg_slots.push_back(identifier_slot_operand(arg_ident));
				} else {
					site.arg_symbols.push_back(UINT64_MAX);
					site.arg_slots.push_back(k_invalid_u32);
				}
				compile_expression(arg);
			}
			chunk_->call_sites.push_back(std::move(site));
			emit(opcode::op_call_method, static_cast<uint32_t>(expr->arguments.size()),
			     static_cast<uint32_t>(chunk_->call_sites.size() - 1));
			if (null_guard != SIZE_MAX) {
				patch_jump(null_guard, chunk_->code.size());
			}
			return;
		}
	}

	compile_expression(expr->callee);
	call_site site;
	site.arg_symbols.reserve(expr->arguments.size());
	site.arg_slots.reserve(expr->arguments.size());
	for (const auto& arg : expr->arguments) {
		if (arg->get_type() == node_type::identifier_expr) {
			auto* arg_ident = static_cast<identifier_expr*>(arg.get());
			site.arg_symbols.push_back(arg_ident->symbol_id);
			site.arg_slots.push_back(identifier_slot_operand(arg_ident));
		} else {
			site.arg_symbols.push_back(UINT64_MAX);
			site.arg_slots.push_back(k_invalid_u32);
		}
		compile_expression(arg);
	}
	chunk_->call_sites.push_back(std::move(site));
	emit(opcode::op_call, static_cast<uint32_t>(expr->arguments.size()),
	     static_cast<uint32_t>(chunk_->call_sites.size() - 1));
}

void vm_compiler::compile_member(const std::shared_ptr<member_expr>& expr) {
	if (expr->is_static) {
		emit(opcode::op_get_static, add_node(expr));
		return;
	}
	compile_expression(expr->object);
	emit(opcode::op_get_member, add_node(expr));
}

void vm_compiler::compile_new(const std::shared_ptr<new_expr>& expr) {
	const auto base = expr->type ? expr->type->base_type : script_value_type::jai_invalid_type;
	const bool container = base == script_value_type::jai_array_type || base == script_value_type::jai_map_type;
	if ((container && !expr->arguments.empty()) ||
	    (base == script_value_type::jai_weak_ptr_type && expr->arguments.size() > 1)) {
		// Interpreter rejects these argument counts before evaluating any argument
		emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::argument_count_mismatch), k_invalid_u32, k_invalid_u32);
		emit(opcode::op_null);
		return;
	}
	for (const auto& arg : expr->arguments) {
		compile_expression(arg);
	}
	emit(opcode::op_new, add_node(expr), static_cast<uint32_t>(expr->arguments.size()));
}

void vm_compiler::compile_lambda(const std::shared_ptr<lambda_expr>& expr) {
	closure_proto proto;
	proto.lambda = expr;
	proto.body = as_block(expr->body, expr->location);
	proto.default_capture = static_cast<uint8_t>(expr->default_capture);

	uint64_t this_id = symbolizer_->get_this_id();
	for (const auto& cap : expr->captures) {
		uint64_t id = cap.symbol_id != UINT64_MAX ? cap.symbol_id : symbolizer_->intern(cap.name);
		proto.captures.push_back({id, cap.by_reference});
		if (id == this_id) proto.captures_this = true;
	}

	std::unordered_set<uint64_t> declared;
	for (const auto& p : expr->parameters) {
		uint64_t id = p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name);
		declared.insert(id);
	}
	{
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_declared = [&declared](uint64_t id) { declared.insert(id); };
		walker.stmt(proto.body);
	}

	{
		std::unordered_set<uint64_t> seen;
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_identifier = [&](identifier_expr* ident) {
			if (ident->slot_index == SIZE_MAX) return;
			if (declared.count(ident->symbol_id)) return;
			if (seen.insert(ident->symbol_id).second) {
				proto.outer_slot_plan.emplace_back(ident->symbol_id, ident->slot_index);
			}
		};
		walker.stmt(proto.body);
	}

	if (expr->default_capture != lambda_expr::capture_default::none) {
		std::unordered_set<uint64_t> param_ids;
		for (const auto& p : expr->parameters) {
			param_ids.insert(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name));
		}
		std::unordered_set<uint64_t> seen;
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_identifier = [&](identifier_expr* ident) {
			if (param_ids.count(ident->symbol_id)) return;
			if (seen.insert(ident->symbol_id).second) {
				proto.used_variables.push_back(ident->symbol_id);
			}
		};
		walker.stmt(proto.body);
	}

	{
		vm_compiler body_compiler(symbolizer_);
		proto.body_chunk = body_compiler.compile_callable("<lambda>", expr->parameters, proto.body, expr->local_count);
	}

	chunk_->closure_protos.push_back(std::move(proto));
	emit(opcode::op_closure, static_cast<uint32_t>(chunk_->closure_protos.size() - 1));
}

void vm_compiler::compile_variable_decl(const std::shared_ptr<variable_decl>& decl) {
	const bool is_reference = decl->type && decl->type->base_type == script_value_type::jai_reference_type;

	// Locals declared through env fallback in lambda contexts keep working because the
	// slot decision below mirrors identifier loads (declared symbols keep parser slots).
	if (is_reference) {
		if (!decl->initializer) {
			emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::uninitialized_reference),
			     add_message("Reference variable must be initialized"), add_symbol(decl->name_id));
			return;
		}
		if (decl->initializer->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(decl->initializer.get());
			emit(opcode::op_decl_ref_ident, add_node(decl), add_symbol(ident->symbol_id));
		} else {
			compile_expression(decl->initializer);
			emit(opcode::op_decl_ref_value, add_node(decl));
		}
		return;
	}

	uint32_t has_init = 0;
	uint32_t lvalue_init = 0;
	if (decl->initializer) {
		compile_expression(decl->initializer);
		has_init = 1;
		lvalue_init = is_lvalue_shaped(decl->initializer.get()) ? 1u : 0u;
	}
	emit(opcode::op_decl_var, add_node(decl), has_init, lvalue_init);
}

void vm_compiler::compile_function_decl(const std::shared_ptr<function_decl>& decl) {
	function_proto proto;
	proto.decl = decl;
	proto.is_coroutine = decl->is_coroutine;
	if (!decl->is_coroutine) {
		proto.fn = std::make_shared<script_defined_function>(
			decl->name, decl->parameters, decl->return_type, decl->body, nullptr, decl->local_count);
	}
	chunk_->function_protos.push_back(std::move(proto));
	emit(opcode::op_func_decl, static_cast<uint32_t>(chunk_->function_protos.size() - 1));
}

void vm_compiler::collect_declared_symbols(const statement_ptr& stmt, std::unordered_set<uint64_t>& out) const {
	body_walker walker;
	walker.symbolizer = symbolizer_;
	walker.on_declared = [&out](uint64_t id) { out.insert(id); };
	walker.stmt(stmt);
}

void vm_compiler::collect_used_identifiers(const statement_ptr& stmt, const lambda_expr* lambda,
                                           std::vector<uint64_t>& out, std::unordered_set<uint64_t>& seen) const {
	std::unordered_set<uint64_t> param_ids;
	for (const auto& p : lambda->parameters) {
		param_ids.insert(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name));
	}
	body_walker walker;
	walker.symbolizer = symbolizer_;
	walker.on_identifier = [&](identifier_expr* ident) {
		if (param_ids.count(ident->symbol_id)) return;
		if (seen.insert(ident->symbol_id).second) out.push_back(ident->symbol_id);
	};
	walker.stmt(stmt);
}

void vm_compiler::collect_outer_slot_candidates(const statement_ptr& stmt,
                                                const std::unordered_set<uint64_t>& declared,
                                                std::vector<std::pair<uint64_t, size_t>>& out,
                                                std::unordered_set<uint64_t>& seen) const {
	body_walker walker;
	walker.symbolizer = symbolizer_;
	walker.on_identifier = [&](identifier_expr* ident) {
		if (ident->slot_index == SIZE_MAX) return;
		if (declared.count(ident->symbol_id)) return;
		if (seen.insert(ident->symbol_id).second) out.emplace_back(ident->symbol_id, ident->slot_index);
	};
	walker.stmt(stmt);
}

} // namespace jai::vm
