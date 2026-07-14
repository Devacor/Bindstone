#ifndef _MV_SCRIPT_H_
#define _MV_SCRIPT_H_

#include <string>
#include <map>
#include <memory>
#include <vector>

namespace jai {
	class engine;
	class script_value;
}

namespace MV {
	class Services;

	// One place for engine policy: VM backend (env MV_SCRIPT_INTERPRETER=1 forces the
	// tree-walking interpreter for parity debugging), stdlib, Services-bound registrars,
	// script-error logging via MV::error, a memory cap, and static_checking(warn) in
	// debug builds (diagnostics surface through Script::eval as MV::warning).
	// Callers still connect the engine into their Services themselves.
	std::shared_ptr<jai::engine> makeScriptEngine(const Services& a_services, size_t a_memoryCapBytes = 256 * 1024 * 1024);

	// Editor-hosted process flag. The Workbench sets it TRUE before engine creation; the
	// shipping game never touches it. Game systems opt out of behaviors that assume a real
	// run by checking editMode(), scripts read the InEditMode global (registered by
	// makeScriptEngine), and gameplay script hot-reload turns on without the env var.
	void editMode(bool a_enabled);
	bool editMode();

	// Env MV_SCRIPT_HOT_RELOAD=1 (or editMode()): gameplay .script files re-eval when their
	// mtime changes (see StandardScriptMethods::loadScript). Off = parse-once shipping behavior.
	bool scriptHotReloadEnabled();

	// Script debugging (DAP; VS Code "Attach to JaiScript") is ON by default on Windows:
	// every engine makeScriptEngine creates listens on 127.0.0.1 at the first free port in
	// {base, base+10, base+20, ...} where base = 52472 + scriptDebugPortOffset(). The offset
	// is per build target (Client 0, LobbyServer 1, GameServer 2) so each process type has a
	// stable attach port. Costs nothing on the script hot path until a session attaches;
	// breakpoints/stepping need the interpreter backend (MV_SCRIPT_INTERPRETER=1). Bind
	// failure logs one line and the engine runs without the listener. Opt out with
	// MV_SCRIPT_NO_DEBUG=1 or scriptDebugEnabled(false) before engine creation
	// (BindstoneClient: --no-script-debug).
	void scriptDebugEnabled(bool a_enabled);
	bool scriptDebugEnabled();
	void scriptDebugPortOffset(int a_offset);
	int scriptDebugPortOffset();
	int lastScriptDebugPort();   // port the most recent makeScriptEngine bound; 0 = none

	// Forward declarations only here; the implementation TU (Source/Game/Script/script.cpp) includes the engine.
	class Script {
	public:
		explicit Script(jai::engine& a_engine) : engine_(&a_engine) {}
		explicit Script(const Services& a_services, const std::vector<std::string>& = {});

		jai::engine& engine() const { return *engine_; }

		// Locals are injected as temporary globals (load-time `self` pattern; single-threaded only).
		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents) const;
		bool eval(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
			const std::map<std::string, jai::script_value>& a_localVariables) const;

		bool evalBool(const std::string& a_scriptIdentifier, const std::string& a_scriptContents,
			bool a_default) const;

	private:
		jai::engine* engine_;
	};
}

#endif
