#include "gameServerActions.h"
#include "clientActions.h"

#ifdef BINDSTONE_SERVER
#include "lobbyServer.h"
#endif

#include "Game/player.h"
#include "Utility/cerealUtility.h"
#include "Game/NetworkLayer/gameServer.h"
#include "Game/game.h"

#ifdef BINDSTONE_SERVER
void GameServerAvailable::execute(LobbyGameConnectionState* a_connection) {
	a_connection->setEndpoint(ourUrl, ourPort);
}

void AssignPlayersToGame::execute(GameServer& a_server) {
	a_server.assign(left, right, matchQueueId);
	a_server.lobby()->send(makeNetworkString<ExpectedPlayersNoted>());
}
#endif

void GetInitialGameState::execute(GameUserConnectionState* a_connection, GameServer &a_game) {
	auto player = a_game.userConnected(secret);
	a_connection->authenticate(player, secret);
	if (a_game.allUsersConnected()) {
		a_game.server()->send(makeNetworkString<SuppliedInitialGameState>(a_game.leftPlayer(), a_game.rightPlayer()));
	}
}

void SuppliedInitialGameState::execute(Game& a_game) {
	a_game.enterGame(left, right);
}

void RequestBuildingUpgrade::execute(GameUserConnectionState* /*a_gameUser*/, GameServer &a_game) {
	a_game.instance()->performUpgrade(side, slot, id);
	a_game.server()->send(makeNetworkString<RequestBuildingUpgrade>(side, slot, id));
}

void RequestBuildingUpgrade::execute(Game &a_game) {
	a_game.instance()->performUpgrade(side, slot, id);
}
