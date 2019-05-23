#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Utility/cerealUtility.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "Game/NetworkLayer/gameServer.h"

#include "MV/Utility/taskActions.hpp"

#include <fstream>

int main(int, char *[]) {
	Managers managers;
	managers.timer.start();
	bool done = false;
	auto server = std::make_shared<GameServer>(managers);
	MV::Task statDisplay;
	statDisplay.also("PrintBandwidth", [&](MV::Task&, double) {
		if (server->server()) {
			auto sent = static_cast<double>(server->server()->bytesPerSecondSent()) / 1024.0;
			auto received = static_cast<double>(server->server()->bytesPerSecondReceived()) / 1024.0;
			MV::info("Server Sent: [", sent, "]kbs Received: [", received, "]kbs");
		}
		return true;
	}).recent()->localInterval(1.0);
	while (!done) {
		managers.pool.run();
		auto tick = managers.timer.delta("tick");
		server->update(tick);
		statDisplay.update(tick);
		std::this_thread::yield();
	}

	return 0;
}