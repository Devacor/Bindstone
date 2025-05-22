#include "managers.h"
#include "player.h"
#include "Instance/gameInstance.h"
#include "Game/Interface/interfaceManager.h"
#include "Game/NetworkLayer/package.h"

MV::Script::Registrar<Managers> _hookManagers([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	a_script.add(chaiscript::type_conversion<int, float>());
	a_script.add(chaiscript::type_conversion<int, double>());
	a_script.add(chaiscript::type_conversion<int, long>());
	a_script.add(chaiscript::type_conversion<float, double>());
	a_script.add(chaiscript::type_conversion<int, int32_t>());
	a_script.add(chaiscript::type_conversion<int, int64_t>());
	a_script.add(chaiscript::type_conversion<int16_t, int32_t>());
	a_script.add(chaiscript::type_conversion<int16_t, int64_t>());
	a_script.add(chaiscript::type_conversion<int32_t, int64_t>());

	//Narrowing conversions added too, because they'll still happen in the language but at lest we can make them more efficient:
	a_script.add(chaiscript::type_conversion<double, float>());
	a_script.add(chaiscript::type_conversion<long, int>());
	a_script.add(chaiscript::type_conversion<int32_t, int>());
	a_script.add(chaiscript::type_conversion<int64_t, int>());
	a_script.add(chaiscript::type_conversion<int32_t, int16_t>());
	a_script.add(chaiscript::type_conversion<int64_t, int16_t>());
	a_script.add(chaiscript::type_conversion<int64_t, int32_t>());

	a_script.add(chaiscript::fun([](int a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](size_t a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](float a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](double a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](bool a_from) {return MV::to_string(a_from); }), "to_string");
	
	a_script.add(chaiscript::fun([](const std::string& a_output) {
		MV::info("CHAISCRIPT: ", a_output);
	}), "log_chaiscript_output");
	a_script.eval("global print = fun(x){ log_chaiscript_output(to_string(x)); };");
});

MV::Script::Registrar<StandardMessages> _hookStandardMessages([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
	auto messages = a_services.get<StandardMessages>();
	a_script.add_global(chaiscript::var(messages), "messages");
	a_script.add(chaiscript::fun(&StandardMessages::lobbyConnected), "lobbyConnected");
	a_script.add(chaiscript::fun(&StandardMessages::lobbyDisconnect), "lobbyDisconnect");
	a_script.add(chaiscript::fun(&StandardMessages::lobbyAuthenticated), "lobbyAuthenticated");
});

MV::ScriptSignalRegistrar<void()> _lobbyConnected{};
MV::ScriptSignalRegistrar<void(const std::string&)> _lobbyDisconnect{};
MV::ScriptSignalRegistrar<void(bool, const std::string&)> _lobbyAuthenticated{};
