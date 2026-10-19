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
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include "MV/Network/url.h"
#include "MV/Utility/generalUtility.h"
#include <asio.hpp>
#include "MV/Utility/log.h"

namespace MV {
	struct MessageLimits {
		uint32_t authenticatedMaxMessageBytes {2 * 1024 * 1024};
		uint32_t unauthenticatedMaxMessageBytes {32 * 1024};
		size_t authenticatedMaxUnreadBytes {8 * 1024 * 1024};
		size_t unauthenticatedMaxUnreadBytes {512 * 1024};
		std::chrono::milliseconds unreadTimeout {std::chrono::seconds(30)};

		uint32_t maxMessageBytes(bool a_authenticated) const {
			return a_authenticated ? authenticatedMaxMessageBytes : unauthenticatedMaxMessageBytes;
		}

		size_t maxUnreadBytes(bool a_authenticated) const {
			return a_authenticated ? authenticatedMaxUnreadBytes : unauthenticatedMaxUnreadBytes;
		}
	};

	// A header with this bit set frames a ProtocolFrame of at most MAX_PROTOCOL_FRAME_BYTES, never game content.
	inline constexpr uint32_t PROTOCOL_FRAME_BIT = 0x80000000;
	inline constexpr uint32_t MAX_PROTOCOL_FRAME_BYTES = 1024;

	enum class ProtocolCode : uint8_t { MESSAGE_LIMIT = 1, MESSAGE_TOO_LARGE, MESSAGE_TIMEOUT, INBOX_TIMEOUT };

	// What the network layer itself tells a peer: its message limit once authenticated, or why it is being closed.
	struct ProtocolFrame {
		ProtocolCode code {ProtocolCode::MESSAGE_LIMIT};
		// The limit in bytes for MESSAGE_LIMIT and MESSAGE_TOO_LARGE, the timeout in ms for the timeouts.
		uint32_t value {0};
		std::string reason;

		bool rejection() const {
			return code != ProtocolCode::MESSAGE_LIMIT;
		}

		std::string encode() const;
		static std::optional<ProtocolFrame> decode(const std::string &a_payload);
	};

	struct NetworkMessage {
		NetworkMessage() {
		}

		NetworkMessage(const std::string &a_content) :
			content(a_content) {
		}

		static std::shared_ptr<NetworkMessage> make(const ProtocolFrame &a_frame);

		uint32_t headerValue() const;

		uint32_t headerAndContentSize() const;

		// The buffers point into this message, which must outlive the write.
		std::array<asio::const_buffer, 2> wire();

		std::array<uint8_t, 4> headerBuffer {0, 0, 0, 0};
		std::string content;
		bool protocol = false;
	};

	class MessageTooLargeException : public DeviceException {
	public:
		MessageTooLargeException(size_t a_size, uint32_t a_limit) :
			std::runtime_error(describe(a_size, a_limit)),
			DeviceException(describe(a_size, a_limit)),
			ourSize(a_size),
			ourLimit(a_limit) {
		}

		virtual const char* what() const noexcept override {
			prefixWhat("Message Too Large Exception: ");
			return combinedWhat.c_str();
		}

		size_t size() const {
			return ourSize;
		}

		uint32_t limit() const {
			return ourLimit;
		}

	private:
		static std::string describe(size_t a_size, uint32_t a_limit) {
			return "Refused to send a " + std::to_string(a_size) + " byte message: the server accepts at most " + std::to_string(a_limit) + " bytes.";
		}

		size_t ourSize;
		uint32_t ourLimit;
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

		static std::shared_ptr<Client> make(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized = std::function<void ()>(), const MessageLimits& a_limits = {}) {
			auto self = std::shared_ptr<Client>(new Client(a_url, a_onMessageGet, a_onConnectionFail, a_onInitialized, a_limits));
			self->initialize();
			return self;
		}

		~Client();

		// Throws MessageTooLargeException, sending nothing, for a message over the server's limit.
		void send(const std::string &a_content);

		// Each connection assumes the unauthenticated limit until the server announces its own.
		uint32_t serverMaxMessageBytes() const {
			return ourServerMaxMessageBytes;
		}

		void serverMaxMessageBytes(uint32_t a_maxMessageBytes) {
			ourServerMaxMessageBytes = a_maxMessageBytes;
		}

		// A received message over authenticatedMaxMessageBytes fails the connection.
		const MessageLimits& limits() const {
			return ourLimits;
		}

		// Runs from update() when the server rejects this connection, before the failure callback, which then
		// carries the rejection's reason.
		void onRejected(const std::function<void(const ProtocolFrame &)> &a_onRejected) {
			onRejectedCallback = a_onRejected;
		}

		// The rejection the server sent on the current (or last) connection.
		const std::optional<ProtocolFrame>& rejection() const {
			return ourRejection;
		}

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
		Client(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized, const MessageLimits& a_limits) :
			url(a_url),
			ourLimits(a_limits),
			resolver(ioService),
			onMessageGet(a_onMessageGet),
			onConnectionFail(a_onConnectionFail),
			onInitialized(a_onInitialized),
			work(std::make_unique<asio::io_context::work>(ioService)),
			ourServerMaxMessageBytes(a_limits.unauthenticatedMaxMessageBytes) {
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
		void receiveProtocolFrame(const std::string &a_payload);

		MV::Url url;
		const MessageLimits ourLimits;

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
		std::function<void(const ProtocolFrame &)> onRejectedCallback;

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<asio::io_context::work> work;

		std::vector<double> serverTimeDeltas;
		std::atomic<ConnectionState> ourConnectionState = DISCONNECTED;
		// Touched only by the thread that calls update() and the public methods.
		bool connectionAnnounced = false;
		bool disconnectRequested = false;
		uint32_t ourServerMaxMessageBytes;
		std::optional<ProtocolFrame> ourRejection;
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
		ConnectionSocket(const std::shared_ptr<asio::ip::tcp::socket> &a_socket, asio::io_context& a_ioService, const MessageLimits& a_limits);

		void start();
		void send(const std::string &a_content);
		void close();

		// Raises this socket to the authenticated limits and announces the new message limit to the peer, once.
		void authenticate();

		bool authenticated() const {
			return ourAuthenticated;
		}

		const MessageLimits& limits() const {
			return ourLimits;
		}

		std::vector<std::shared_ptr<NetworkMessage>> takeInbox();

		bool failed() const {
			return ourFailed;
		}

		bool loopback() const {
			return ourLoopback;
		}

	private:
		void queue(const std::shared_ptr<NetworkMessage> &a_message);
		void push(const std::shared_ptr<NetworkMessage> &a_message);
		void initiateRead();
		void stopReading();
		void receive(const std::shared_ptr<NetworkMessage> &a_message);
		void resumeBelowCap();
		void resumeReading();
		void armUnreadDeadline();
		void disarmUnreadDeadline();
		void unreadDeadlineReached();
		void reject(ProtocolCode a_code, uint32_t a_value, const std::string &a_reason);
		void writeNext();
		void beginClose();
		void linger();
		void discardNext();
		void closeNow();
		void handleError(const asio::error_code &a_err, const std::string &a_section);

		asio::io_context& ioService;
		std::shared_ptr<asio::ip::tcp::socket> socket;
		std::deque<std::shared_ptr<NetworkMessage>> outbox;
		bool closing = false;
		bool lingering = false;
		asio::steady_timer closeDeadline;
		std::vector<char> discarded;

		const MessageLimits ourLimits;
		std::atomic<bool> ourAuthenticated {false};
		bool reading = false;
		bool awaitingContent = false;
		asio::steady_timer unreadDeadline;

		std::mutex inboxLock;
		std::vector<std::shared_ptr<NetworkMessage>> inbox;
		size_t unreadBytes = 0;
		bool readingPaused = false;

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

		// The game layer calls this once the peer proved who it is: the authenticated limits apply and the peer is told its new message limit.
		void authenticate() {
			socket->authenticate();
		}

		bool authenticated() const {
			return socket->authenticated();
		}

		uint32_t maxMessageBytes() const {
			return socket->limits().maxMessageBytes(authenticated());
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
		Server(const asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<ConnectionStateBase> (const std::shared_ptr<Connection> &)> a_connectionStateFactory, const MessageLimits& a_limits = {});

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

		const MessageLimits& limits() const {
			return ourLimits;
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

		const MessageLimits ourLimits;

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
