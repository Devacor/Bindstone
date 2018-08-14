#include "synchronizeAction.h"
#include "Game/game.h"


void SynchronizeAction::execute(Game& a_game) {
	if (a_game.instance()) {
		a_game.instance()->networkPool().synchronize(objects);
	}
}
void SynchronizeAction::execute(GameServer&) {

}
void SynchronizeAction::execute(GameUserConnectionState*, GameServer&) {

}
void SynchronizeAction::execute(LobbyUserConnectionState*) {

}
void SynchronizeAction::execute(LobbyGameConnectionState*) {

}
