#include "gameServerActions.h"
#include "clientActions.h"
#include "lobbyServer.h"
#include "Game/player.h"
#include "Utility/cerealUtility.h"
#include "Game/NetworkLayer/gameServer.h"

void GameServerAvailable::execute(LobbyGameConnectionState* a_connection) {
	a_connection->setEndpoint(ourUrl, ourPort);
}

void AssignPlayersToGame::execute(GameServer& a_server) {
	a_server.assign(left, right, matchQueueId);
	a_server.lobby()->send(makeNetworkString<ExpectedPlayersNoted>());
}

void GetFullGameState::execute(GameUserConnectionState* a_connection, GameServer &) {
	a_connection->connection()->send(makeNetworkString<SuppliedFullGameState>());
}

void SuppliedFullGameState::execute(Game& a_connection) {

}
