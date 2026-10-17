#include "accountActions.h"
#include "clientActions.h"

#ifdef BINDSTONE_SERVER
#include "lobbyServer.h"
#endif

#include "Game/player.h"
#include "MV/Serialization/serialize.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

static jai::registrar<CreatePlayer, MV::Services, jai::bases<NetworkAction>> _regCreatePlayer("CreatePlayer");
static jai::registrar<LoginRequest, MV::Services, jai::bases<NetworkAction>> _regLoginRequest("LoginRequest",
	[](jai::dynamic_binder<LoginRequest>& builder, const MV::Services&) {
	builder.constructor<std::string, std::string>();
	builder.method("toNetworkString", [](LoginRequest& a_self) { return a_self.toNetworkString(); });
});
static jai::registrar<FindMatchRequest, MV::Services, jai::bases<NetworkAction>> _regFindMatchRequest("FindMatchRequest",
	[](jai::dynamic_binder<FindMatchRequest>& builder, const MV::Services&) {
	builder.constructor<std::string>();
	builder.method("toNetworkString", [](FindMatchRequest& a_self) { return a_self.toNetworkString(); });
});
static jai::registrar<ExpectedPlayersNoted, MV::Services, jai::bases<NetworkAction>> _regExpectedPlayersNoted("ExpectedPlayersNoted");


#ifdef BINDSTONE_SERVER
namespace {
	// Filled on the database thread, read by the continuation the pool runs on the lobby's main thread.
	struct AccountLookup {
		enum Outcome { FAILED, NO_PLAYER, CREATED, UNVERIFIED, AUTHENTICATED, CREDENTIAL_MISMATCH };
		Outcome outcome = FAILED;
		int64_t id = -1;
		std::string email;
		std::string handle;
		std::string state;
		std::string serverState;
		std::string passSalt;
	};

	bool readAuthenticatedPlayer(const pqxx::result& a_result, const std::string& a_password, AccountLookup& a_lookup) {
		auto hashedPassword = MV::sha512(a_password, a_result[0][2].as<std::string>(), a_result[0][3].as<int>());
		if (hashedPassword != a_result[0][1].as<std::string>()) {
			return false;
		}
		a_lookup.outcome = AccountLookup::AUTHENTICATED;
		a_lookup.state = a_result[0][4].as<std::string>();
		a_lookup.serverState = a_result[0][5].as<std::string>();
		a_lookup.email = a_result[0][6].as<std::string>();
		a_lookup.handle = a_result[0][7].as<std::string>();
		a_lookup.id = a_result[0][8].as<int64_t>();
		return true;
	}

	LobbyUserConnectionState* liveLobbyUser(const std::weak_ptr<MV::Connection>& a_connection) {
		auto connection = a_connection.lock();
		if (!connection || connection->disconnected()) {
			return nullptr;
		}
		return static_cast<LobbyUserConnectionState*>(connection->state());
	}
}

std::string CreatePlayer::createPlayerQueryString(pqxx::work &transaction, const std::string &a_salt, const std::string &a_saveState, const std::string &a_serverSaveState) {
	static int work = 12;
	std::stringstream query;
	query << "INSERT INTO players(email, handle, passhash, passsalt, passiterations, state, serverstate)";
	query << "VALUES(" << transaction.quote(email) << ", " << transaction.quote(handle) << ", ";
	query << transaction.quote(MV::sha512(password, a_salt, work)) << ", " << transaction.quote(a_salt) << ", " << work << ", ";
	query << transaction.quote(a_saveState) << ", " << transaction.quote(a_serverSaveState) << ");";
	return query.str();
}

pqxx::result CreatePlayer::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passhash, passsalt, passiterations, state, serverstate, email, handle, id FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(email);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(handle) + ";";

	return a_transaction->exec(activeQuery);
}

// Only the query and the hashing run on the database thread: the script engine, the shared RNG, the
// connections and the player state all belong to the lobby's main thread.
void CreatePlayer::execute(LobbyUserConnectionState* a_connection) {
	if (email.empty() || !validateHandle(handle) || password.size() < 8) {
		a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to supply a valid email/handle and password."));
		return;
	}
	auto self = std::static_pointer_cast<CreatePlayer>(shared_from_this());
	std::weak_ptr<MV::Connection> connectionLifespan = a_connection->connection();
	auto salt = MV::randomString(32);
	auto saveState = makeSaveString();
	auto serverSaveState = makeServerSaveString();
	auto lookup = std::make_shared<AccountLookup>();
	a_connection->server().databasePool().task(MV::ThreadPool::Job([self, lookup, salt, saveState, serverSaveState, database = a_connection->server().database()] {
		try {
			pqxx::work transaction(*database);
			auto result = self->selectUser(&transaction);
			if (result.empty()) {
				transaction.exec(self->createPlayerQueryString(transaction, salt, saveState, serverSaveState));
				lookup->outcome = AccountLookup::CREATED;
			} else if (!result[0][0].as<bool>()) {
				lookup->outcome = AccountLookup::UNVERIFIED;
				lookup->passSalt = result[0][2].as<std::string>();
			} else if (!readAuthenticatedPlayer(result, self->password, *lookup)) {
				lookup->outcome = AccountLookup::CREDENTIAL_MISMATCH;
			}
			transaction.commit();
		} catch (std::exception& e) {
			lookup->outcome = AccountLookup::FAILED;
			MV::error("Failed to execute CreatePlayer: [", self->email, "]\nWHAT: [", e.what(), "]");
		}
	}, [self, lookup, connectionLifespan, salt] {
		auto* state = liveLobbyUser(connectionLifespan);
		if (!state) {
			return;
		}
		switch (lookup->outcome) {
			case AccountLookup::CREATED:
				state->connection()->send(makeNetworkString<MessageAction>("User created, woot!"));
				self->sendValidationEmail(state, salt);
			break;
			case AccountLookup::UNVERIFIED:
				state->connection()->send(makeNetworkString<LoginResponse>("Player not email validated yet."));
				self->sendValidationEmail(state, lookup->passSalt);
				MV::info("Need Validation: [", self->email, "]");
			break;
			case AccountLookup::AUTHENTICATED:
				if (state->authenticate(lookup->id, lookup->email, lookup->handle, lookup->state, lookup->serverState)) {
					state->connection()->send(makeNetworkString<LoginResponse>("Successful login.", lookup->state, true));
					MV::info("Login Success: [", self->email, "]");
				} else {
					state->connection()->send(makeNetworkString<LoginResponse>("Load Player Parse Fail: Contact Support"));
					MV::error("Parse Fail Load Player: [", self->email, "]\n___\n", lookup->state, "\n___\n");
				}
			break;
			case AccountLookup::CREDENTIAL_MISMATCH:
				state->connection()->send(makeNetworkString<LoginResponse>("Failed to create due to existing player with different credentials."));
				MV::info("Failed to create: [", self->email, "] credential mismatch for existing user!");
			break;
			default:
			break;
		}
	}, MV::ThreadPool::Job::Continue::MAIN_THREAD));
}

bool CreatePlayer::ensureAccount(pqxx::connection &a_db, const std::string &a_email, const std::string &a_handle, const std::string &a_password) {
	CreatePlayer action(a_email, a_handle, a_password);
	pqxx::work transaction(a_db);
	if (!action.selectUser(&transaction).empty()) {
		return false;
	}
	transaction.exec(action.createPlayerQueryString(transaction, MV::randomString(32), makeSaveString(), makeServerSaveString()));
	transaction.commit();
	return true;
}

void CreatePlayer::sendValidationEmail(LobbyUserConnectionState *a_connection, const std::string &a_passSalt) {
	a_connection->server().email(MV::Email::Addresses("mike@m2tm.net", email), "Bindstone Account Activation", "This will be a link to activate your account, for now... Check this out: https://www.youtube.com/watch?v=VAZsBEELqPw \n" + a_passSalt);
}
#endif

std::string CreatePlayer::makeSaveString() {
	IntermediateDbPlayer newPlayer;
	newPlayer.loadouts["Default"].buildings = { "life", "decay", "void", "earth", "fire", "water", "air", "lightning" };
	newPlayer.loadouts["Default"].skins = { "", "", "", "", "", "", "", "" };
	newPlayer.selectedLoadout = "Default";

	newPlayer.wallet.add(Wallet::CurrencyType::SOFT, DEFAULT_SOFT_CURRENCY);
	newPlayer.wallet.add(Wallet::CurrencyType::HARD, DEFAULT_HARD_CURRENCY);
	return MV::toJsonInline(newPlayer, MV::Services::instance());
}

std::string CreatePlayer::makeServerSaveString() {
	ServerPlayer newPlayer;
	return MV::toJsonInline(newPlayer, MV::Services::instance());
}

#ifdef BINDSTONE_SERVER
pqxx::result LoginRequest::selectUser(pqxx::work* a_transaction) {
	std::string activeQuery = "SELECT verified, passhash, passsalt, passiterations, state, serverstate, email, handle, id FROM players WHERE ";
	activeQuery += "email = " + a_transaction->quote(identifier);
	activeQuery += " OR ";
	activeQuery += "handle = " + a_transaction->quote(identifier);

	return a_transaction->exec(activeQuery);
}

void LoginRequest::execute(LobbyUserConnectionState* a_connection) {
	if (identifier.empty() || password.size() < 8) {
		a_connection->connection()->send(makeNetworkString<LoginResponse>("Failed to supply a valid email/handle and password."));
		return;
	}
	auto self = std::static_pointer_cast<LoginRequest>(shared_from_this());
	std::weak_ptr<MV::Connection> connectionLifespan = a_connection->connection();
	auto lookup = std::make_shared<AccountLookup>();
	a_connection->server().databasePool().task(MV::ThreadPool::Job([self, lookup, database = a_connection->server().database()] {
		try {
			pqxx::work transaction(*database);
			auto result = self->selectUser(&transaction);
			if (result.empty()) {
				lookup->outcome = AccountLookup::NO_PLAYER;
			} else if (!result[0][0].as<bool>()) {
				lookup->outcome = AccountLookup::UNVERIFIED;
			} else if (!readAuthenticatedPlayer(result, self->password, *lookup)) {
				lookup->outcome = AccountLookup::CREDENTIAL_MISMATCH;
			}
			transaction.commit();
		} catch (std::exception& e) {
			lookup->outcome = AccountLookup::FAILED;
			MV::info("Failed to execute LoginRequest: [", self->identifier, "]\nWHAT: [", e.what(), "]");
		}
	}, [self, lookup, connectionLifespan] {
		auto* state = liveLobbyUser(connectionLifespan);
		if (!state) {
			return;
		}
		switch (lookup->outcome) {
			case AccountLookup::NO_PLAYER:
				state->connection()->send(makeNetworkString<LoginResponse>("No player exists."));
			break;
			case AccountLookup::UNVERIFIED:
				state->connection()->send(makeNetworkString<LoginResponse>("Player not email validated yet."));
				MV::info("Need Validation: [", self->identifier, "]");
			break;
			case AccountLookup::AUTHENTICATED:
				if (state->authenticate(lookup->id, lookup->email, lookup->handle, lookup->state, lookup->serverState)) {
					std::string clientJson = MV::toJson(state->player()->client, state->server().managers().services);
					state->connection()->send(
						makeNetworkString<LoginResponse>("Successful login.",
						(!self->saveHash.empty() && MV::sha512(clientJson) == self->saveHash ? std::string() : clientJson),
						true)
					);
					MV::info("Login Success: [", self->identifier, "]");
				} else {
					state->connection()->send(makeNetworkString<LoginResponse>("Load Player Parse Fail: Contact Support"));
					MV::error("Parse Fail Load Player: [", self->identifier, "]\n___\n", lookup->state, "\n___\n");
				}
			break;
			case AccountLookup::CREDENTIAL_MISMATCH:
				state->connection()->send(makeNetworkString<LoginResponse>("Failed to authenticate."));
				MV::info("Failed to authenticate: [", self->identifier, "] credential mismatch for existing user!");
			break;
			default:
			break;
		}
	}, MV::ThreadPool::Job::Continue::MAIN_THREAD));
}

void FindMatchRequest::execute(LobbyUserConnectionState* a_connection) {
	if (!a_connection->authenticated()) {
		a_connection->connection()->send(makeNetworkString<IllegalResponse>("Not logged in."));
		return;
	}
	a_connection->state("finding");
	a_connection->seekMatch(a_connection->server().queue(type));
}

void ExpectedPlayersNoted::execute(LobbyGameConnectionState* a_connection) {
	a_connection->notifyPlayersOfGameServer();
}
#endif
