#include "accountActions.h"
#include "clientActions.h"

#ifdef BINDSTONE_SERVER
#include "lobbyServer.h"
#endif

#include "Game/player.h"
#include "Utility/cerealUtility.h"

#ifdef BINDSTONE_SERVER
std::string CreatePlayer::createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt) {
	static int work = 12;
	std::stringstream query;
	query << "INSERT INTO players(email, handle, passhash, passsalt, passiterations, softcurrency, hardcurrency, state, serverstate)";
	query << "VALUES(" << transaction.quote(email) << ", " << transaction.quote(handle) << ", ";
	query << transaction.quote(MV::sha512(password, a_salt, work)) << ", " << transaction.quote(a_salt) << ", " << work << ", ";
	query << DEFAULT_SOFT_CURRENCY << ", " << DEFAULT_HARD_CURRENCY << ", ";
	query << transaction.quote(makeSaveString()) << ", " << transaction.quote(makeServerSaveString()) << ")";
	return query.str();
}

pqxx::result CreatePlayer::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passhash, passsalt, passiterations, state, serverstate, email, handle FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(email);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(handle);

	return a_transaction->exec(activeQuery);
}

pqxx::result CreatePlayer::createPlayer(pqxx::work* transaction, LobbyUserConnectionState *a_connection) {
	auto salt = MV::randomString(32);

	a_connection->connection()->send(makeNetworkString<MessageAction>("User created, woot!"));
	sendValidationEmail(a_connection, salt);
	return transaction->exec(createPlayerQueryString(*transaction, salt));;
}

void CreatePlayer::execute(LobbyUserConnectionState* a_connection) {
	auto self = std::static_pointer_cast<CreatePlayer>(shared_from_this());
	auto connectionLifespan = a_connection->connection();
	a_connection->server().databasePool().task([=]() mutable {
		try {
			self; //force capture;
			if (connectionLifespan->disconnected()) {
				return;
			}
			if (email.empty() || !validateHandle(handle) || password.size() < 8) {
				a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to supply a valid email/handle and password."));
			} else {
				auto database = a_connection->server().database();
				auto transaction = std::make_unique<pqxx::work>(*database);

				auto result = selectUser(transaction.get());

				if (result.empty()) {
					result = createPlayer(transaction.get(), a_connection);
				} else if (!result[0][0].as<bool>()) {
					a_connection->connection()->send(makeNetworkString<LoginResponse>("Player not email validated yet."));
					sendValidationEmail(a_connection, result[0][2].c_str());
					MV::info("Need Validation: [", email, "]");
				} else {
					auto hashedPassword = MV::sha512(password, result[0][2].c_str(), result[0][3].as<int>());
					if (hashedPassword == result[0][1].c_str()) {
						std::string playerStateJson = result[0][4].c_str();
						if (a_connection->authenticate(result[0][6].c_str(), result[0][7].c_str(), playerStateJson, result[0][5].c_str())) {
							a_connection->connection()->send(makeNetworkString<LoginResponse>("Successful login.", playerStateJson, true));
							MV::info("Login Success: [", email, "]");
						} else {
							a_connection->connection()->send(makeNetworkString<LoginResponse>("Load Player Parse Fail: Contact Support"));
							MV::error("Parse Fail Load Player: [", email, "]\n___\n", playerStateJson, "\n___\n");
						}
					} else {
						a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to create due to existing player with different credentials."));
						MV::info("Failed to create: [", email, "] credential mismatch for existing user!");
					}
				}

				transaction->commit();
			}
		} catch (std::exception& e) {
			MV::error("Failed to execute CreatePlayer: [", email, "]\nWHAT: [", e.what(), "]");
		}
	});
}

void CreatePlayer::sendValidationEmail(LobbyUserConnectionState *a_connection, const std::string &a_passSalt) {
	a_connection->server().email(MV::Email::Addresses("mike@m2tm.net", email), "Bindstone Account Activation", "This will be a link to activate your account, for now... Check this out: https://www.youtube.com/watch?v=VAZsBEELqPw \n" + a_passSalt);
}

std::string CreatePlayer::makeSaveString() {
	auto newPlayer = std::make_shared<Player>();
	newPlayer->handle = handle;
	newPlayer->loadout.buildings = { "Life", "Life", "Life", "Life", "Life", "Life", "Life", "Life" };
	newPlayer->loadout.skins = { "", "", "", "", "", "", "", "" };

	newPlayer->wallet.add(Wallet::CurrencyType::SOFT, DEFAULT_SOFT_CURRENCY);
	newPlayer->wallet.add(Wallet::CurrencyType::HARD, DEFAULT_HARD_CURRENCY);
	return MV::toJson(newPlayer);
}

std::string CreatePlayer::makeServerSaveString() {
	auto newServerPlayer = std::make_shared<ServerPlayer>();
	return MV::toJson(newServerPlayer);
}

pqxx::result LoginRequest::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passhash, passsalt, passiterations, state, serverstate, email, handle FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(identifier);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(identifier);

	return a_transaction->exec(activeQuery);
}

void LoginRequest::execute(LobbyUserConnectionState* a_connection) {
	auto self = std::static_pointer_cast<LoginRequest>(shared_from_this());
	auto connectionLifespan = a_connection->connection();
	a_connection->server().databasePool().task([=]() mutable {
		try {
			self; //force capture;
			if (connectionLifespan->disconnected()){
				return;
			}
			if (identifier.empty() || password.size() < 8) {
				a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to supply a valid email/handle and password."));
			} else {
				auto database = a_connection->server().database();
				auto transaction = std::make_unique<pqxx::work>(*database);

				auto result = selectUser(transaction.get());

				if (result.empty()) {
					a_connection->connection()->send(makeNetworkString<LoginResponse>("No player exists."));
				} else if (!result[0][0].as<bool>()) {
					a_connection->connection()->send(makeNetworkString<LoginResponse>("Player not email validated yet."));
					MV::info("Need Validation: [", identifier, "]");
				} else {
					auto hashedPassword = MV::sha512(password, result[0][2].c_str(), result[0][3].as<int>());
					if (hashedPassword == result[0][1].c_str()) {
						std::string playerStateJson = result[0][4].c_str();
						if (a_connection->authenticate(result[0][6].c_str(), result[0][7].c_str(), playerStateJson, result[0][5].c_str())) {
							a_connection->connection()->send(makeNetworkString<LoginResponse>("Successful login.", MV::sha512(playerStateJson) == saveHash ? "" : playerStateJson, true));
							MV::info("Login Success: [", identifier, "]");
						} else {
							a_connection->connection()->send(makeNetworkString<LoginResponse>("Load Player Parse Fail: Contact Support"));
							MV::error("Parse Fail Load Player: [", identifier, "]\n___\n", playerStateJson, "\n___\n");
						}
					} else {
						a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to authenticate."));
						MV::info("Failed to authenticate: [", identifier, "] credential mismatch for existing user!\n[", hashedPassword, "]\n[", result[0][1].c_str(), "]");
					}
				}

				transaction->commit();
			}
		} catch (std::exception& e) {
			MV::info("Failed to execute LoginRequest: [", identifier, "]\nWHAT: [", e.what(), "]");
		}
	});
}

void FindMatchRequest::execute(LobbyUserConnectionState* a_connection) {
	a_connection->state("finding");
	a_connection->seekMatch(a_connection->server().queue(type));
}

void ExpectedPlayersNoted::execute(LobbyGameConnectionState* a_connection) {
	a_connection->notifyPlayersOfGameServer();
}
#endif
