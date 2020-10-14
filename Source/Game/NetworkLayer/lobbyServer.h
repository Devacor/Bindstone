#ifndef _LOBBYSERVER_MV_H_
#define _LOBBYSERVER_MV_H_
#ifdef BINDSTONE_SERVER

#include "MV/Utility/package.h"
#include "MV/Network/package.h"
#include "Game/NetworkLayer/package.h"
#include "Game/managers.h"

#include "Game/player.h"

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <tuple>

#include <pqxx/pqxx>
#include <LINQ/boolinq.hpp>

#include "MV/Serialization/serialize.h"

class ServerUserAction;
class LobbyServer;
class MatchQueue;
class LobbyUserConnectionState;

struct MatchSeeker : public std::enable_shared_from_this<MatchSeeker> {
	MatchSeeker(const std::shared_ptr<MV::Connection> &a_connection, MatchQueue& a_queue);
	~MatchSeeker();

	void initialize();

	ServerPlayer* player();

	double tolerance() {
		if (time < 5.0) {
			return .025;
		} else if(time < 10.0) {
			return .05;
		} else if (time < 20.0) {
			return .075;
		} else if (time < 40.0) {
			return .10;
		} else if (time < 60.0) {
			return .15;
		} else if (time < 80.0) {
			return .25;
		} else {
			return 1.0; //any match.
		}
	}

	std::weak_ptr<MV::Connection> lifespan;

	double time = 0.0;
	bool matching = false;
	LobbyUserConnectionState* state;
	MatchQueue& queue;
	int64_t secret = 0;
};

bool operator<(MatchSeeker &a_lhs, MatchSeeker &a_rhs);

class LobbyUserConnectionState : public MV::ConnectionStateBase {
public:
	LobbyUserConnectionState(const std::shared_ptr<MV::Connection> &a_connection, LobbyServer& a_server);

	virtual void message(const std::string &a_message);

	LobbyServer& server() {
		return ourServer;
	}

	bool authenticated() const {
		return loggedIn;
	}

	bool authenticate(int64_t a_id, const std::string& a_email, const std::string& a_name, const std::string &a_newState, const std::string &a_serverState);

	std::string state() const {
		return activeState;
	}

	void state(const std::string &a_newState) {
		activeState = a_newState;
	}

	std::shared_ptr<ServerPlayer> player() {
		return ourPlayer;
	}

	std::shared_ptr<MatchSeeker> seekMatch(MatchQueue& a_queue);

	void cancelSeekMatch() {
		seeking.reset();
	}

	void save() {
		//commit player to db
	}

protected:
	virtual void connectImplementation() override;

private:
	std::string activeState;
	bool loggedIn = false;
	LobbyServer& ourServer;
	std::shared_ptr<ServerPlayer> ourPlayer;
	std::shared_ptr<MatchSeeker> seeking;
};

class LobbyGameConnectionState : public MV::ConnectionStateBase {
public:
	enum State {INITIALIZING, AVAILABLE, CONNECTING_PLAYERS, OCCUPIED};
	static const std::vector<std::string> states;

	LobbyGameConnectionState(const std::shared_ptr<MV::Connection> &a_connection, LobbyServer& a_server);
	~LobbyGameConnectionState() {
		handleExpiredPlayers();
	}
	virtual void message(const std::string &a_message);

	void matchMade(std::shared_ptr<MatchSeeker> a_leftPlayer, std::shared_ptr<MatchSeeker> a_rightPlayer);

	LobbyServer& server() {
		return ourServer;
	}

	State state() const {
		return activeState;
	}

	std::string stateString() const {
		return states[activeState];
	}

	void state(const State a_newState) {
		activeState = a_newState;
		if (activeState == AVAILABLE) {
			leftPlayer = nullptr;
			rightPlayer = nullptr;
		}
	}

	bool available() const {
		return activeState == AVAILABLE;
	}

	std::string url() const {
		return ourUrl;
	}

	uint16_t port() const {
		return ourPort;
	}

	void setEndpoint(const std::string &a_url, uint16_t a_port) {
		state(AVAILABLE);
		ourUrl = a_url;
		ourPort = a_port;
		std::cout << "GameServer Connected: " << a_url << ":" << a_port << std::endl;
	}

	void notifyPlayersOfGameServer();

	virtual void update(double a_dt) override;
protected:
	virtual void connectImplementation() override;

private:

	std::string queueType;
	State activeState = INITIALIZING;
	LobbyServer& ourServer;

	std::string ourUrl;
	uint16_t ourPort = 0;

	std::shared_ptr<MatchSeeker> leftPlayer;
	std::shared_ptr<MatchSeeker> rightPlayer;

	void notifyGameServerOfPlayers();

	bool handleExpiredPlayers();

};

class MatchQueue {
public:
	MatchQueue(LobbyServer& a_server, const std::string &a_id) :
		ourId(a_id),
		server(&a_server) {
	}

	void add(const std::weak_ptr<MatchSeeker> &a_seeker) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		MV::insertSorted(seekers, a_seeker);
	}

	void cull() {
		std::lock_guard<std::recursive_mutex> guard(lock);
		seekers.erase(std::remove_if(seekers.begin(), seekers.end(), [&](auto& seeker) {
			if (auto lockedSeeker = seeker.lock()) {
				return lockedSeeker->lifespan.expired();
			}
			return true;
		}), seekers.end());
	}

	void remove(MatchSeeker* a_removeSeeker) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		seekers.erase(std::remove_if(seekers.begin(), seekers.end(), [&](auto& seeker){
			if (auto lockedSeeker = seeker.lock()) {
				return lockedSeeker.get() == a_removeSeeker || lockedSeeker->lifespan.expired();
			} else {
				return true;
			}
		}), seekers.end());
	}

	void update(double a_dt);

	std::vector<std::pair<std::shared_ptr<MatchSeeker>, std::shared_ptr<MatchSeeker>>> getMatchPairs(double a_dt) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		std::vector<std::pair<std::shared_ptr<MatchSeeker>, std::shared_ptr<MatchSeeker>>> pairs;
		size_t i = 0;
		for (auto it = seekers.begin(); it != seekers.end();) {
			if (auto current = it->lock()) {
				if (auto lifespan = current->lifespan.lock()) {
					if (!current->matching) {
						current->time += a_dt;

						double currentBest; std::shared_ptr<MatchSeeker> opponent;
						std::tie(opponent, currentBest) = opponentFromIndex(i, current);

						if (opponent && (currentBest <= current->tolerance())) {
							if (auto opponentLifespan = opponent->lifespan.lock()) {
								std::cout << "Matching [" << current->player()->client->handle << "] vs [" << opponent->player()->client->handle << "]" << std::endl;
								current->matching = true;
								opponent->matching = true;
								pairs.emplace_back(current, opponent);
							}
						}
					}
					++it;
					++i;
					continue;
				}
			}

			//If we get here it means the seeker is expired, so just remove. Note: continue; above.
			it = seekers.erase(it);
		}
		return pairs;
	}

	std::tuple<std::shared_ptr<MatchSeeker>, double> opponentFromIndex(size_t i, const std::shared_ptr<MatchSeeker> &current) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		std::shared_ptr<MatchSeeker> opponent;
		double currentBest = 10.0;
		int remainingComparisons = PLAYERS_TO_SEARCH_FOR_MATCH;
		for (size_t j = i + 1; j < seekers.size() && remainingComparisons > 0; ++j) {
			if (auto seekerCompare = seekers[j].lock()) {
				if (auto seekerLifespan = seekerCompare->lifespan.lock()) {
					if (!seekerCompare->matching) {
						auto difference = current->player()->queue(ourId).skillDifference(seekerCompare->player()->queue(ourId));
						if (difference < currentBest) {
							currentBest = difference;
							opponent = seekerCompare;
						}
					}
				}
			}
		}
		return std::make_tuple(opponent, currentBest);
	}

	const std::string& id() const {
		return ourId;
	}

	void print() const;
private:
	void removeMatchedPairsFromSeekers(const std::vector<std::pair<std::shared_ptr<MatchSeeker>, std::shared_ptr<MatchSeeker>>>& pairs);

	const int PLAYERS_TO_SEARCH_FOR_MATCH = 7;

	std::recursive_mutex lock;
	std::vector<std::weak_ptr<MatchSeeker>> seekers;
	LobbyServer* server;
	std::string ourId;
};

class LobbyServer {
public:
	LobbyServer(Managers& a_managers);

	void update(double dt);

	Managers& managers() {
		return manager;
	}

	std::shared_ptr<pqxx::connection> database() {
		return db;
	}

	MV::ThreadPool& pool() {
		return threadPool;
	}

	std::shared_ptr<MV::Server> server() {
		return ourUserServer;
	}

	MV::ThreadPool& databasePool() {
		return dbPool;
	}

	void email(const MV::Email::Addresses &a_addresses, const std::string &a_title, const std::string &a_message) {
		auto emailer = MV::Email::make(emailPool.io_context(), "email-smtp.us-west-2.amazonaws.com", "587", { "AKIAIVINRAMKWEVUT6UQ", "AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw" });
		emailer->send(a_addresses, a_title, a_message);
	}

	MatchQueue& ranked() {
		return rankedQueue;
	}

	MatchQueue& normal() {
		return normalQueue;
	}

	MatchQueue& queue(const std::string &a_type) {
		if (a_type == rankedQueue.id()) {
			return rankedQueue;
		} else {
			return normalQueue;
		}
	}

	LobbyGameConnectionState* availableGameServer() {
		auto foundConnection = boolinq::from(ourGameServer->connections()).firstOrDefault([](const auto& connection) {
			return static_cast<LobbyGameConnectionState*>(connection->state())->available();
		});
		return (foundConnection) ? static_cast<LobbyGameConnectionState*>(foundConnection->state()) : nullptr;
	}

private:
	LobbyServer(const LobbyServer &) = delete;
	LobbyServer& operator=(const LobbyServer &) = delete;

	std::shared_ptr<pqxx::connection> db;
	std::shared_ptr<MV::Server> ourUserServer;
	std::shared_ptr<MV::Server> ourGameServer;

	MV::ThreadPool threadPool;
	MV::AsioThreadPool emailPool;
	MV::ThreadPool dbPool;

	MatchQueue rankedQueue;
	MatchQueue normalQueue;

	Managers &manager;
	bool done = false;

	double lastUpdateDelta = 0.0;
};
#endif
#endif
