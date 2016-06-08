#include "lobbyServer.h"

#include "Game/player.h"


LobbyConnectionState::LobbyConnectionState(MV::Connection *a_connection, LobbyServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void LobbyConnectionState::connect() {
	auto in = MV::toBase64Cast<ClientResponse>(std::make_shared<ServerDetails>());
	std::cout << "SENDING:\n" << in << std::endl;
	auto out = MV::fromBase64<std::shared_ptr<ClientResponse>>(in);
	std::cout << std::static_pointer_cast<ServerDetails>(out)->forceClientVersion << std::endl;

	connection()->send(in);
}

std::string CreatePlayer::makeSaveString() {
	auto newPlayer = std::make_shared<Player>();
	newPlayer->name = handle;
	newPlayer->loadout.buildings = { "life", "life", "life", "life", "life", "life", "life", "life" };
	newPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };

	newPlayer->wallet.add(Wallet::CurrencyType::SOFT, 450);
	newPlayer->wallet.add(Wallet::CurrencyType::HARD, 150);
	std::stringstream stream;
	{
		cereal::JSONOutputArchive archive(stream);
		archive(newPlayer);
	}
	return stream.str();
}
