#include "synchronizeAction.h"
#include "Game/game.h"

CEREAL_REGISTER_TYPE(SynchronizeAction);
CEREAL_REGISTER_DYNAMIC_INIT(mv_synchronizeaction);

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