#include "accountActions.h"
#include "clientActions.h"
#include "lobbyServer.h"
#include "Game/player.h"

pqxx::result CreatePlayer::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passsalt, passhash, passiterations FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(email);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(handle);

	return a_transaction->exec(activeQuery);
}

pqxx::result CreatePlayer::createPlayer(pqxx::work* transaction, LobbyConnectionState &a_connection) {
	auto salt = MV::randomString(32);

	a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("User created, woot!")));
	sendValidationEmail(a_connection, salt);
	return transaction->exec(createPlayerQueryString(*transaction, salt));;
}

void CreatePlayer::execute(LobbyConnectionState& a_connection) {
	auto self = std::static_pointer_cast<CreatePlayer>(shared_from_this());
	a_connection.server().databasePool().task([=]() mutable {
		try {
			self; //force capture;
			if (email.empty() || !validateHandle(handle) || password.size() < 8) {
				a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Failed to supply a valid email/handle and password.")));
			} else {
				auto database = a_connection.server().database();
				auto transaction = std::make_unique<pqxx::work>(*database);

				auto result = selectUser(transaction.get());

				if (result.empty()) {
					result = createPlayer(transaction.get(), a_connection);
				} else if (!result[0][0].as<bool>()) {
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Player not email validated yet.")));
					sendValidationEmail(a_connection, result[0][2].c_str());
					MV::info("Need Validation: [", email, "]");
				} else if (MV::sha512(password, result[0][2].c_str(), result[0][3].as<int>()) == result[0][1].c_str()) {
					a_connection.authenticate();
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Successful login.")));
					MV::info("Login Success: [", email, "]");
				} else {
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Failed to create due to existing player with different credentials.")));
					MV::info("Failed to create: [", email, "] credential mismatch for existing user!");
				}

				transaction->commit();
			}
			doneFlag = true;
		} catch (std::exception& e) {
			MV::error("Failed to execute CreatePlayer: [", email, "]\nWHAT: [", e.what(), "]");
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

pqxx::result LoginRequest::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passsalt, passhash, passiterations FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(identifier);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(identifier);

	return a_transaction->exec(activeQuery);
}

void LoginRequest::execute(LobbyConnectionState& a_connection) {
	auto self = std::static_pointer_cast<CreatePlayer>(shared_from_this());
	a_connection.server().databasePool().task([=]() mutable {
		try {
			self; //force capture;
			if (identifier.empty() || password.size() < 8) {
				a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Failed to supply a valid email/handle and password.")));
			} else {
				auto database = a_connection.server().database();
				auto transaction = std::make_unique<pqxx::work>(*database);

				auto result = selectUser(transaction.get());

				if (result.empty()) {
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("No player exists.")));
				} else if (!result[0][0].as<bool>()) {
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Player not email validated yet.")));
					MV::info("Need Validation: [", identifier, "]");
				} else if (MV::sha512(password, result[0][2].c_str(), result[0][3].as<int>()) == result[0][1].c_str()) {
					a_connection.authenticate();
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Successful login.")));
					MV::info("Login Success: [", identifier, "]");
				} else {
					a_connection.connection()->send(MV::toBinaryStringCast<ClientAction>(std::make_shared<MessageResponse>("Failed to authenticate.")));
					MV::info("Failed to authenticate: [", identifier, "] credential mismatch for existing user!");
				}

				transaction->commit();
			}
			doneFlag = true;
		} catch (std::exception& e) {
			MV::info("Failed to execute LoginRequest: [", identifier, "]\nWHAT: [", e.what(), "]");
			doneFlag = true;
		}
	});
}