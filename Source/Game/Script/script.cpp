#ifdef _WIN32
#define JAISCRIPT_ENABLE_DEBUGGER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <jaiscript/debug/connector.hpp>
#endif

#include "MV/Script/script.h"

#include <cstdlib>

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include <jaiscript/stdlib/stdlib.hpp>
#include "MV/Utility/services.hpp"
#include "MV/Utility/generalUtility.h"

namespace MV {
	// Script-visible spelling of MV::PathMode: PathMode.search / PathMode.flowField
	struct PathModeValues {
		int64_t search = static_cast<int64_t>(MV::PathMode::search);
		int64_t flowField = static_cast<int64_t>(MV::PathMode::flowField);
	};
	static jai::registrar<PathModeValues, MV::Services> _hookPathModeValues("PathModeValues",
		[](jai::dynamic_binder<PathModeValues>& builder, const MV::Services&) {
		builder.property("search", &PathModeValues::search);
		builder.property("flowField", &PathModeValues::flowField);
	});


	namespace {
		bool environmentFlag(const char* a_name) {
			const char* value = std::getenv(a_name);
			return value && *value && *value != '0';
		}

#ifdef _DEBUG
		// Warn-mode lint (see makeScriptEngine): check() is parse-cached, so this is
		// amortized once per unique source.
		void logCheckDiagnostics(jai::engine& a_engine, const std::string& a_scriptIdentifier, const std::string& a_scriptContents) {
			if (a_engine.static_checking() == jai::check_mode::off) { return; }
			try {
				auto report = a_engine.check(a_scriptContents);
				if (!report.diagnostics.empty()) {
					MV::warning("Script check [", a_scriptIdentifier, "]:\n", report.format(a_scriptContents, a_scriptIdentifier));
				}
			} catch (const std::exception&) {
				// Parse errors surface from execute() with full context.
			}
		}
#endif
	}

	namespace {
		bool editModeOn_ = false;
	}

	void editMode(bool a_enabled) { editModeOn_ = a_enabled; }
	bool editMode() { return editModeOn_; }

	bool scriptHotReloadEnabled() {
		static const bool envEnabled = environmentFlag("MV_SCRIPT_HOT_RELOAD");
		return envEnabled || editModeOn_;
	}

	namespace {
		bool scriptDebugOn_ = true;
		int scriptDebugPortOffset_ = 0;
		int lastScriptDebugPort_ = 0;

#ifdef JAISCRIPT_ENABLE_DEBUGGER
		// Exclusive-bind probe: the connector's own listener sets SO_REUSEADDR, which on
		// Windows lets a second bind on a taken port "succeed" (in-process or a second
		// Bindstone instance) — probe without it to find a genuinely free port.
		bool debugPortFree(int a_port) {
			static const bool wsaReady = [] { WSADATA wsa; return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0; }();
			if (!wsaReady) { return false; }
			SOCKET probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (probe == INVALID_SOCKET) { return false; }
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_port = htons(static_cast<u_short>(a_port));
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			bool available = ::bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
			::closesocket(probe);
			return available;
		}

		void attachScriptDebugger(jai::engine& a_engine) {
			if (!scriptDebugEnabled()) { return; }
			int base = jai::debug::default_port + scriptDebugPortOffset();
			for (int candidate = base; candidate < base + 80; candidate += 10) {
				if (!debugPortFree(candidate)) { continue; }
				auto connector = jai::debug::listen(a_engine, candidate);
				if (connector->port() == candidate) {
					lastScriptDebugPort_ = candidate;
					MV::info("Script debugger listening on 127.0.0.1:", candidate, " (VS Code: Attach to JaiScript)");
					return;
				}
				a_engine.set_debug_connector(nullptr);
			}
			MV::warning("Script debugger: no free port at ", base, " (+10 steps tried); running without the debug listener");
		}
#endif
	}

	void scriptDebugEnabled(bool a_enabled) { scriptDebugOn_ = a_enabled; }
	bool scriptDebugEnabled() { return scriptDebugOn_ && !environmentFlag("MV_SCRIPT_NO_DEBUG"); }
	void scriptDebugPortOffset(int a_offset) { scriptDebugPortOffset_ = a_offset; }
	int scriptDebugPortOffset() { return scriptDebugPortOffset_; }
	int lastScriptDebugPort() { return lastScriptDebugPort_; }

	std::shared_ptr<jai::engine> makeScriptEngine(const Services& a_services, size_t a_memoryCapBytes) {
		auto engine = jai::engine::make();
		if (environmentFlag("MV_SCRIPT_INTERPRETER")) {
			engine->set_backend(jai::backend_type::interpreter); // must precede the first execute
			MV::info("Script engine: interpreter backend forced via MV_SCRIPT_INTERPRETER");
		}
		engine->memory_cap(a_memoryCapBytes);
		engine->add_include_path("Assets");   // include "Creatures/Shared/status.script" from any script
		engine->static_checking(jai::check_mode::warn);   // engine default is strict; the live client stays at warn (-verifyassets runs the same scripts strict)
		engine->set_script_error_handler([raw = engine.get()](const std::string& a_message) {
			auto trace = raw->format_stack_trace();
			MV::error("Script callback failed: ", a_message, trace.empty() ? "" : "\n", trace);
		});
		jai::stdlib::register_all(*engine);
		jai::bind_registrar<MV::Services>(*engine, a_services);
		engine->add_global("InEditMode", jai::script_value(editMode(), engine.get()));
		engine->add_global("PathMode", engine->make_object(std::make_shared<PathModeValues>()));
		engine->add_global("SpawnBudget", jai::script_value(static_cast<int64_t>(-1), engine.get()));   // buildings spawn without limit; the arena caps it
		if (editMode()) {
			MV::info("Script engine: edit mode (InEditMode=true, gameplay script hot-reload on)");
		}
#ifdef JAISCRIPT_ENABLE_DEBUGGER
		attachScriptDebugger(*engine);
#endif
		return engine;
	}

	Script::Script(const Services& a_services, const std::vector<std::string>&) :
		engine_(a_services.get<jai::engine>()) {
	}

	bool Script::eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents) const {
		return eval(a_scriptIdentifier, a_scriptContents, {});
	}

	bool Script::eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
		const std::map<std::string, jai::script_value>& a_localVariables) const {

		// Shadow colliding globals for the duration of the eval, then restore them.
		std::vector<std::pair<std::string, jai::script_value>> shadowed;
		std::vector<std::string> introduced;
		for (auto&& [name, value] : a_localVariables) {
			if (engine_->has_variable(name)) {
				shadowed.emplace_back(name, engine_->get_variable(name));
			} else {
				introduced.push_back(name);
			}
			engine_->add_global(name, value);
		}
#ifdef _DEBUG
		logCheckDiagnostics(*engine_, a_scriptIdentifier, a_scriptContents);
#endif
		bool success = true;
		try {
			engine_->execute_file_source(a_scriptIdentifier, a_scriptContents);
		} catch (const std::exception& e) {
			MV::error("Script [", a_scriptIdentifier, "] failed: ", e.what());
			success = false;
		}
		for (auto&& [name, value] : shadowed) {
			engine_->add_global(name, value);
		}
		// No global-removal API: null the names this eval introduced so they don't pin objects.
		for (auto&& name : introduced) {
			engine_->add_global(name, jai::script_value(std::monostate{}, engine_));
		}
		return success;
	}

	bool Script::evalBool(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
		bool a_default) const {
#ifdef _DEBUG
		logCheckDiagnostics(*engine_, a_scriptIdentifier, a_scriptContents);
#endif
		try {
			auto result = engine_->execute_file_source(a_scriptIdentifier, a_scriptContents);
			return result.is_bool() ? result.as_bool() : a_default;
		} catch (const std::exception& e) {
			MV::error("Script [", a_scriptIdentifier, "] failed: ", e.what());
			return a_default;
		}
	}

}
