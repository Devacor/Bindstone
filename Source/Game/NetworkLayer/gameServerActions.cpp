#include "gameServerActions.h"
#include "clientActions.h"

#ifdef BINDSTONE_SERVER
#include "Game/NetworkLayer/lobbyServer.h"
#include "Game/NetworkLayer/gameServer.h"
#endif

#include "Game/player.h"
#include "MV/Serialization/serialize.h"
#include "Game/game.h"

#include <jaiscript/core/registrar.hpp>

// Polymorphic wire registrations: without these, toBinaryStringCast<NetworkAction> writes a
// typeless base payload and the receiving side cannot reconstruct the action.
static jai::registrar<GameServerAvailable, MV::Services, jai::bases<NetworkAction>> _regGameServerAvailable("GameServerAvailable");
static jai::registrar<GameServerStateChange, MV::Services, jai::bases<NetworkAction>> _regGameServerStateChange("GameServerStateChange");
static jai::registrar<MatchResult, MV::Services, jai::bases<NetworkAction>> _regMatchResult("MatchResult");
static jai::registrar<AssignPlayersToGame, MV::Services, jai::bases<NetworkAction>> _regAssignPlayersToGame("AssignPlayersToGame");
static jai::registrar<GetInitialGameState, MV::Services, jai::bases<NetworkAction>> _regGetInitialGameState("GetInitialGameState");
static jai::registrar<SuppliedInitialGameState, MV::Services, jai::bases<NetworkAction>> _regSuppliedInitialGameState("SuppliedInitialGameState");
static jai::registrar<RequestBuildingUpgrade, MV::Services, jai::bases<NetworkAction>> _regRequestBuildingUpgrade("RequestBuildingUpgrade");
static jai::registrar<RequestFullGameState, MV::Services, jai::bases<NetworkAction>> _regRequestFullGameState("RequestFullGameState");

#ifdef BINDSTONE_SERVER
// 22326 is mutual TLS, so the CA vouched for the certificate; it must be a game server's and name the host players are sent to.
void GameServerAvailable::execute(LobbyGameConnectionState* a_connection) {
	auto connection = a_connection->connection();
	auto& certificate = connection->peerCertificate();
	auto host = MV::Url(ourUrl).host();
	std::string refusal;
	if (!certificate) {
		refusal = "it presented no client certificate";
	} else if (!certificate->commonName.starts_with(GameCertificates::GAME_SERVER_CLIENT_PREFIX)) {
		refusal = "its certificate (" + certificate->describe() + ") is not a game server's";
	} else if (!certificate->names(host)) {
		refusal = "it advertises [" + host + "] but its certificate (" + certificate->describe() + ") does not name that host";
	}
	if (!refusal.empty()) {
		MV::error("Refused game server registration from [", connection->peerAddress(), "]: ", refusal);
		connection->disconnect();
		return;
	}
	MV::info("Game server [", certificate->commonName.substr(GameCertificates::GAME_SERVER_CLIENT_PREFIX.size()), "] registered from [", connection->peerAddress(), "] for ", ourUrl, ":", ourPort, " (", connection->tlsSession(), ", certificate ", certificate->describe(), ")");
	connection->authenticate();
	a_connection->setEndpoint(ourUrl, ourPort);
}

void MatchResult::execute(LobbyGameConnectionState* a_connection) {
	a_connection->recordResult(winner);
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
	if (player) {
		a_connection->connection()->authenticate();
	}
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
