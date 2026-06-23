#include "synchronizeAction.h"
#include "Game/game.h"

#include <jaiscript/core/registrar.hpp>

static jai::registrar<SynchronizeAction, MV::Services, jai::bases<NetworkAction>> _regSynchronizeAction("SynchronizeAction");

void SynchronizeAction::execute(Game& a_game) {
	if (a_game.instance()) {
		a_game.instance()->networkPool().synchronize(objects);
	}
}

#ifdef BINDSTONE_SERVER
void SynchronizeAction::execute(GameServer&) {

}
void SynchronizeAction::execute(GameUserConnectionState*, GameServer&) {

}
void SynchronizeAction::execute(LobbyUserConnectionState*) {

}
void SynchronizeAction::execute(LobbyGameConnectionState*) {

}
#endif