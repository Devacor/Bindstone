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

void CreatePlayer::execute(LobbyConnectionState& a_connection) {
	std::cout << "THREAD: " << pqxx::describe_thread_safety().safe_libpq << std::endl;

	auto self = std::static_pointer_cast<CreatePlayer>(shared_from_this());
	a_connection.server().databasePool().task([=]() mutable {
		try {
			self; //force capture;
			if (email.empty() || !validateHandle(handle) || password.size() < 8) {
				a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Failed to supply a valid email/handle and password.")));
				doneFlag = true;
			} else {
				auto database = a_connection.server().database();
				auto transaction = std::make_unique<pqxx::work>(*database);

				std::string activeQuery = "SELECT verified, passsalt, passhash, passiterations FROM players WHERE ";
				activeQuery += "email = " + transaction->quote(email);
				activeQuery += " OR ";
				activeQuery += "handle = " + transaction->quote(handle);

				auto result = transaction->exec(activeQuery);

				if (result.empty()) {
					auto salt = MV::randomString(32);
					result = transaction->exec(createPlayerQueryString(*transaction, salt));
					transaction->commit();

					a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("User created, woot!")));
					sendValidationEmail(a_connection, salt);

				} else if (!result[0][0].as<bool>()) {
					a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Player not email validated yet.")));
					std::cout << "Not verified. Sending new validation email." << std::endl;
					sendValidationEmail(a_connection, result[0][3].c_str());
				} else if (MV::sha512(password, result[0][2].c_str(), result[0][3].as<int>()) == result[0][1].c_str()) {
					a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Successful login.")));
					std::cout << "Yup, we good." << std::endl;
				} else {
					a_connection.connection()->send(MV::toBase64Cast<ClientResponse>(std::make_shared<MessageResponse>("Failed to create due to existing player with different credentials.")));
					std::cout << "Failed to create due to existing player with different credentials." << std::endl;
				}
				doneFlag = true;
			}
		} catch (std::exception& e) {
			std::cerr << "Error caught: " << e.what() << std::endl;
			doneFlag = true;
		}
	});
}

void CreatePlayer::sendValidationEmail(LobbyConnectionState &a_connection, const std::string &a_passSalt) {
	a_connection.server().email(MV::Email::Addresses("mike@m2tm.net", email), "Bindstone Account Activation", "This will be a link to activate your account, for now... Check this out: https://www.youtube.com/watch?v=VAZsBEELqPw \n" + a_passSalt);
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

std::string CreatePlayer::createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt) {
	static int work = 12;
	std::stringstream query;
	query << "INSERT INTO players(email, handle, passhash, passsalt, passiterations, softcurrency, hardcurrency, state)";
	query << "VALUES(" << transaction.quote(email) << ", " << transaction.quote(handle) << ", ";
	query << transaction.quote(MV::sha512(password, a_salt, work)) << ", " << transaction.quote(a_salt) << ", " << work << ", ";
	query << DEFAULT_SOFT_CURRENCY << ", " << DEFAULT_HARD_CURRENCY << ", ";
	query << transaction.quote(makeSaveString()) << ")";
	return query.str();
}
