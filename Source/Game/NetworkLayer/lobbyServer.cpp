#include "lobbyServer.h"
#include "networkAction.h"
#include "clientActions.h"


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

bool LobbyUserConnectionState::authenticate(const std::string& a_email, const std::string& a_name, const std::string &a_newState, const std::string &a_serverState) {
	try {
		ourPlayer = MV::fromJson<std::shared_ptr<ServerPlayer>>(a_serverState);
		ourPlayer->client = MV::fromJson<std::shared_ptr<Player>>(a_newState);
		ourPlayer->client->email = a_email;
		ourPlayer->client->handle = a_name;
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
			rightPlayer->queue.add(leftPlayer);
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
		AssignedPlayer(leftPlayer->player()->client, leftPlayer->secret),
		AssignedPlayer(rightPlayer->player()->client, rightPlayer->secret),
		rightPlayer->queue.id()));
}

void LobbyGameConnectionState::matchMade(std::shared_ptr<MatchSeeker> a_leftPlayer, std::shared_ptr<MatchSeeker> a_rightPlayer) {
	leftPlayer = a_leftPlayer;
	rightPlayer = a_rightPlayer;

	leftPlayer->secret = MV::randomInteger(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
	rightPlayer->secret = MV::randomInteger(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());

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
	std::lock_guard<std::mutex> guard(lock);

	auto pairs = getMatchPairs(a_dt);

	removeMatchedPairsFromSeekers(pairs);

	auto* gameServer = server->availableGameServer();
	for (auto && match : pairs) {
		if (!gameServer) {
			break;
		}
		if (auto firstConnection = match.first->lifespan.lock()) {
			if (auto secondConnection = match.second->lifespan.lock()) {
				try {
					gameServer->matchMade(match.first, match.second);
				} catch (...) {
					std::cerr << "Failed to match!" << std::endl;
				}
				gameServer = server->availableGameServer(); //find the next available game server.
			}
		}
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
