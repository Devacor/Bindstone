#pragma once

#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/return_conv.hpp>
#include <optional>

namespace jai::detail {

	// Completion paths, independent of optimizer reachability. Break/continue belong to their enclosing construct.
	class ReturnFlow {
	public:
		enum : uint8_t { NORMAL = 1, RETURN = 2, THROW = 4, BREAK = 8, CONTINUE = 16, FALLTHROUGH = 32, BARE_RETURN = 64, MAYBE_NORMAL = 128 };
		struct Result {
			uint8_t paths{NORMAL};
			bool mayThrow{false};
		};

		explicit ReturnFlow(const type_info* a_returnType = nullptr) : returnType(a_returnType) {}

		template <typename Statements>
		Result sequence(const Statements& a_statements) const {
			Result result;
			for (const auto& statement : a_statements) {
				if (!(result.paths & (NORMAL | MAYBE_NORMAL))) { break; }
				auto next = analyze(statement.get());
				const bool definite = (result.paths & NORMAL) != 0;
				result.paths = static_cast<uint8_t>((result.paths & ~(NORMAL | MAYBE_NORMAL)) |
					(definite ? next.paths : uncertainNormal(next.paths)));
				result.mayThrow = result.mayThrow || next.mayThrow;
			}
			return result;
		}

		Result analyze(const statement* a_statement) const {
			if (!a_statement) { return {}; }
			switch (a_statement->get_type()) {
				case node_type::return_stmt: {
					const auto* value = static_cast<const return_stmt*>(a_statement);
					if (!value->value) {
						return {static_cast<uint8_t>(RETURN | BARE_RETURN), requires_return_value(returnType)};
					}
					bool safeLiteral = false;
					if (!value->binds_reference && value->value->get_type() == node_type::literal_expr) {
						const auto& literal = static_cast<const literal_expr*>(value->value.get())->value;
						const auto kind = literal.current_type();
						safeLiteral = (kind == script_value_type::jai_int_type || kind == script_value_type::jai_float_type ||
							kind == script_value_type::jai_bool_type || kind == script_value_type::jai_char_type) &&
							(!returnType || returnType->base_type == kind);
					}
					return {RETURN, !safeLiteral};
				}
				case node_type::break_stmt: return {BREAK, false};
				case node_type::continue_stmt: return {CONTINUE, false};
				case node_type::fallthrough_stmt: return {FALLTHROUGH, false};
				case node_type::block_stmt: return sequence(static_cast<const block_stmt*>(a_statement)->declarations);
				case node_type::statement_decl: return analyze(static_cast<const statement_decl*>(a_statement)->statement.get());
				case node_type::expression_stmt:
				case node_type::expression_decl: {
					const auto* value = a_statement->get_type() == node_type::expression_stmt
						? static_cast<const expression_stmt*>(a_statement)->expression.get()
						: static_cast<const expression_decl*>(a_statement)->expression.get();
					if (value && value->get_type() == node_type::throw_expr) { return {THROW, true}; }
					return {NORMAL, value && value->get_type() != node_type::literal_expr};
				}
				case node_type::if_stmt: {
					const auto* branch = static_cast<const if_stmt*>(a_statement);
					if (auto condition = literalTruth(branch->condition.get())) {
						return analyze(*condition ? branch->then_statement.get() : branch->else_statement.get());
					}
					auto left = analyze(branch->then_statement.get());
					auto right = analyze(branch->else_statement.get());
					return {static_cast<uint8_t>(left.paths | right.paths), true};
				}
				case node_type::while_stmt: {
					const auto* loop = static_cast<const while_stmt*>(a_statement);
					return loopResult(loop->condition.get(), analyze(loop->body.get()));
				}
				case node_type::for_stmt: {
					const auto* loop = static_cast<const for_stmt*>(a_statement);
					auto result = analyze(loop->initializer.get());
					if (!(result.paths & (NORMAL | MAYBE_NORMAL))) { return result; }
					auto body = loopResult(loop->condition.get(), analyze(loop->body.get()));
					result.paths = static_cast<uint8_t>((result.paths & ~(NORMAL | MAYBE_NORMAL)) | body.paths);
					result.mayThrow = result.mayThrow || body.mayThrow || loop->update != nullptr;
					return result;
				}
				case node_type::range_for_stmt: {
					auto body = analyze(static_cast<const range_for_stmt*>(a_statement)->body.get());
					body.paths = static_cast<uint8_t>(NORMAL | (body.paths & (RETURN | THROW | BARE_RETURN)));
					body.mayThrow = true;
					return body;
				}
				case node_type::try_stmt: {
					const auto* attempt = static_cast<const try_stmt*>(a_statement);
					auto result = analyze(attempt->try_block.get());
					if (result.mayThrow || (result.paths & THROW)) {
						auto caught = analyze(attempt->catch_block.get());
						const uint8_t caughtPaths = (result.paths & THROW) ? caught.paths : uncertainNormal(caught.paths);
						result.paths = static_cast<uint8_t>((result.paths & ~THROW) | caughtPaths);
						result.mayThrow = caught.mayThrow;
					}
					return result;
				}
				case node_type::switch_stmt: {
					const auto* choice = static_cast<const switch_stmt*>(a_statement);
					Result next = choice->default_case ? sequence(choice->default_case->body) : Result{};
					Result result = next;
					for (size_t i = choice->cases.size(); i-- > 0;) {
						auto current = sequence(choice->cases[i]->body);
						uint8_t paths = current.paths;
						if ((paths & FALLTHROUGH) || (choice->cases[i]->has_fallthrough && (paths & NORMAL))) {
							paths = static_cast<uint8_t>((paths & ~(FALLTHROUGH | NORMAL)) | next.paths);
							current.mayThrow = current.mayThrow || next.mayThrow;
						}
						if (paths & BREAK) { paths = static_cast<uint8_t>((paths & ~BREAK) | NORMAL); }
						current.paths = paths;
						result.paths = static_cast<uint8_t>(result.paths | paths);
						result.mayThrow = result.mayThrow || current.mayThrow;
						next = current;
					}
					if (result.paths & BREAK) { result.paths = static_cast<uint8_t>((result.paths & ~BREAK) | NORMAL); }
					result.mayThrow = true;
					return result;
				}
				default: return {NORMAL, true};
			}
		}

	private:
		const type_info* returnType;

		static uint8_t uncertainNormal(uint8_t a_paths) {
			return static_cast<uint8_t>((a_paths & ~NORMAL) | ((a_paths & NORMAL) ? MAYBE_NORMAL : 0));
		}

		static std::optional<bool> literalTruth(const expression* a_expression) {
			if (!a_expression) { return true; }
			if (a_expression->get_type() != node_type::literal_expr) { return std::nullopt; }
			const auto& value = static_cast<const literal_expr*>(a_expression)->value;
			if (value.is_bool()) { return value.unchecked_as_bool(); }
			if (value.is_int()) { return value.unchecked_as_int() != 0; }
			if (value.is_float()) { return value.unchecked_as_float() != 0.0; }
			if (value.is_char()) { return value.unchecked_as_char() != '\0'; }
			if (value.is_string()) { return !value.unchecked_as_string().empty(); }
			if (value.is_null()) { return false; }
			return std::nullopt;
		}

		Result loopResult(const expression* a_condition, Result a_body) const {
			auto condition = literalTruth(a_condition);
			if (condition && !*condition) { return {}; }
			uint8_t paths = static_cast<uint8_t>(a_body.paths & (RETURN | THROW | BARE_RETURN));
			if (!condition || (a_body.paths & BREAK)) { paths = static_cast<uint8_t>(paths | NORMAL); }
			return {paths, a_body.mayThrow || !condition};
		}
	};

	inline bool has_complete_return(const function_decl& a_function) {
		if (!a_function.body) { return false; }
		auto result = ReturnFlow(a_function.return_type.get()).analyze(a_function.body.get());
		return !(result.paths & (ReturnFlow::NORMAL | ReturnFlow::MAYBE_NORMAL | ReturnFlow::BARE_RETURN));
	}
}
