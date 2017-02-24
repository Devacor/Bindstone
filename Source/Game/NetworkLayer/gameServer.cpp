#include "gameServer.h"
#include "serverActions.h"
#include "clientActions.h"

GameUserConnectionState::GameUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, GameServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void GameUserConnectionState::connectImplementation() {
	auto in = MV::toBinaryStringCast<ClientAction>(std::make_shared<ServerDetails>());
	std::cout << "SENDING:\n" << in << std::endl;
	auto out = MV::fromBinaryString<std::shared_ptr<ClientAction>>(in);
	std::cout << std::static_pointer_cast<ServerDetails>(out)->forceClientVersion << std::endl;

	connection()->send(in);
}

void GameUserConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<ServerUserAction>>(a_message);
	action->execute(this);
}

bool GameUserConnectionState::authenticate(const std::string& a_email, const std::string& a_name, const std::string &a_newState, const std::string &a_serverState) {
	try {
		ourPlayer = MV::fromJson<std::shared_ptr<ServerPlayer>>(a_serverState);
		ourPlayer->client = MV::fromJson<std::shared_ptr<Player>>(a_newState);
		ourPlayer->client->email = a_email;
		ourPlayer->client->handle = a_name;
		auto lockedConnection = connection();
		for (auto&& c : ourServer.server()->connections()) {
			if (lockedConnection != c && (static_cast<GameUserConnectionState*>(c->state())->player()->client->email == ourPlayer->client->email)) {
				c->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<IllegalResponse>("Logged in elsewhere.")));
				c->disconnect();
			}
		}
		loggedIn = true;
		return true;
	} catch (...) {
		return false;
	}
}
