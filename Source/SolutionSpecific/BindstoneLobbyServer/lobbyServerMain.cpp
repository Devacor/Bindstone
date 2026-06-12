#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"

#include "MV/Serialization/serialize.h"

#include "Game/NetworkLayer/lobbyServer.h"

#include "MV/Utility/scopeGuard.hpp"

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

// Lines of "email handle password"; each missing account is created through the same
// query/hash path CreatePlayer uses, so local clients can log straight in.
static void seedLocalTestAccounts(LobbyServer& a_server) {
	for (auto&& line : MV::fileLines("ServerConfig/localTestAccounts.config")) {
		auto fields = MV::explode(line, [](char c) { return c == ' '; });
		if (fields.size() == 3 && CreatePlayer::ensureAccount(*a_server.database(), fields[0], fields[1], fields[2])) {
			MV::info("Seeded local test account: [", fields[0], "]");
		}
	}
}

int main(int, char *[]) {
	Managers managers({});
	managers.timer.start();

	auto jaiEngine = jai::engine::make();
	jai::stdlib::register_all(*jaiEngine);
	jai::bind_registrar<MV::Services>(*jaiEngine, managers.services);
	managers.services.connect<jai::engine>(jaiEngine.get());
	MV::Services::instance().connect<jai::engine>(jaiEngine.get());

	try {
		auto server = std::make_shared<LobbyServer>(managers);
		seedLocalTestAccounts(*server);
		std::cout << "LobbyServer listening: users on 22325, game servers on 22326\n";
		bool done = false;
		while (!done) {
			managers.pool.run();
			auto tick = managers.timer.delta("tick");
			server->update(tick);
			std::this_thread::sleep_for(std::chrono::milliseconds(1)); //don't busy-spin a core
		}
	} catch (std::exception & e) {
		MV::error("Exception Toasted the Server: ", e.what());
	}

	return 0;
}
