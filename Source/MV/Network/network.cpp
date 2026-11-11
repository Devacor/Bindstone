#include "network.h"
#include "MV/Network/networkStream.h"
#include "dynamicVariable.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Utility/scopeGuard.hpp"

#include <algorithm>
#include <cstring>

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

static jai::registrar<MV::DynamicVariable, MV::Services> _hookDynamicVariable([](jai::dynamic_binder<MV::DynamicVariable>& builder, const MV::Services&) {
	builder.constructor<>();
	builder.constructor<bool>();
	builder.constructor<int64_t>();
	builder.constructor<int>();
	builder.constructor<size_t>();
	builder.constructor<double>();
	builder.constructor<const std::string&>();

	builder.method("=", [](MV::DynamicVariable& self, bool v) -> MV::DynamicVariable& { return self = v; });
	builder.method("=", [](MV::DynamicVariable& self, int64_t v) -> MV::DynamicVariable& { return self = v; });
	builder.method("=", [](MV::DynamicVariable& self, int v) -> MV::DynamicVariable& { return self = v; });
	builder.method("=", [](MV::DynamicVariable& self, double v) -> MV::DynamicVariable& { return self = v; });
	builder.method("=", [](MV::DynamicVariable& self, const std::string& v) -> MV::DynamicVariable& { return self = v; });

	builder.method("==", [](MV::DynamicVariable& self, bool v) { return self == v; });
	builder.method("==", [](MV::DynamicVariable& self, int64_t v) { return self == v; });
	builder.method("==", [](MV::DynamicVariable& self, int v) { return self == v; });
	builder.method("==", [](MV::DynamicVariable& self, double v) { return self == v; });
	builder.method("==", [](MV::DynamicVariable& self, const std::string& v) { return self == v; });

	builder.method("!=", [](MV::DynamicVariable& self, bool v) { return self != v; });
	builder.method("!=", [](MV::DynamicVariable& self, int64_t v) { return self != v; });
	builder.method("!=", [](MV::DynamicVariable& self, int v) { return self != v; });
	builder.method("!=", [](MV::DynamicVariable& self, double v) { return self != v; });
	builder.method("!=", [](MV::DynamicVariable& self, const std::string& v) { return self != v; });

	builder.method("bool", [](MV::DynamicVariable& self) { return self.getBool(); });
	builder.method("int", [](MV::DynamicVariable& self) { return self.getInt(); });
	builder.method("double", [](MV::DynamicVariable& self) { return self.getDouble(); });
	builder.method("string", [](MV::DynamicVariable& self) { return self.getString(); });

	builder.method("clear", [](MV::DynamicVariable& self) { return self.clear(); });
	builder.method("empty", &MV::DynamicVariable::empty);
});

static jai::registrar<MV::Client, MV::Services> _hookClient([](jai::dynamic_binder<MV::Client>& builder, const MV::Services&) {
	builder.method("send", &MV::Client::send, jai::serial);
	builder.method("connected", &MV::Client::connected);
	builder.method("disconnect", [](MV::Client& self) { self.disconnect(); }, jai::serial);
	builder.method("reconnect", [](MV::Client& self) { self.reconnect(); }, jai::serial);
});

namespace MV {

	namespace {
		size_t unreadCost(const NetworkMessage& a_message) {
			return sizeof(NetworkMessage) + a_message.content.size();
		}

		// Moves the first four bytes of a_stream's input into a new message's header.
		std::shared_ptr<NetworkMessage> takeHeader(NetworkStream& a_stream) {
			auto result = std::make_shared<NetworkMessage>();
			std::memcpy(result->headerBuffer.data(), a_stream.input().data(), result->headerBuffer.size());
			a_stream.consume(result->headerBuffer.size());
			return result;
		}

		// Moves what a_stream holds of a_message's content into it; true once the content is whole.
		bool takeContent(NetworkStream& a_stream, NetworkMessage& a_message, uint32_t a_contentSize) {
			auto available = a_stream.input();
			auto taking = std::min(static_cast<size_t>(a_contentSize) - a_message.content.size(), available.size());
			a_message.content.append(available.data(), taking);
			a_stream.consume(taking);
			return a_message.content.size() == a_contentSize;
		}

		const size_t PROTOCOL_FRAME_FIXED_BYTES = 5;
	}

	Client::Client(const MV::Url& a_url, const TlsConfig& a_tls, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized, const MessageLimits& a_limits) :
		url(a_url),
		ourLimits(a_limits),
		tlsConfig(a_tls),
		network(std::make_unique<NetworkContext>(1)),
		onMessageGet(a_onMessageGet),
		onConnectionFail(a_onConnectionFail),
		onInitialized(a_onInitialized),
		ourServerMaxMessageBytes(a_limits.unauthenticatedMaxMessageBytes) {
	}

	// The network thread ends before the stream and inbox it uses go away.
	Client::~Client() {
		ourConnectionState = DISCONNECTED;
		network.reset();
	}

	void Client::disconnect() {
		disconnectRequested = true;
		info("client.disconnect();");
		closeConnection("Disconnect Requested");
	}

	void Client::handleError(const std::error_code &a_err, const std::string &a_section) {
		auto message = a_err.message();
		closeConnection(message);
		error("[", a_section, "] => ", message);
	}

	void Client::closeConnection(const std::string &a_reason) {
		ourConnectionState = DISCONNECTED;
		{
			std::lock_guard<std::mutex> guard(errorLock);
			failMessage = a_reason;
		}
		closeOnNetworkThread();
	}

	void Client::closeOnNetworkThread() {
		if (network->reactor().runningInThisThread()) {
			closeNow();
		} else {
			network->post([this] { closeNow(); });
		}
	}

	void Client::closeNow() {
		if (stream) {
			stream->close();
			stream.reset();
		}
		network->reactor().cancel(handshakeDeadline);
		handshakeDeadline = 0;
		incoming.reset();
	}

	void Client::initiateConnection() {
		auto expected = DISCONNECTED;
		if (ourConnectionState.compare_exchange_strong(expected, CONNECTING)) {
			disconnectRequested = false;
			connectionAnnounced = false;
			ourServerMaxMessageBytes = ourLimits.unauthenticatedMaxMessageBytes;
			ourRejection.reset();
			{
				std::lock_guard<std::mutex> guard(errorLock);
				failMessage.clear();
				ourTlsSession.clear();
			}
			if (!tlsContext) {
				try {
					tlsContext = makeTlsClientContext(tlsConfig);
				} catch (std::exception &e) {
					error("[tls] -> ", e.what());
					closeConnection(e.what());
					return;
				}
			}
			network->post([this] {
				if (ourConnectionState != CONNECTING) {
					return;
				}
				if (stream) {
					stream->close();
				}
				auto attempt = NetworkStream::make(network->reactor());
				stream = attempt;
				network->reactor().resolve(url.host(), std::to_string(url.port()), [this, attempt](const std::error_code& a_err, std::vector<SocketAddress> a_addresses) {
					if (attempt != stream) {
						return;
					}
					if (a_err) {
						handleError(a_err, "resolve");
					} else {
						connectTo(attempt, std::move(a_addresses));
					}
				});
			});
		}
	}

	void Client::connectTo(const std::shared_ptr<NetworkStream>& a_attempt, std::vector<SocketAddress> a_addresses) {
		a_attempt->connect(std::move(a_addresses), [this, a_attempt](const std::error_code& a_err) {
			if (a_attempt != stream) {
				return;
			}
			if (a_err) {
				handleError(a_err, "connect");
			} else {
				handshake(a_attempt);
			}
		});
	}

	void Client::handshake(const std::shared_ptr<NetworkStream>& a_attempt) {
		ssl_st* ssl = nullptr;
		try {
			ssl = a_attempt->beginTls(*tlsContext);
			verifyTlsPeerAs(ssl, url.host());
		} catch (std::exception &e) {
			error("[handshake] -> ", e.what());
			closeConnection(e.what());
			return;
		}
		handshakeDeadline = network->reactor().scheduleAfter(ourLimits.handshakeTimeout, [this, a_attempt] {
			handshakeDeadline = 0;
			if (a_attempt == stream && ourConnectionState == CONNECTING) {
				auto reason = "No TLS handshake from " + url.host() + " within " + std::to_string(ourLimits.handshakeTimeout.count()) + " ms.";
				error("[handshake] -> ", reason);
				closeConnection(reason);
			}
		});
		a_attempt->handshake([this, a_attempt, ssl](const std::error_code& a_err) {
			if (a_attempt != stream) {
				return;
			}
			network->reactor().cancel(handshakeDeadline);
			handshakeDeadline = 0;
			if (a_err) {
				auto reason = "TLS handshake with " + url.host() + " failed: " + describeTlsFailure(a_err, ssl);
				error("[handshake] -> ", reason);
				closeConnection(reason);
				return;
			}
			{
				std::lock_guard<std::mutex> guard(errorLock);
				ourTlsSession = tlsSessionDescription(ssl);
			}
			auto expected = CONNECTING;
			if (ourConnectionState.compare_exchange_strong(expected, CONNECTING_SUCCESS)) {
				a_attempt->onInput([this, a_attempt] { readMessages(a_attempt); });
				a_attempt->onClosed([this, a_attempt](const std::error_code& a_closeErr) {
					if (a_attempt == stream) {
						handleError(a_closeErr, incoming ? "content" : "header");
					}
				});
				a_attempt->reading(true);
			}
		});
	}

	void Client::readMessages(const std::shared_ptr<NetworkStream>& a_attempt) {
		while (a_attempt == stream && ourConnectionState != DISCONNECTED) {
			if (!incoming) {
				if (a_attempt->input().size() < 4) {
					return;
				}
				auto message = takeHeader(*a_attempt);
				auto header = message->headerValue();
				message->protocol = (header & PROTOCOL_FRAME_BIT) != 0;
				auto contentSize = header & ~PROTOCOL_FRAME_BIT;
				auto maxMessageBytes = message->protocol ? MAX_PROTOCOL_FRAME_BYTES : ourLimits.authenticatedMaxMessageBytes;
				if (contentSize > maxMessageBytes) {
					auto reason = "Received a " + std::to_string(contentSize) + " byte " + (message->protocol ? "protocol frame" : "message") + ", over the " + std::to_string(maxMessageBytes) + " byte limit.";
					error("[header] -> ", reason);
					closeConnection(reason);
					return;
				}
				message->content.reserve(contentSize);
				incoming = message;
			}
			if (!takeContent(*a_attempt, *incoming, incoming->headerValue() & ~PROTOCOL_FRAME_BIT)) {
				return;
			}
			std::lock_guard<std::mutex> guard(inboxLock);
			inbox.push_back(std::move(incoming));
			incoming.reset();
		}
	}

	void Client::initialize() {
		try {
			initiateConnection();
		} catch (std::exception &e) {
			disconnect();
			error("Exception caught in Client: ", e.what());
		}
	}

	std::string Client::takeFailure() {
		std::string result;
		std::lock_guard<std::mutex> guard(errorLock);
		result.swap(failMessage);
		return result;
	}

	void Client::update() {
		auto keepAlive = shared_from_this();
		auto failure = takeFailure(); //read before the inbox: every message the peer sent before failing is in it
		std::vector<std::shared_ptr<NetworkMessage>> received;
		{
			std::lock_guard<std::mutex> guard(inboxLock);
			received.swap(inbox);
		}
		tryInitializeCallback();
		for (auto&& message : received) {
			if (message->protocol) {
				receiveProtocolFrame(message->content);
			} else if (connectionAnnounced && !disconnectRequested) {
				try {
					onMessageGet(message->content);
				} catch (std::exception &e) {
					error("Exception caught in network message handler: ", e.what());
				}
			}
		}
		if (!failure.empty()) {
			connectionAnnounced = false;
			if (onConnectionFail) {
				onConnectionFail(ourRejection ? ourRejection->reason : failure);
			}
		}
	}

	void Client::receiveProtocolFrame(const std::string &a_payload) {
		auto frame = ProtocolFrame::decode(a_payload);
		if (!frame) {
			error("Ignored a malformed protocol frame.");
		} else if (!frame->rejection()) {
			info("Server message limit: ", frame->value, " bytes");
			ourServerMaxMessageBytes = frame->value;
		} else {
			error("Server rejected the connection: ", frame->reason);
			ourRejection = frame;
			if (onRejectedCallback) {
				try {
					onRejectedCallback(*frame);
				} catch (std::exception &e) {
					error("Exception caught in network rejection handler: ", e.what());
				}
			}
		}
	}

	void Client::send(const std::string &a_content) {
		require<DeviceException>(connected(), "Failed to send message due to lack of connection.");
		if (a_content.size() > ourServerMaxMessageBytes) {
			MessageTooLargeException exception(a_content.size(), ourServerMaxMessageBytes);
			error(exception.what());
			throw exception;
		}
		network->post([this, message = NetworkMessage::make(a_content)] {
			if (stream && stream->open()) {
				stream->write(message->wire());
			}
		});
	}

	void Client::tryInitializeCallback() {
		auto expected = CONNECTING_SUCCESS;
		if (ourConnectionState.compare_exchange_strong(expected, CONNECTED)) {
			connectionAnnounced = true;
			if (onInitialized) {
				try {
					onInitialized();
				} catch (std::exception &e) {
					error("Exception caught in network initialization call: ", e.what());
				}
			}
		}
	}

	Server::Server(const std::string& a_address, uint16_t a_port, const TlsConfig& a_tls, std::function<std::unique_ptr<ConnectionStateBase>(const std::shared_ptr<Connection> &)> a_connectionStateFactory, const MessageLimits& a_limits) :
		ourLimits(a_limits),
		tlsContext(makeTlsServerContext(a_tls)),
		network(std::make_unique<NetworkContext>(1)),
		connectionStateFactory(a_connectionStateFactory) {

		auto address = SocketAddress::parse(a_address, a_port);
		require<ResourceException>(address.has_value(), "Server: [", a_address, "] is not an IPv4 or IPv6 address");
		listener = NetworkListener::make(network->reactor(), *address, [this](Socket a_socket) { accept(std::move(a_socket)); });
		ourPort = listener->port();
		info("Server Startup: TLS on port ", ourPort, a_tls.requirePeerCertificate ? ", client certificates required" : "");
	}

	// The network thread stops first; the connections then post their closes to a reactor that only discards them.
	Server::~Server() {
		info("Server::~Server");
		network->stop();
	}

	void Server::sendAll(const std::string &a_message) {
		sent.add(accumulatedTime, (a_message.size() + 4) * ourConnections.size());
		for (auto&& connection : ourConnections) {
			connection->send(a_message);
		}
	}

	void Server::sendExcept(const std::string &a_message, Connection* a_exceptConnection) {
		if (!ourConnections.empty()) {
			sent.add(accumulatedTime, (a_message.size() + 4) * (ourConnections.size() - 1));
		}
		for (auto&& connection : ourConnections) {
			if (connection.get() != a_exceptConnection) {
				connection->send(a_message);
			}
		}
	}

	void Server::accept(Socket a_socket) {
		auto stream = NetworkStream::make(network->reactor(), std::move(a_socket));
		auto connectionSocket = std::make_shared<ConnectionSocket>(network->reactor(), tlsContext, stream, ourLimits, [this](const std::shared_ptr<ConnectionSocket> &a_ready) {
			std::lock_guard<std::mutex> guard(acceptedLock);
			accepted.push_back(a_ready);
		});
		connectionSocket->start();
	}

	void Server::adoptAcceptedConnections() {
		std::vector<std::shared_ptr<ConnectionSocket>> adopting;
		{
			std::lock_guard<std::mutex> guard(acceptedLock);
			adopting.swap(accepted);
		}
		for (auto&& connectionSocket : adopting) {
			auto connection = std::make_shared<Connection>(connectionSocket);
			connection->initialize(connectionStateFactory);
			ourConnections.push_back(connection);
		}
	}

	void Server::update(double a_dt) {
		adoptAcceptedConnections();
		accumulatedTime += a_dt;
		auto updating = ourConnections; //an update may disconnect any connection, so none is erased mid-pass
		for (auto&& connection : updating) {
			try {
				received.add(accumulatedTime, connection->update(a_dt));
			} catch (std::exception &e) {
				error("Exception for connection update: ", e.what());
			} catch (...) {
				error("Unknown Exception for connection update!");
			}
		}
		auto startSize = ourConnections.size();
		ourConnections.erase(std::remove_if(ourConnections.begin(), ourConnections.end(), [](const std::shared_ptr<Connection>& a_connection) {
			if (a_connection->disconnected()) {
				a_connection->disconnect();
				return true;
			}
			return false;
		}), ourConnections.end());
		if (startSize != ourConnections.size()) {
			info("Connections lost: ", startSize - ourConnections.size());
		}
	}

	size_t Connection::update(double a_dt) {
		size_t receivedAmount = 0;
		if (ourState->disconnected()) {
			socket->close();
			return receivedAmount;
		}
		auto self = shared_from_this();
		bool failed = socket->failed(); //read before the inbox: every message the peer sent before failing is in it
		for (auto&& message : socket->takeInbox()) {
			try {
				receivedAmount += message->headerAndContentSize();
				ourState->message(message->content);
			} catch (std::exception &e) {
				error("Caught an exception in Connection::update: ", e.what());
				disconnect();
				return receivedAmount;
			}
		}
		if (failed) {
			disconnect();
			return receivedAmount;
		}
		ourState->update(a_dt);
		return receivedAmount;
	}

	ConnectionSocket::ConnectionSocket(NetworkReactor &a_reactor, std::shared_ptr<TlsContext> a_tlsContext, std::shared_ptr<NetworkStream> a_stream, const MessageLimits& a_limits, std::function<void(const std::shared_ptr<ConnectionSocket> &)> a_onReady) :
		reactor(a_reactor),
		mailbox(a_reactor.mailbox()),
		tlsContext(std::move(a_tlsContext)),
		stream(std::move(a_stream)),
		onReady(std::move(a_onReady)),
		ourLimits(a_limits),
		ourPeerAddress(stream->peer() ? stream->peer()->to_string() : std::string("unknown peer")),
		ourLoopback(stream->peer() && stream->peer()->loopback()) {
	}

	ConnectionSocket::~ConnectionSocket() = default;

	void ConnectionSocket::start() {
		handshaking = true;
		armDeadline(ourLimits.handshakeTimeout);
		try {
			stream->beginTls(*tlsContext);
		} catch (std::exception &e) {
			error("[handshake] ", ourPeerAddress, " -> ", e.what());
			ourFailed = true;
			closeNow();
			return;
		}
		stream->handshake([self = shared_from_this()](const std::error_code& a_err) { self->handshakeComplete(a_err); });
	}

	void ConnectionSocket::handshakeComplete(const std::error_code &a_err) {
		handshaking = false;
		disarmDeadline();
		if (a_err) {
			error("[handshake] ", ourPeerAddress, " -> ", describeTlsFailure(a_err, stream->tls()));
			ourFailed = true;
			closeNow();
			return;
		}
		ourPeerCertificate = tlsPeerCertificate(stream->tls());
		ourTlsSession = tlsSessionDescription(stream->tls());
		info("[handshake] ", ourPeerAddress, " -> ", ourTlsSession, ourPeerCertificate ? " certificate " + ourPeerCertificate->describe() : std::string());
		auto self = shared_from_this();
		stream->onInput([self] { self->readMessages(); });
		stream->onClosed([self](const std::error_code& a_closeErr) { self->handleError(a_closeErr, self->incoming ? "content" : "header"); });
		stream->reading(true);
		auto ready = std::move(onReady);
		ready(self);
	}

	void ConnectionSocket::send(const std::string &a_content) {
		queue(NetworkMessage::make(a_content));
	}

	void ConnectionSocket::queue(const std::shared_ptr<NetworkMessage> &a_message) {
		mailbox->post([self = shared_from_this(), a_message] {
			if (self->stream->open() && !self->closing) {
				self->push(a_message);
			}
		});
	}

	void ConnectionSocket::push(const std::shared_ptr<NetworkMessage> &a_message) {
		stream->write(a_message->wire());
	}

	void ConnectionSocket::close() {
		mailbox->post([self = shared_from_this()] { self->beginClose(); });
	}

	// Messages queued before the close still go out (a kick or a rejection says why), then TLS sends its
	// close_notify and incoming bytes are discarded until the peer closes, so the peer reads the reason
	// instead of a reset; the deadline bounds a peer that stops reading or keeps sending.
	void ConnectionSocket::beginClose() {
		if (closing || !stream->open()) {
			return;
		}
		closing = true;
		disarmDeadline();
		closeDeadline = reactor.scheduleAfter(std::chrono::seconds(2), [self = shared_from_this()] {
			self->closeDeadline = 0;
			self->closeNow();
		});
		stream->finish([self = shared_from_this()] { self->closeNow(); });
	}

	void ConnectionSocket::closeNow() {
		stream->close();
		reactor.cancel(closeDeadline);
		closeDeadline = 0;
		disarmDeadline();
		incoming.reset();
	}

	void ConnectionSocket::authenticate() {
		if (!ourAuthenticated.exchange(true)) {
			queue(NetworkMessage::make(ProtocolFrame {ProtocolCode::MESSAGE_LIMIT, ourLimits.authenticatedMaxMessageBytes, {}}));
			resumeBelowCap();
		}
	}

	std::vector<std::shared_ptr<NetworkMessage>> ConnectionSocket::takeInbox() {
		std::vector<std::shared_ptr<NetworkMessage>> result;
		{
			std::lock_guard<std::mutex> guard(inboxLock);
			result.swap(inbox);
			unreadBytes = 0;
		}
		resumeBelowCap();
		return result;
	}

	// The pause flag changes hands under the inbox lock, so a drain either lands before the network thread
	// decides to pause or sees the pause and posts the resume.
	void ConnectionSocket::resumeBelowCap() {
		{
			std::lock_guard<std::mutex> guard(inboxLock);
			if (!readingPaused || unreadBytes >= ourLimits.maxUnreadBytes(ourAuthenticated)) {
				return;
			}
			readingPaused = false;
		}
		mailbox->post([self = shared_from_this()] { self->resumeReading(); });
	}

	void ConnectionSocket::readMessages() {
		while (stream->open() && !closing) {
			if (!incoming) {
				if (stream->input().size() < 4) {
					return;
				}
				auto message = takeHeader(*stream);
				auto contentSize = message->headerValue();
				auto maxMessageBytes = ourLimits.maxMessageBytes(ourAuthenticated);
				if (contentSize > maxMessageBytes) {
					reject(ProtocolCode::MESSAGE_TOO_LARGE, maxMessageBytes, "A " + std::to_string(contentSize) + " byte message exceeds the " + std::to_string(maxMessageBytes) + " byte limit.");
					return;
				}
				message->content.reserve(contentSize);
				incoming = message;
				armDeadline(ourLimits.unreadTimeout);
			}
			if (!takeContent(*stream, *incoming, incoming->headerValue())) {
				return;
			}
			disarmDeadline();
			auto message = std::move(incoming);
			incoming.reset();
			if (!receive(message)) {
				return;
			}
		}
	}

	bool ConnectionSocket::receive(const std::shared_ptr<NetworkMessage> &a_message) {
		bool paused = false;
		{
			std::lock_guard<std::mutex> guard(inboxLock);
			inbox.push_back(a_message);
			unreadBytes += unreadCost(*a_message);
			readingPaused = unreadBytes >= ourLimits.maxUnreadBytes(ourAuthenticated);
			paused = readingPaused;
		}
		if (paused) {
			stream->reading(false);
			armDeadline(ourLimits.unreadTimeout);
		}
		return !paused;
	}

	void ConnectionSocket::resumeReading() {
		if (stream->open() && !closing) {
			disarmDeadline();
			stream->reading(true);
		}
	}

	void ConnectionSocket::armDeadline(std::chrono::milliseconds a_timeout) {
		reactor.cancel(deadline);
		deadline = reactor.scheduleAfter(a_timeout, [self = shared_from_this()] {
			self->deadline = 0;
			if (self->stream->open() && !self->closing) {
				self->deadlineReached();
			}
		});
	}

	void ConnectionSocket::disarmDeadline() {
		reactor.cancel(deadline);
		deadline = 0;
	}

	void ConnectionSocket::deadlineReached() {
		if (handshaking) {
			error("[handshake] ", ourPeerAddress, " -> no TLS handshake within ", ourLimits.handshakeTimeout.count(), " ms");
			ourFailed = true;
			closeNow();
			return;
		}
		auto timeout = static_cast<uint32_t>(ourLimits.unreadTimeout.count());
		if (incoming) {
			reject(ProtocolCode::MESSAGE_TIMEOUT, timeout, "A message did not finish arriving within " + std::to_string(timeout) + " ms.");
			return;
		}
		bool paused = false;
		{
			std::lock_guard<std::mutex> guard(inboxLock);
			paused = readingPaused;
		}
		if (paused) {
			reject(ProtocolCode::INBOX_TIMEOUT, timeout, "Messages arrived faster than the server processes them and stayed unread for " + std::to_string(timeout) + " ms.");
		}
	}

	void ConnectionSocket::reject(ProtocolCode a_code, uint32_t a_value, const std::string &a_reason) {
		error("[rejected] -> ", a_reason);
		ourFailed = true;
		if (stream->open() && !closing) {
			push(NetworkMessage::make(ProtocolFrame {a_code, a_value, a_reason}));
			beginClose();
		}
	}

	void ConnectionSocket::handleError(const std::error_code &a_err, const std::string &a_section) {
		error("[", a_section, "] -> ", a_err.message());
		ourFailed = true;
	}

	std::string ProtocolFrame::encode() const {
		std::string result {static_cast<char>(code), static_cast<char>(value >> 24), static_cast<char>(value >> 16), static_cast<char>(value >> 8), static_cast<char>(value)};
		result.append(reason, 0, MAX_PROTOCOL_FRAME_BYTES - PROTOCOL_FRAME_FIXED_BYTES);
		return result;
	}

	std::optional<ProtocolFrame> ProtocolFrame::decode(const std::string &a_payload) {
		if (a_payload.size() < PROTOCOL_FRAME_FIXED_BYTES || a_payload.size() > MAX_PROTOCOL_FRAME_BYTES) {
			return std::nullopt;
		}
		auto code = static_cast<uint8_t>(a_payload[0]);
		if (code < static_cast<uint8_t>(ProtocolCode::MESSAGE_LIMIT) || code > static_cast<uint8_t>(ProtocolCode::INBOX_TIMEOUT)) {
			return std::nullopt;
		}
		ProtocolFrame result;
		result.code = static_cast<ProtocolCode>(code);
		for (size_t i = 1; i < PROTOCOL_FRAME_FIXED_BYTES; ++i) {
			result.value = (result.value << 8) | static_cast<uint8_t>(a_payload[i]);
		}
		result.reason = a_payload.substr(PROTOCOL_FRAME_FIXED_BYTES);
		return result;
	}

	namespace {
		std::shared_ptr<NetworkMessage> outgoing(const std::string &a_content, bool a_protocol) {
			auto result = std::make_shared<NetworkMessage>();
			result->protocol = a_protocol;
			auto header = static_cast<uint32_t>(a_content.size()) | (a_protocol ? PROTOCOL_FRAME_BIT : 0);
			result->wireBytes.reserve(4 + a_content.size());
			result->wireBytes = {static_cast<char>(header >> 24), static_cast<char>(header >> 16), static_cast<char>(header >> 8), static_cast<char>(header)};
			result->wireBytes += a_content;
			return result;
		}
	}

	std::shared_ptr<NetworkMessage> NetworkMessage::make(const std::string &a_content) {
		return outgoing(a_content, false);
	}

	std::shared_ptr<NetworkMessage> NetworkMessage::make(const ProtocolFrame &a_frame) {
		return outgoing(a_frame.encode(), true);
	}

	uint32_t NetworkMessage::headerValue() const {
		return (static_cast<uint32_t>(headerBuffer[0]) << 24) | (static_cast<uint32_t>(headerBuffer[1]) << 16) | (static_cast<uint32_t>(headerBuffer[2]) << 8) | static_cast<uint32_t>(headerBuffer[3]);
	}

	uint32_t NetworkMessage::headerAndContentSize() const {
		return 4 + static_cast<uint32_t>(content.size());
	}

}
