// Values nest as deep as memory allows: parsing, from_json/from_binary, to_json/to_binary and deep copy walk them on work
// lists, so a host thread's ordinary stack (1 MB here, as a host's main thread) never bounds the depth

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/serialization/convenience.hpp>

#include <exception>
#include <stdexcept>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

namespace deep_detail {
	// Runs body on a thread with a 1 MB stack and rethrows what it threw
	void on_small_stack(const std::function<void()>& body) {
		struct run {
			const std::function<void()>* body;
			std::exception_ptr failure;
			static void go(run* self) {
				try { (*self->body)(); } catch (...) { self->failure = std::current_exception(); }
			}
		} state{&body, nullptr};
		const size_t stack_bytes = 1024 * 1024;
#if defined(_WIN32)
		struct thunk { static DWORD WINAPI entry(LPVOID self) { run::go(static_cast<run*>(self)); return 0; } };
		HANDLE thread = CreateThread(nullptr, stack_bytes, &thunk::entry, &state, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
		if (!thread) { throw std::runtime_error("the small-stack thread did not start"); }
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
#else
		struct thunk { static void* entry(void* self) { run::go(static_cast<run*>(self)); return nullptr; } };
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, stack_bytes);
		pthread_t thread;
		const int started = pthread_create(&thread, &attr, &thunk::entry, &state);
		pthread_attr_destroy(&attr);
		if (started != 0) { throw std::runtime_error("the small-stack thread did not start"); }
		pthread_join(thread, nullptr);
#endif
		if (state.failure) { std::rethrow_exception(state.failure); }
	}

	constexpr int k_depth = 100000;

	std::string nested_arrays(int depth) { return std::string(depth, '[') + std::string(depth, ']'); }
	std::string nested_maps(int depth) {
		std::string result;
		result.reserve(static_cast<size_t>(depth) * 6 + 1);
		for (int i = 0; i < depth; ++i) { result += "{\"a\":"; }
		result += "1";
		result.append(static_cast<size_t>(depth), '}');
		return result;
	}

	// A chain of host records: their own serialize recurses through host code, so the archive's guard bounds it
	struct host_link {
		int n = 0;
		std::shared_ptr<host_link> next;
		template<typename Archive>
		void serialize(Archive& ar) {
			ar("n", n);
			ar("next", next);
		}
	};

	std::string host_chain_json(int depth) {
		std::string result = "{\"value0\":";
		for (int i = 0; i < depth; ++i) { result += "{\"$id\":" + std::to_string(i + 1) + ",\"$val\":{\"n\":" + std::to_string(i) + ",\"next\":"; }
		result += "null";
		for (int i = 0; i < depth; ++i) { result += "}}"; }
		result += "}";
		return result;
	}

	// The binary wire form of the same chain, built without the writer (which recurses once per level itself)
	std::vector<uint8_t> host_chain_binary(size_t depth) {
		std::vector<uint8_t> out;
		auto u8 = [&out](uint8_t v) { out.push_back(v); };
		auto u32 = [&out](uint32_t v) { for (int i = 0; i < 4; ++i) { out.push_back(static_cast<uint8_t>(v >> (8 * i))); } };
		auto str = [&](const std::string& s) { u32(static_cast<uint32_t>(s.size())); out.insert(out.end(), s.begin(), s.end()); };
		const uint32_t link_overhead = 39;     // 01, "", version, count, "n", "next", two sizes, the int, 02
		const uint32_t record_overhead = 41;   // 01, "", version, count, "$id", "$val", two sizes, the id, 02
		std::vector<uint32_t> next_size(depth), value_size(depth);
		uint32_t below = 1;   // the deepest next: the null marker
		for (size_t level = depth; level-- > 0;) {
			next_size[level] = below;
			value_size[level] = link_overhead + below;
			below = record_overhead + value_size[level];
		}
		u8(0x01); str(""); u32(1); u32(1); str("value0"); u32(below);
		for (size_t level = 0; level < depth; ++level) {
			u8(0x01); str(""); u32(0); u32(2); str("$id"); str("$val"); u32(4); u32(value_size[level]);
			u32(static_cast<uint32_t>(level + 1));
			u8(0x01); str(""); u32(0); u32(2); str("n"); str("next"); u32(4); u32(next_size[level]);
			u32(static_cast<uint32_t>(level));
		}
		u8(0x00);
		for (size_t level = 0; level < depth; ++level) { u8(0x02); u8(0x02); }
		u8(0x02);
		return out;
	}

	size_t host_chain_length(std::shared_ptr<host_link> head) {
		size_t result = 0;
		while (head) { ++result; head = std::move(head->next); }
		return result;
	}
}

class deep_nesting_tests : public suite {
public:
	deep_nesting_tests() : suite("Deep Nesting") {}

	void forge_tests() override {
		using namespace deep_detail;

		test("deep_arrays_and_maps_round_trip_through_every_archive", [this]() {
			on_small_stack([this]() {
				for (const std::string& text : {nested_arrays(k_depth), nested_maps(k_depth)}) {
					auto eng = make_engine();
					stdlib::register_all(*eng);
					eng->execution_budget(0);
					eng->add_global("text", eng->make_value(text));
					const std::string result = eng->execute(R"(
						var v = from_json(text);
						string out = to_string(to_json(v) == text);
						out += " " + to_string(to_json(from_binary(to_binary(v))) == text);
						out += " " + to_string(to_json(from_base64(to_base64(v))) == text);
						var copy = v;
						out += " " + to_string(to_json(copy) == text);
						v = null;
						copy = null;
						out;
					)").as<std::string>();
					check_eq(std::string("true true true true"), result, text.substr(0, 12));
				}
			});
		});

		test("deep_script_class_chains_round_trip_and_copy", [this]() {
			on_small_stack([this]() {
				auto eng = make_engine();
				stdlib::register_all(*eng);
				eng->execution_budget(0);
				eng->add_global("depth", eng->make_value(static_cast<script_int>(k_depth)));
				const std::string result = eng->execute(R"(
					class Link { int n = 0; var next = null; }
					function length_of(var p) -> int { int count = 0; while (p != null) { count += 1; p = p.next; } return count; }
					var head = new Link();
					var at = head;
					for (int i = 1; i < depth; i++) { var link = new Link(); link.n = i; at.next = link; at = link; }
					at = null;
					string out = to_string(length_of(from_json(to_json(head))));
					out += " " + to_string(length_of(from_binary(to_binary(head))));
					string nested = "";
					for (int i = 0; i < depth; i++) { nested += "{\"_type_\":\"Link\",\"next\":"; }
					nested += "null";
					for (int i = 0; i < depth; i++) { nested += "}"; }
					var values = from_json(nested);
					var copied = values;
					out += " " + to_string(to_json(copied) == to_json(values));   // a walk by value would copy the rest of the chain per step
					head = null;
					values = null;
					copied = null;
					out;
				)").as<std::string>();
				const std::string d = std::to_string(k_depth);
				check_eq(d + " " + d + " true", result);
			});
		});

		test("a_deep_host_record_chain_loads_or_raises_cleanly", [this]() {
			// a host type's own serialize recurses natively: an ordinary chain loads, a deep one loads whole or meets the guard
			on_small_stack([this]() {
				auto eng = make_engine();
				for (int depth : {200, 1500}) {
					for (bool binary : {false, true}) {
						std::string error;
						size_t length = 0;
						try {
							length = binary ? host_chain_length(jai::from_binary<std::shared_ptr<host_link>>(*eng, host_chain_binary(static_cast<size_t>(depth))))
							                : host_chain_length(jai::from_json<std::shared_ptr<host_link>>(*eng, host_chain_json(depth)));
						} catch (const serialization_error& e) {
							error = e.what();
						}
						const std::string what = std::string(binary ? "binary " : "json ") + std::to_string(depth) + ": " + error;
						if (depth == 200) {
							check_eq(size_t(200), length, what);
						} else {
							check_true(length == size_t(depth) || error.find("depth") != std::string::npos, what);
						}
					}
				}
			});
		});
	}
};

}

using deep_nesting_tests = jai::foundry::tests::deep_nesting_tests;
FOUNDRY_REGISTER(deep_nesting_tests)
