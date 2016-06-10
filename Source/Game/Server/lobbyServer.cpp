#include "lobbyServer.h"
#include "serverActions.h"
#include "clientActions.h"

LobbyConnectionState::LobbyConnectionState(MV::Connection *a_connection, LobbyServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void LobbyConnectionState::connect() {
	auto in = MV::toBinaryStringCast<ClientAction>(std::make_shared<ServerDetails>());
	std::cout << "SENDING:\n" << in << std::endl;
	auto out = MV::fromBinaryString<std::shared_ptr<ClientAction>>(in);
	std::cout << std::static_pointer_cast<ServerDetails>(out)->forceClientVersion << std::endl;

	connection()->send(in);
}

void LobbyConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<ServerAction>>(a_message);
	action->execute(*this);
}
