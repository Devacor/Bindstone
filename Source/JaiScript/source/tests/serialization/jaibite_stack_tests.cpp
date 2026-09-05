#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/jaibite.hpp>
#include <jaiscript/detail/ast_serializer.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace jai::foundry::tests {

class jaibite_stack_tests : public suite {
public:
	jaibite_stack_tests() : suite("Jaibite Native Stack") {}

	struct nested_tree {
		std::vector<expression_ptr> pins;
		std::vector<declaration_ptr> declarations;

		explicit nested_tree(size_t depth) {
			source_location location;
			location.filename = "saved-depth-fixture";
			token plus;
			plus.type = token_type::plus;
			plus.lexeme = "+";
			plus.location = location;
			auto literal = [&]() {
				return std::make_shared<literal_expr>(location,
					script_value(script_value::ast_literal_tag{}, script_int(1)));
			};
			pins.push_back(literal());
			for (size_t i = 0; i < depth; ++i) {
				pins.push_back(std::make_shared<binary_expr>(location, pins.back(), plus, literal()));
			}
			declarations.push_back(std::make_shared<expression_decl>(location, pins.back(), true));
		}

		~nested_tree() {
			declarations.clear();
			while (!pins.empty()) { pins.pop_back(); }
		}
	};

	void forge_tests() override {
		test("valid_deep_tree_keeps_its_bytes_on_large_stack", [this]() {
			auto eng = make_engine();
			eng->static_checking(check_mode::off);
			nested_tree tree(800);
			const auto bytes = detail::serialize_jaibite(tree.declarations,
				*eng->get_symbolizer(), eng->registration_fingerprint());
			{
				auto loaded = eng->jaibite_load_bytes(bytes);
				check_eq(bytes, loaded.save_bytes());
			}
			check_eq(script_int(42), eng->execute("40 + 2;").as_int());
		});

#if defined(_WIN32)
		test("reader_and_writer_recover_on_one_megabyte_stack", [this]() {
			auto author = make_engine();
			for (size_t depth : {size_t(250), size_t(800)}) {
				nested_tree tree(depth);
				const auto bytes = detail::serialize_jaibite(tree.declarations,
					*author->get_symbolizer(), author->registration_fingerprint());
				struct probe_state {
					backend_type backend;
					engine& author;
					const nested_tree& tree;
					const std::vector<uint8_t>& bytes;
					std::string failure;
					bool completed = false;
				} state{author->get_backend_type(), *author, tree, bytes};
				auto probe = [](void* raw) -> DWORD {
					auto& state = *static_cast<probe_state*>(raw);
					try {
						auto eng = engine::make();
						eng->set_backend(state.backend);
						eng->static_checking(check_mode::off);
						try {
							auto loaded = eng->jaibite_load_bytes(state.bytes);
							check_eq(state.bytes, loaded.save_bytes());
						} catch (const jai::runtime_error& error) {
							check_eq(std::string("jaibite: AST nesting too deep"), std::string(error.what()));
						}
						check_eq(script_int(42), eng->execute("40 + 2;").as_int());
						try {
							check_eq(state.bytes, detail::serialize_jaibite(state.tree.declarations,
								*state.author.get_symbolizer(), state.author.registration_fingerprint()));
						} catch (const jai::runtime_error& error) {
							check_eq(std::string("jaibite: AST nesting too deep"), std::string(error.what()));
						}
						check_eq(script_int(42), eng->execute("40 + 2;").as_int());
						state.completed = true;
					} catch (const std::exception& error) {
						state.failure = error.what();
					} catch (...) {
						state.failure = "non-standard exception in small-stack probe";
					}
					return 0;
				};
				HANDLE thread = CreateThread(nullptr, 1024 * 1024, probe, &state,
					STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
				check_true(thread != nullptr, "could not create small-stack probe thread");
				const DWORD wait = WaitForSingleObject(thread, INFINITE);
				CloseHandle(thread);
				check_eq(DWORD(WAIT_OBJECT_0), wait);
				check_true(state.completed, state.failure);
			}
		});
#endif
	}
};

} // namespace jai::foundry::tests

using jaibite_stack_tests = jai::foundry::tests::jaibite_stack_tests;
FOUNDRY_REGISTER(jaibite_stack_tests)
