#include "accountActions.h"
#include "clientActions.h"

#include "lobbyServer.h"

#include "Game/player.h"
#include "MV/Utility/cerealUtility.h"

CEREAL_REGISTER_TYPE(CreatePlayer);
CEREAL_REGISTER_TYPE(LoginRequest);
CEREAL_REGISTER_TYPE(FindMatchRequest);
CEREAL_REGISTER_TYPE(ExpectedPlayersNoted);
CEREAL_REGISTER_DYNAMIC_INIT(mv_accountactions);

std::string CreatePlayer::createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt) {
	static int work = 12;
	std::stringstream query;
	query << "INSERT INTO players(email, handle, passhash, passsalt, passiterations, state, serverstate)";
	query << "VALUES(" << transaction.quote(email) << ", " << transaction.quote(handle) << ", ";
	query << transaction.quote(MV::sha512(password, a_salt, work)) << ", " << transaction.quote(a_salt) << ", " << work << ", ";
	query << transaction.quote(makeSaveString()) << ", " << transaction.quote(makeServerSaveString()) << ");";
	return query.str();
}

pqxx::result CreatePlayer::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passhash, passsalt, passiterations, state, serverstate, email, handle, id FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(email);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(handle) + ";";

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
				pqxx::work transaction(*database);

				auto result = selectUser(&transaction);

				if (result.empty()) {
					result = createPlayer(&transaction, a_connection);
				} else if (!result[0][0].as<bool>()) {
					a_connection->connection()->send(makeNetworkString<LoginResponse>("Player not email validated yet."));
					sendValidationEmail(a_connection, result[0][2].as<std::string>());
					MV::info("Need Validation: [", email, "]");
				} else {
					auto hashedPassword = MV::sha512(password, result[0][2].as<std::string>(), result[0][3].as<int>());
					if (hashedPassword == result[0][1].as<std::string>()) {
						std::string playerStateJson = result[0][4].as<std::string>();
						if (a_connection->authenticate(result[0][8].as<int64_t>(), result[0][6].as<std::string>(), result[0][7].as<std::string>(), playerStateJson, result[0][5].as<std::string>())) {
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

				transaction.commit();
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
	IntermediateDbPlayer newPlayer;
	newPlayer.loadouts["Default"].buildings = { "Life", "Life", "Life", "Life", "Life", "Life", "Life", "Life" };
	newPlayer.loadouts["Default"].skins = { "", "", "", "", "", "", "", "" };
	newPlayer.selectedLoadout = "";

	newPlayer.wallet.add(Wallet::CurrencyType::SOFT, DEFAULT_SOFT_CURRENCY);
	newPlayer.wallet.add(Wallet::CurrencyType::HARD, DEFAULT_HARD_CURRENCY);
	return MV::toJsonInline(newPlayer);
}

std::string CreatePlayer::makeServerSaveString() {
	ServerPlayer newPlayer;
	return MV::toJsonInline(newPlayer);
}

pqxx::result LoginRequest::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passhash, passsalt, passiterations, state, serverstate, email, handle, id FROM players WHERE ";
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
					auto hashedPassword = MV::sha512(password, result[0][2].as<std::string>(), result[0][3].as<int>());
					if (hashedPassword == result[0][1].as<std::string>()) {
						std::string playerStateJson = result[0][4].as<std::string>();
						if (a_connection->authenticate(result[0][8].as<int64_t>(), result[0][6].as<std::string>(), result[0][7].as<std::string>(), playerStateJson, result[0][5].as<std::string>())) {
							std::string clientJson = MV::toJson(a_connection->player()->client);
							a_connection->connection()->send(
								makeNetworkString<LoginResponse>("Successful login.", 
								(!saveHash.empty() && MV::sha512(clientJson) == saveHash ? std::string() : clientJson),
								true)
							);
							MV::info("Login Success: [", identifier, "]");
						} else {
							a_connection->connection()->send(makeNetworkString<LoginResponse>("Load Player Parse Fail: Contact Support"));
							MV::error("Parse Fail Load Player: [", identifier, "]\n___\n", playerStateJson, "\n___\n");
						}
					} else {
						a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to authenticate."));
						MV::info("Failed to authenticate: [", identifier, "] credential mismatch for existing user!\n[", hashedPassword, "]\n[", result[0][1].as<std::string>(), "]");
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
