#include "MV/Script/script.h"

#include <cstdlib>

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include "MV/Utility/services.hpp"
#include "MV/Utility/generalUtility.h"

namespace MV {

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

	bool scriptHotReloadEnabled() {
		static const bool enabled = environmentFlag("MV_SCRIPT_HOT_RELOAD");
		return enabled;
	}

	std::shared_ptr<jai::engine> makeScriptEngine(const Services& a_services, size_t a_memoryCapBytes) {
		auto engine = jai::engine::make();
		if (environmentFlag("MV_SCRIPT_INTERPRETER")) {
			MV::info("Script engine: interpreter backend forced via MV_SCRIPT_INTERPRETER");
		} else {
			engine->set_backend(jai::backend_type::vm); // must precede the first execute
		}
		engine->memory_cap(a_memoryCapBytes);
#ifdef _DEBUG
		engine->static_checking(jai::check_mode::warn);
#endif
		engine->set_script_error_handler([](const std::string& a_message) {
			MV::error("Script callback failed: ", a_message);
		});
		jai::stdlib::register_all(*engine);
		jai::bind_registrar<MV::Services>(*engine, a_services);
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
			engine_->execute(a_scriptContents);
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
			auto result = engine_->execute(a_scriptContents);
			return result.is_bool() ? result.as_bool() : a_default;
		} catch (const std::exception& e) {
			MV::error("Script [", a_scriptIdentifier, "] failed: ", e.what());
			return a_default;
		}
	}

}
