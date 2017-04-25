#include "gameServer.h"
#include "networkAction.h"
#include "clientActions.h"

GameUserConnectionState::GameUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, GameServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void GameUserConnectionState::connectImplementation() {
	connection()->send(makeNetworkString<ServerDetails>());
}

void GameUserConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
	action->execute(this, ourServer);
}
