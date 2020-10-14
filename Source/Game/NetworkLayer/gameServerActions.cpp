#include "gameServerActions.h"
#include "clientActions.h"

#ifdef BINDSTONE_SERVER
#include "Game/NetworkLayer/lobbyServer.h"
#include "Game/NetworkLayer/gameServer.h"
#endif

#include "Game/player.h"
#include "MV/Serialization/serialize.h"
#include "Game/game.h"

CEREAL_REGISTER_TYPE(RequestFullGameState);
CEREAL_REGISTER_TYPE(RequestBuildingUpgrade);
CEREAL_REGISTER_TYPE(SuppliedInitialGameState);
CEREAL_REGISTER_TYPE(GetInitialGameState);
CEREAL_REGISTER_TYPE(AssignPlayersToGame);
CEREAL_REGISTER_TYPE(GameServerAvailable);
CEREAL_REGISTER_TYPE(GameServerStateChange);
CEREAL_REGISTER_DYNAMIC_INIT(mv_gameserveractions);

#ifdef BINDSTONE_SERVER
void GameServerAvailable::execute(LobbyGameConnectionState* a_connection) {
	a_connection->setEndpoint(ourUrl, ourPort);
}
void GameServerStateChange::execute(LobbyGameConnectionState* a_connection) {
	a_connection->state(ourState == AVAILABLE ? LobbyGameConnectionState::AVAILABLE : LobbyGameConnectionState::OCCUPIED);
}

void AssignPlayersToGame::execute(GameServer& a_server) {
	a_server.assign(left, right, matchQueueId);
	a_server.lobby()->send(makeNetworkString<ExpectedPlayersNoted>());
}

void GetInitialGameState::execute(GameUserConnectionState* a_connection, GameServer &a_game) {
	auto player = a_game.userConnected(secret);
	a_connection->authenticate(player, secret);
	if (a_game.allUsersConnected()) {
		a_game.lobby()->send(makeNetworkString<GameServerStateChange>(GameServerStateChange::OCCUPIED));
		a_game.server()->sendAll(makeNetworkString<SuppliedInitialGameState>(a_game.leftPlayer(), a_game.rightPlayer(), a_game.instance()->networkPool()));
	}
}

void RequestBuildingUpgrade::execute(GameUserConnectionState* /*a_gameUser*/, GameServer &a_game) {
	a_game.instance()->performUpgrade(slot, id);
}

void RequestFullGameState::execute(GameUserConnectionState* a_gameUser, GameServer &a_game) {
	a_gameUser->connection()->send(makeNetworkString<SynchronizeAction>(a_game.instance()->networkPool().all()));
	//a_gameUser->connection()->send(makeNetworkString<RequestBuildingUpgrade>(a_game.data));
}
#endif

void RequestFullGameState::execute(Game &a_game) {

}

void SuppliedInitialGameState::execute(Game& a_game) {
	a_game.enterGame(left, right, pool);
}
