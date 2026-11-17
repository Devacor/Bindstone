#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <string>
#include <utility>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class coroutine_snapshot_tests : public suite {
public:
	coroutine_snapshot_tests() : suite("Coroutine Snapshot Tests") {}

	// The program's last value as text on each backend (interpreter, vm), an exception as "ERROR: what"
	static std::pair<std::string, std::string> both_backends(const std::string& a_source) {
		std::string out[2];
		int index = 0;
		for (bool use_vm : {false, true}) {
			auto e = jai::foundry::make_default_engine();
			e->set_backend(use_vm ? jai::backend_type::vm : jai::backend_type::interpreter);
			e->execution_budget(0);
			jai::stdlib::register_all(e);
			try { out[index] = e->execute(a_source).to_string(); }
			catch (const std::exception& ex) { out[index] = std::string("ERROR: ") + ex.what(); }
			++index;
		}
		return {out[0], out[1]};
	}

	void check_both(const std::string& a_source, const std::string& a_expected, const std::string& a_what) {
		auto [interpreted, compiled] = both_backends(a_source);
		check_eq(a_expected, interpreted, "interp: " + a_what);
		check_eq(a_expected, compiled, "vm: " + a_what);
	}

	void forge_tests() override {
		test("an_archive_never_drops_a_coroutine_silently", [this]() {
			const std::string prelude =
				"coroutine int count() { yield 1; yield 2; }\n"
				"class Boss { int hp = 3; coroutine int fight() { yield hp; } auto brain = fight(); }\n"
				"function attempt(f) { try { f(); return \"saved\"; } catch (e) { return \"\" + e; } }\n";
			const std::string free_text = "Cannot save coroutine 'count': a paused coroutine is not data";
			const std::string method_text = "Cannot save coroutine 'Boss::fight': a paused coroutine is not data";
			const std::pair<const char*, const std::string*> cases[] = {
				{"auto h = count(); attempt([&]() { to_json(h); })", &free_text},
				{"auto h = count(); attempt([&]() { to_binary(h); })", &free_text},
				{"auto h = count(); attempt([&]() { to_base64(h); })", &free_text},
				{"auto h = count(); h.next(); attempt([&]() { to_json([1, h]); })", &free_text},
				{"auto h = count(); attempt([&]() { to_binary({\"k\": h}); })", &free_text},
				{"Boss b; attempt([&]() { to_json(b); })", &method_text},
				{"Boss b; b.brain.next(); attempt([&]() { to_binary(b); })", &method_text},
			};
			for (const auto& [source, expected] : cases) { check_both(prelude + source, *expected, source); }
		});
	}
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::coroutine_snapshot_tests)
