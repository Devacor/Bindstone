#ifdef BINDSTONE_SERVER
#include "lobbyServer.h"
#include "networkAction.h"
#include "clientActions.h"

#include <conio.h>

LobbyUserConnectionState::LobbyUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, LobbyServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void LobbyUserConnectionState::connectImplementation() {
	connection()->send(makeNetworkString<ServerDetails>());
}

void LobbyUserConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
	action->execute(this);
}

bool LobbyUserConnectionState::authenticate(int64_t a_id, const std::string& a_email, const std::string& a_name, const std::string &a_newState, const std::string &a_serverState) {
	try {
		ourPlayer = std::make_shared<ServerPlayer>(MV::fromJsonInline<ServerPlayer>(a_serverState));
		ourPlayer->client = std::make_shared<LocalPlayer>(a_id, a_email, a_name, MV::fromJsonInline<IntermediateDbPlayer>(a_newState));
		auto lockedConnection = connection();
		auto connectionList = ourServer.server()->connections();
		for (auto&& c : connectionList) {
			auto* connectionState = static_cast<LobbyUserConnectionState*>(c->state());
			if (lockedConnection != c && (connectionState->player() && connectionState->player()->client->email == ourPlayer->client->email)) {
				c->send(makeNetworkString<IllegalResponse>("Logged in elsewhere."));
				c->disconnect();
				break;
			}
		}
		loggedIn = true;
		return true;
	} catch (...) {
		std::cerr << "Failed to authenticate user!" << std::endl;
		return false;
	}
}

std::shared_ptr<MatchSeeker> LobbyUserConnectionState::seekMatch(MatchQueue& a_queue) {
	if (auto strongConnection = connection()) {
		seeking = std::make_shared<MatchSeeker>(strongConnection, a_queue);
		seeking->initialize(); //must be called due to shared_from_this
		return seeking;
	}
	return std::shared_ptr<MatchSeeker>();
}

const std::vector<std::string> LobbyGameConnectionState::states = { "INITIALIZING", "AVAILABLE", "CONNECTING_PLAYERS", "OCCUPIED" };

LobbyGameConnectionState::LobbyGameConnectionState(const std::shared_ptr<MV::Connection> &a_connection, LobbyServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void LobbyGameConnectionState::connectImplementation() {
	connection()->send(makeNetworkString<ServerDetails>());
}

bool LobbyGameConnectionState::handleExpiredPlayers() {
	if (leftPlayer->lifespan.expired() || rightPlayer->lifespan.expired()) {
		activeState = AVAILABLE;
		if (!leftPlayer->lifespan.expired()) {
			leftPlayer->queue.add(leftPlayer);
			leftPlayer.reset();
		}
                if (!rightPlayer->lifespan.expired()) {
                        rightPlayer->queue.add(rightPlayer);
                        rightPlayer.reset();
                }
		return true;
	}
	return false;
}

void LobbyGameConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<NetworkAction>>(a_message);
	action->execute(this);
}

void LobbyGameConnectionState::notifyGameServerOfPlayers() {
	activeState = CONNECTING_PLAYERS;

	connection()->send(makeNetworkString<AssignPlayersToGame>(
		AssignedPlayer{ leftPlayer->player()->client, leftPlayer->secret },
		AssignedPlayer{ rightPlayer->player()->client, rightPlayer->secret },
		rightPlayer->queue.id()));
}

void LobbyGameConnectionState::matchMade(std::shared_ptr<MatchSeeker> a_leftPlayer, std::shared_ptr<MatchSeeker> a_rightPlayer) {
	leftPlayer = a_leftPlayer;
	rightPlayer = a_rightPlayer;

	leftPlayer->secret = MV::randomInteger(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
	rightPlayer->secret = MV::randomInteger(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
	while (rightPlayer->secret == leftPlayer->secret) {
		rightPlayer->secret = MV::randomInteger(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
	}

	notifyGameServerOfPlayers();
}

void LobbyGameConnectionState::notifyPlayersOfGameServer() {
	if (handleExpiredPlayers()) {
		return;
	}

	leftPlayer->lifespan.lock()->send(makeNetworkString<MatchedResponse>(url(), port(), leftPlayer->secret));
	rightPlayer->lifespan.lock()->send(makeNetworkString<MatchedResponse>(url(), port(), rightPlayer->secret));
}

void LobbyGameConnectionState::update(double /*a_dt*/) {
}

MatchSeeker::MatchSeeker(const std::shared_ptr<MV::Connection> &a_connection, MatchQueue& a_queue) :
	lifespan(a_connection),
	state(static_cast<LobbyUserConnectionState*>(a_connection->state())),
	queue(a_queue) {
}

void MatchSeeker::initialize() {
	state->state("seeking");
	queue.add(shared_from_this());
}

MatchSeeker::~MatchSeeker() {
	queue.remove(this);
}

ServerPlayer* MatchSeeker::player() {
	return state->player().get();
}

bool operator<(MatchSeeker &a_lhs, MatchSeeker &a_rhs) {
	auto* lhsPlayer = a_lhs.player();
	auto* rhsPlayer = a_rhs.player();
	double lhsRating = !lhsPlayer ? 0.0 : lhsPlayer->queue(a_lhs.queue.id()).rating;
	double rhsRating = !rhsPlayer ? 0.0 : rhsPlayer->queue(a_rhs.queue.id()).rating;
	return lhsRating < rhsRating;
}

void MatchQueue::update(double a_dt) {
	std::lock_guard<std::recursive_mutex> guard(lock);

	auto pairs = getMatchPairs(a_dt);

	removeMatchedPairsFromSeekers(pairs);

	auto* gameServer = server->availableGameServer();
	for (auto && match : pairs) {
		if (!gameServer) {
			break;
		}
		try {
			gameServer->matchMade(match.first, match.second);
		} catch (...) {
			std::cerr << "Failed to match!" << std::endl;
		}
		gameServer = server->availableGameServer(); //find the next available game server.
	}
}

void MatchQueue::removeMatchedPairsFromSeekers(const std::vector<std::pair<std::shared_ptr<MatchSeeker>, std::shared_ptr<MatchSeeker>>>& pairs) {
	std::set<std::string> emailsToRemove;
	for (auto && match : pairs) {
		match.first->matching = false;
		match.second->matching = false;
		emailsToRemove.insert(match.first->player()->client->email);
		emailsToRemove.insert(match.second->player()->client->email);
	}

	seekers.erase(std::remove_if(seekers.begin(), seekers.end(), [&](auto& seeker) {
		if (auto lockedSeeker = seeker.lock()) {
			return lockedSeeker->lifespan.expired() || emailsToRemove.find(lockedSeeker->player()->client->email) != emailsToRemove.end();
		} else {
			return true;
		}
	}), seekers.end());
}

void MatchQueue::print() const {
	for (auto&& seeker : seekers) {
		if (auto lockedSeeker = seeker.lock()) {
			std::cout << lockedSeeker->player()->client->email << ":\t\t" << lockedSeeker->time << "\n";
		}
	}
}

LobbyServer::LobbyServer(Managers& a_managers) :
	manager(a_managers),
	//db(std::make_shared<pqxx::connection>("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=3306 dbname=bindstone user=m2tm password=Tinker123")),
	//db(std::make_shared<pqxx::connection>("host=localhost port=3306 dbname=bindstone user=m2tm password=Tinker123")),
	emailPool(1), //need to test values greater than 1 to make sure ssh does not break.
	dbPool(1), //currently locked to 1 as pqxx requires one per thread. We can expand this later with more connections and a different query interface.
	rankedQueue(*this, "ranked"),
	normalQueue(*this, "normal"),
	ourUserServer(std::make_shared<MV::Server>(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 22325),
		[this](const std::shared_ptr<MV::Connection>& a_connection) {
			return std::make_unique<LobbyUserConnectionState>(a_connection, *this);
		})),
	ourGameServer(std::make_shared<MV::Server>(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 22326),
		[this](const std::shared_ptr<MV::Connection>& a_connection) {
			return std::make_unique<LobbyGameConnectionState>(a_connection, *this);
		})) {

	MV::info("Initialize DB");
	db = std::make_shared<pqxx::connection>("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=3306 dbname=bindstone user=m2tm password=Tinker123");
	MV::info("DB Connected");
}

void LobbyServer::update(double dt) {
	ourUserServer->update(dt);
	ourGameServer->update(dt);
	rankedQueue.update(dt);
	normalQueue.update(dt);
	threadPool.run();
	emailPool.run();
	dbPool.run();

	if (_kbhit()) {
		switch (_getch()) {
		case 'q':
			std::cout << "Ranked Queue: " << std::endl;
			rankedQueue.print();
			std::cout << "Normal Queue: " << std::endl;
			normalQueue.print();
			break;
		case 'c':
			std::cout << "Connections: " << ourUserServer->connections().size() << std::endl;
			break;
		case 'g':
			for (auto&& gs : ourGameServer->connections()) {
				auto* gsState = static_cast<LobbyGameConnectionState*>(gs->state());
				std::cout << "Game Server: " << gsState->url() << ":" << gsState->port() << " - " << gsState->stateString() << std::endl;
			}
			break;
		}
	}
}
#endif
