/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MV_NETWORK_H_
#define _MV_NETWORK_H_

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <mutex>
#include <memory>
#include <set>
#include <utility>
#include <atomic>
#include <string>
#include "MV/Network/url.h"
#include "MV/Utility/generalUtility.h"
#include <asio.hpp>
#include "MV/Utility/log.h"

namespace MV {
	struct NetworkMessage {
		NetworkMessage() {
		}

		NetworkMessage(const std::string &a_content) :
			content(a_content) {
		}

		uint32_t sizeFromHeaderBuffer();

		void readHeaderFromBuffer();

		void readContentFromBuffer();

		void pushSizeToHeaderBuffer();

		std::string headerAndContent() const;

		uint32_t headerAndContentSize() const;
		const std::string& wire();

		asio::streambuf buffer;
		std::array<uint8_t, 4> headerBuffer = {0, 0, 0, 0};

		std::string content;
		std::string wireBytes;
	};


	inline void closeSocket(std::shared_ptr<asio::ip::tcp::socket> &socket) {
		if (socket) {
			if (socket->is_open()) {
				asio::error_code ec; //don't really care
				socket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
				socket->close();
			}
			socket.reset();
		}
	}

	class Client : public std::enable_shared_from_this<Client> {
	public:
		enum ConnectionState { DISCONNECTED, CONNECTING, CONNECTING_SUCCESS, CONNECTED };

		static std::shared_ptr<Client> make(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized = std::function<void ()>()) {
			auto self = std::shared_ptr<Client>(new Client(a_url, a_onMessageGet, a_onConnectionFail, a_onInitialized));
			self->initialize();
			return self;
		}

		~Client();

		void send(const std::string &a_content);

		void update();

		void reconnect() {
			initialize();
		}

		void reconnect(const std::function<void()> &a_onInitialized) {
			if (ourConnectionState == DISCONNECTED) {
				onInitialized = a_onInitialized;
				initialize();
			}
		}

		ConnectionState state() const{
			return ourConnectionState;
		}

		bool connected() const {
			return ourConnectionState == CONNECTED;
		}
		bool disconnected() const {
			return ourConnectionState == DISCONNECTED;
		}

		void disconnect();

	private:
		Client(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized) :
			url(a_url),
			resolver(ioService),
			onMessageGet(a_onMessageGet),
			onConnectionFail(a_onConnectionFail),
			onInitialized(a_onInitialized),
			work(std::make_unique<asio::io_context::work>(ioService)) {
		}

		void initialize();
		void tryInitializeCallback();

		void initiateConnection();

		void connectTo(const std::shared_ptr<asio::ip::tcp::socket>& a_attempt, asio::ip::tcp::resolver::iterator a_endpoints);
		void handleConnect(const std::shared_ptr<asio::ip::tcp::socket>& a_attempt, const asio::error_code& a_err, asio::ip::tcp::resolver::iterator a_nextEndpoint);

		void initiateRead();
		void writeNext();

		void handleError(const asio::error_code &a_err, const std::string &a_section);

		void closeConnection(const std::string &a_reason);
		void closeOnNetworkThread();
		void closeNow();

		std::string takeFailure();

		MV::Url url;

		std::mutex inboxLock;
		std::mutex errorLock;

		asio::io_context ioService;
		// The resolver, socket and outbox belong to the network thread; handlers capture this, never own it.
		asio::ip::tcp::resolver resolver;
		std::shared_ptr<asio::ip::tcp::socket> socket;
		std::deque<std::shared_ptr<NetworkMessage>> outbox;

		std::vector<std::shared_ptr<NetworkMessage>> inbox;

		std::function<void(const std::string &)> onMessageGet;

		std::string failMessage;
		std::function<void(const std::string &)> onConnectionFail;

		std::function<void()> onInitialized;

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<asio::io_context::work> work;

		std::vector<double> serverTimeDeltas;
		std::atomic<ConnectionState> ourConnectionState = DISCONNECTED;
		// Touched only by the thread that calls update() and the public methods.
		bool connectionAnnounced = false;
		bool disconnectRequested = false;
	};

	class Server;
	class Connection;

	//derive from this in your own code.
	class ConnectionStateBase {
	public:
		ConnectionStateBase(const std::shared_ptr<Connection>& a_connection) :
			ourConnection(a_connection) {

			connectImplementation();
		}

		virtual ~ConnectionStateBase() = default;

		void disconnect() {
			if (!disconnectRecieved) {
				disconnectRecieved = true;
				disconnectImplementation();
			}
		}

		bool disconnected() const { return disconnectRecieved; }

		virtual void message(const std::string& a_message) { }

		virtual void update(double a_dt) { }

		//Get a shared_ptr handle keep the connection alive in long running requests even if disconnected.
		std::shared_ptr<Connection> connection() { return ourConnection.lock(); }
	protected:
		virtual void connectImplementation() {}
		virtual void disconnectImplementation() {}

		std::weak_ptr<Connection> ourConnection;
		bool disconnectRecieved = false;
	};

	// The network-thread half of a Connection: its handlers own this, never the Connection, so the
	// Connection and its game state die on the thread that updates the Server.
	class ConnectionSocket : public std::enable_shared_from_this<ConnectionSocket> {
	public:
		ConnectionSocket(const std::shared_ptr<asio::ip::tcp::socket> &a_socket, asio::io_context& a_ioService);

		void start();
		void send(const std::string &a_content);
		void close();

		std::vector<std::shared_ptr<NetworkMessage>> takeInbox();

		bool failed() const {
			return ourFailed;
		}

		bool loopback() const {
			return ourLoopback;
		}

	private:
		void initiateRead();
		void writeNext();
		void closeNow();
		void handleError(const asio::error_code &a_err, const std::string &a_section);

		asio::io_context& ioService;
		std::shared_ptr<asio::ip::tcp::socket> socket;
		std::deque<std::shared_ptr<NetworkMessage>> outbox;
		bool closing = false;
		asio::steady_timer closeDeadline;

		std::mutex inboxLock;
		std::vector<std::shared_ptr<NetworkMessage>> inbox;

		std::atomic<bool> ourFailed {false};
		const bool ourLoopback;
	};

	class Connection : public std::enable_shared_from_this<Connection> {
	public:
		Connection(const std::shared_ptr<ConnectionSocket> &a_socket) :
			socket(a_socket) {
		}

		//can't happen in the constructor due to shared_from_this
		void initialize(std::function<std::unique_ptr<ConnectionStateBase>(const std::shared_ptr<Connection> &)> a_connectionStateFactory) {
			ourState = a_connectionStateFactory(shared_from_this());
		}

		~Connection() {
			info("Connection::~Connection()");
			if (ourState) {
				ourState->disconnect();
			}
			socket->close();
		}

		void send(const std::string &a_content) {
			socket->send(a_content);
		}

		size_t update(double a_dt);

		ConnectionStateBase* state() const {
			return ourState.get();
		}

		void disconnect() {
			ourState->disconnect();
			socket->close();
		}

		bool disconnected() const {
			return ourState->disconnected();
		}
		// The peer is this machine (a locally hosted stack), as seen by the socket itself
		bool loopback() const {
			return socket->loopback();
		}

	private:
		std::shared_ptr<ConnectionSocket> socket;
		std::unique_ptr<ConnectionStateBase> ourState = nullptr;
	};

	class DataSentAggregate {
	public:
		inline void add(double a_timeStamp, size_t a_sent) {
			packets.push_back({ a_timeStamp, a_sent });
			dirty = true;
		}

		inline size_t bytesPerSecond() {
			if (dirty && packets.size() > 1) {
				auto last = packets.back();
				while (!packets.empty() && (last.first - packets.front().first) > 1.0) {
					packets.pop_front();
				}
				currentAmount = 0;
				for(auto&& packet : packets){
					currentAmount+=packet.second;
				}
				dirty = false;
			}
			return currentAmount;
		}
	private:
		bool dirty = false;
		size_t currentAmount = 0;
		std::deque<std::pair<double, size_t>> packets;
	};

	// Connections are accepted on the network thread and adopted, updated and dropped by update(); every
	// other member is used only from the thread that calls update().
	class Server {
	public:
		Server(const asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<ConnectionStateBase> (const std::shared_ptr<Connection> &)> a_connectionStateFactory);

		~Server();

		void sendAll(const std::string &a_message);
		void sendExcept(const std::string &a_message, Connection* a_connectionToSkip);
		void update(double a_dt);

		std::vector<std::shared_ptr<Connection>>& connections() {
			return ourConnections;
		}

		uint16_t port() const {
			return ourPort;
		}

		size_t bytesPerSecondSent() {
			return sent.bytesPerSecond();
		}

		size_t bytesPerSecondReceived() {
			return received.bytesPerSecond();
		}
	private:
		void acceptClients();
		void adoptAcceptedConnections();

		asio::io_context ioService;
 		asio::ip::tcp::acceptor acceptor;
		uint16_t ourPort = 0;

		std::mutex acceptedLock;
		std::vector<std::shared_ptr<ConnectionSocket>> accepted;

		std::vector<std::shared_ptr<Connection>> ourConnections;

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<asio::io_context::work> work;

		std::function<std::unique_ptr<ConnectionStateBase> (const std::shared_ptr<Connection> &)> connectionStateFactory;
		double accumulatedTime = 0.0;

		DataSentAggregate sent;
		DataSentAggregate received;
	};
};

#endif
