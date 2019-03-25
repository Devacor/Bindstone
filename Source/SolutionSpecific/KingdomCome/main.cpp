#include "diggerGame.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/services.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Utility/cerealUtility.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "Game/NetworkLayer/synchronizeAction.h"
#include "MV/Network/networkObject.h"

int main(int argc, char *argv[]) {
	Managers managers;
	DiggerGame game(managers);

	managers.timer.start();
	managers.timer.delta("tick");
	while (game.update(managers.timer.delta("tick"))) {
		game.handleInput();
		game.render();
		std::this_thread::yield();
	}
	return 0;
}
