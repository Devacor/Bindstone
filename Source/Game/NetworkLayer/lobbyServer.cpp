#include "lobbyServer.h"
#include "serverActions.h"
#include "clientActions.h"

LobbyUserConnectionState::LobbyUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, LobbyServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void LobbyUserConnectionState::connectImplementation() {
	auto in = MV::toBinaryStringCast<ClientAction>(std::make_shared<ServerDetails>());
	std::cout << "SENDING:\n" << in << std::endl;
	auto out = MV::fromBinaryString<std::shared_ptr<ClientAction>>(in);
	std::cout << std::static_pointer_cast<ServerDetails>(out)->forceClientVersion << std::endl;

	connection()->send(in);
}

void LobbyUserConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<ServerUserAction>>(a_message);
	action->execute(this);
}

bool LobbyUserConnectionState::authenticate(const std::string& a_email, const std::string& a_name, const std::string &a_newState, const std::string &a_serverState) {
	try {
		ourPlayer = MV::fromJson<std::shared_ptr<ServerPlayer>>(a_serverState);
		ourPlayer->client = MV::fromJson<std::shared_ptr<Player>>(a_newState);
		ourPlayer->client->email = a_email;
		ourPlayer->client->handle = a_name;
		auto lockedConnection = connection();
		for (auto&& c : ourServer.server()->connections()) {
			if (lockedConnection != c && (static_cast<LobbyUserConnectionState*>(c->state())->player()->client->email == ourPlayer->client->email)) {
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

LobbyGameConnectionState::LobbyGameConnectionState(const std::shared_ptr<MV::Connection> &a_connection, LobbyServer& a_server) :
	MV::ConnectionStateBase(a_connection),
	ourServer(a_server) {
}

void LobbyGameConnectionState::connectImplementation() {
	auto in = MV::toBinaryStringCast<ClientAction>(std::make_shared<ServerDetails>());
	std::cout << "SENDING:\n" << in << std::endl;
	auto out = MV::fromBinaryString<std::shared_ptr<ClientAction>>(in);
	std::cout << std::static_pointer_cast<ServerDetails>(out)->forceClientVersion << std::endl;

	connection()->send(in);
}

void LobbyGameConnectionState::message(const std::string &a_message) {
	auto action = MV::fromBinaryString<std::shared_ptr<ServerGameAction>>(a_message);
	action->execute(this);
}

MatchSeeker::MatchSeeker(const std::shared_ptr<MV::Connection> &a_connection, MatchQueue& a_queue) :
	lifespan(a_connection),
	state(static_cast<LobbyUserConnectionState*>(a_connection->state())),
	queue(a_queue) {
}

MatchSeeker::~MatchSeeker() {
	queue.remove(this);
	//queue.remove(lockedPlayer);
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

	std::set<std::string> emailsToRemove;

	for (auto && match : pairs) {
		if (auto firstConnection = match.first->lifespan.lock()) {
			if (auto secondConnection = match.second->lifespan.lock()) {
				emailsToRemove.insert(match.first->player()->client->email);
				emailsToRemove.insert(match.second->player()->client->email);
				try {
					auto secret = MV::randomInteger(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
					auto response = MV::toBinaryStringCast<ClientAction>(std::make_shared<MatchedResponse>("TODO: Supply Server IP", secret));
					firstConnection->send(response);
					secondConnection->send(response);
				} catch (...) {

				}
			}
		}
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
