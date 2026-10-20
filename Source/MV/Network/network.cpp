#include "network.h"
#include "MV/Network/tlsAsio.h"
#include "dynamicVariable.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Utility/scopeGuard.hpp"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

static jai::registrar<MV::DynamicVariable, MV::Services> _hookDynamicVariable("DynamicVariable",
	[](jai::dynamic_binder<MV::DynamicVariable>& builder, const MV::Services&) {
	builder.auto_bind();

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
});

static jai::registrar<MV::Client, MV::Services> _hookClient("Client",
	[](jai::dynamic_binder<MV::Client>& builder, const MV::Services&) {
	builder.serial_method("send").serial_method("disconnect").serial_method("reconnect");
	builder.auto_bind();

	builder.method("send", &MV::Client::send);
	builder.method("connected", &MV::Client::connected);
	builder.method("disconnect", [](MV::Client& self) { self.disconnect(); });
	builder.method("reconnect", [](MV::Client& self) { self.reconnect(); });
});

namespace MV {

	namespace {
		void closeSocket(std::shared_ptr<TlsStream> &a_stream) {
			if (a_stream) {
				auto& socket = a_stream->lowest_layer();
				if (socket.is_open()) {
					asio::error_code ignored;
					socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
					socket.close(ignored);
				}
				a_stream.reset();
			}
		}
	}

	struct Client::AsioConnection {
		explicit AsioConnection(Client& a_client) :
			client(a_client),
			resolver(ioService),
			handshakeDeadline(ioService) {
		}

		void connectTo(const std::shared_ptr<TlsStream>& a_attempt, asio::ip::tcp::resolver::iterator a_endpoints);
		void handleConnect(const std::shared_ptr<TlsStream>& a_attempt, const asio::error_code& a_err, asio::ip::tcp::resolver::iterator a_nextEndpoint);
		void handshake(const std::shared_ptr<TlsStream>& a_attempt);

		Client& client;
		// Built by the first connection attempt, so a missing certificate fails that attempt instead of the constructor.
		std::shared_ptr<asio::ssl::context> tlsContext;
		asio::io_context ioService;
		asio::ip::tcp::resolver resolver;
		std::shared_ptr<TlsStream> socket;
		asio::steady_timer handshakeDeadline;
		std::unique_ptr<asio::io_context::work> work {std::make_unique<asio::io_context::work>(ioService)};
	};

	struct ConnectionSocket::AsioSocket {
		AsioSocket(asio::io_context& a_ioService, std::shared_ptr<asio::ssl::context> a_tlsContext, asio::ip::tcp::socket&& a_socket) :
			ioService(a_ioService),
			tlsContext(std::move(a_tlsContext)),
			socket(std::make_shared<TlsStream>(std::move(a_socket), *tlsContext)),
			closeDeadline(a_ioService),
			deadline(a_ioService) {
		}

		asio::io_context& ioService;
		std::shared_ptr<asio::ssl::context> tlsContext;
		std::shared_ptr<TlsStream> socket;
		asio::steady_timer closeDeadline;
		asio::steady_timer deadline;
	};

	struct Server::AsioAcceptor {
		explicit AsioAcceptor(const TlsConfig& a_tls) :
			tlsContext(makeTlsServerContext(a_tls)),
			acceptor(ioService) {
		}

		std::shared_ptr<asio::ssl::context> tlsContext;
		asio::io_context ioService;
		asio::ip::tcp::acceptor acceptor;
		std::unique_ptr<asio::io_context::work> work {std::make_unique<asio::io_context::work>(ioService)};
	};

	Client::Client(const MV::Url& a_url, const TlsConfig& a_tls, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized, const MessageLimits& a_limits) :
		url(a_url),
		ourLimits(a_limits),
		tlsConfig(a_tls),
		asioConnection(std::make_unique<AsioConnection>(*this)),
		onMessageGet(a_onMessageGet),
		onConnectionFail(a_onConnectionFail),
		onInitialized(a_onInitialized),
		ourServerMaxMessageBytes(a_limits.unauthenticatedMaxMessageBytes) {
	}

	Client::~Client() {
		ourConnectionState = DISCONNECTED;
		asioConnection->ioService.stop();
		if (worker && worker->joinable()) {
			worker->join();
		}
		closeNow();
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
		if (asioConnection->ioService.get_executor().running_in_this_thread()) {
			closeNow();
		} else {
			asio::post(asioConnection->ioService, [this] { closeNow(); });
		}
	}

	void Client::closeNow() {
		closeSocket(asioConnection->socket);
		asioConnection->handshakeDeadline.cancel();
		outbox.clear();
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
			if (!asioConnection->tlsContext) {
				try {
					asioConnection->tlsContext = makeTlsClientContext(tlsConfig);
				} catch (std::exception &e) {
					error("[tls] -> ", e.what());
					closeConnection(e.what());
					return;
				}
			}
			asio::post(asioConnection->ioService, [this, context = asioConnection->tlsContext] {
				if (ourConnectionState != CONNECTING) {
					return;
				}
				auto attempt = std::make_shared<TlsStream>(asioConnection->ioService, *context);
				asioConnection->socket = attempt;
				asio::ip::tcp::resolver::query query(url.host(), std::to_string(url.port()), asio::ip::tcp::resolver::query::canonical_name);
				asioConnection->resolver.async_resolve(query, [this, attempt](const asio::error_code& a_err, asio::ip::tcp::resolver::iterator a_endpoints) {
					if (attempt != asioConnection->socket) {
						return;
					}
					if (a_err) {
						handleError(a_err, "resolve");
					} else {
						asioConnection->connectTo(attempt, a_endpoints);
					}
				});
			});
		}
	}

	void Client::AsioConnection::connectTo(const std::shared_ptr<TlsStream>& a_attempt, asio::ip::tcp::resolver::iterator a_endpoints) {
		asio::ip::tcp::endpoint endpoint = *a_endpoints;
		a_attempt->lowest_layer().async_connect(endpoint, [this, a_attempt, next = ++a_endpoints](const asio::error_code& a_err) {
			handleConnect(a_attempt, a_err, next);
		});
	}

	void Client::AsioConnection::handleConnect(const std::shared_ptr<TlsStream>& a_attempt, const asio::error_code& a_err, asio::ip::tcp::resolver::iterator a_nextEndpoint) {
		if (a_attempt != socket) {
			return;
		}
		if (!a_err) {
			handshake(a_attempt);
		} else if (a_nextEndpoint != asio::ip::tcp::resolver::iterator()) {
			info("Trying next endpoint: ", a_err.message());
			auto retry = std::make_shared<TlsStream>(ioService, *tlsContext);
			socket = retry;
			connectTo(retry, a_nextEndpoint);
		} else {
			client.handleError(a_err, "connect");
		}
	}

	void Client::AsioConnection::handshake(const std::shared_ptr<TlsStream>& a_attempt) {
		try {
			verifyTlsPeerAs(*a_attempt, client.url.host());
		} catch (std::exception &e) {
			error("[handshake] -> ", e.what());
			client.closeConnection(e.what());
			return;
		}
		handshakeDeadline.expires_after(client.ourLimits.handshakeTimeout);
		handshakeDeadline.async_wait([this, a_attempt](const asio::error_code& a_err) {
			if (!a_err && a_attempt == socket && client.ourConnectionState == CONNECTING) {
				auto reason = "No TLS handshake from " + client.url.host() + " within " + std::to_string(client.ourLimits.handshakeTimeout.count()) + " ms.";
				error("[handshake] -> ", reason);
				client.closeConnection(reason);
			}
		});
		a_attempt->async_handshake(asio::ssl::stream_base::client, [this, a_attempt](const asio::error_code& a_err) {
			if (a_attempt != socket) {
				return;
			}
			handshakeDeadline.cancel();
			if (a_err) {
				auto reason = "TLS handshake with " + client.url.host() + " failed: " + describeTlsFailure(a_err, *a_attempt);
				error("[handshake] -> ", reason);
				client.closeConnection(reason);
				return;
			}
			{
				std::lock_guard<std::mutex> guard(client.errorLock);
				client.ourTlsSession = tlsSessionDescription(*a_attempt);
			}
			auto expected = CONNECTING;
			if (client.ourConnectionState.compare_exchange_strong(expected, CONNECTING_SUCCESS)) {
				client.initiateRead();
			}
		});
	}

	void Client::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto reading = asioConnection->socket;
		asio::async_read(*reading, asio::buffer(message->headerBuffer), [this, message, reading](const asio::error_code& a_err, size_t) {
			if (reading != asioConnection->socket) {
				return;
			}
			if (a_err) {
				handleError(a_err, "header");
				return;
			}
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
			message->content.resize(contentSize);
			asio::async_read(*reading, asio::buffer(message->content), [this, message, reading](const asio::error_code& a_err, size_t) {
				if (reading != asioConnection->socket) {
					return;
				}
				if (a_err) {
					handleError(a_err, "content");
				} else if (ourConnectionState != DISCONNECTED) {
					{
						std::lock_guard<std::mutex> guard(inboxLock);
						inbox.push_back(message);
					}
					initiateRead();
				}
			});
		});
	}

	void Client::initialize() {
		try {
			initiateConnection();
			if (!worker) {
				worker = std::make_unique<std::thread>([this] { asioConnection->ioService.run(); });
			}
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
		auto message = NetworkMessage::make(a_content);
		asio::post(asioConnection->ioService, [this, message] {
			if (asioConnection->socket) {
				outbox.push_back(message);
				if (outbox.size() == 1) { writeNext(); }
			}
		});
	}

	// One async_write in flight per socket: overlapping writes interleave their bytes on the wire.
	void Client::writeNext() {
		auto message = outbox.front();
		auto writing = asioConnection->socket;
		asio::async_write(*writing, asio::buffer(message->wire()), [this, message, writing](asio::error_code a_err, size_t) {
			if (writing != asioConnection->socket) {
				return;
			}
			outbox.pop_front();
			if (a_err) {
				handleError(a_err, "write");
			} else if (!outbox.empty()) {
				writeNext();
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
		asioAcceptor(std::make_unique<AsioAcceptor>(a_tls)),
		connectionStateFactory(a_connectionStateFactory) {
		const asio::ip::tcp::endpoint endpoint(asio::ip::make_address(a_address), a_port);
		auto& acceptor = asioAcceptor->acceptor;
		acceptor.open(endpoint.protocol());
#ifdef _WIN32
		// asio's endpoint constructor would set SO_REUSEADDR, which on Windows lets another process
		// bind a specific address over this wildcard port and capture its loopback connections
		acceptor.set_option(asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>(true));
#else
		acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
#endif
		acceptor.bind(endpoint);
		acceptor.listen();
		ourPort = acceptor.local_endpoint().port();
		info("Server Startup: TLS on port ", ourPort, a_tls.requirePeerCertificate ? ", client certificates required" : "");
		acceptClients();
		info("Accept Clients");
		worker = std::make_unique<std::thread>([this] { asioAcceptor->ioService.run(); });
	}

	Server::~Server() {
		info("Server::~Server");
		asioAcceptor->ioService.stop();
		if (worker && worker->joinable()) { worker->join(); }
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

	void Server::acceptClients() {
		auto socket = std::make_shared<asio::ip::tcp::socket>(asioAcceptor->ioService);
		asioAcceptor->acceptor.async_accept(*socket, [this, socket](asio::error_code ec) {
			if (!ec) {
				auto asioSocket = std::make_unique<ConnectionSocket::AsioSocket>(asioAcceptor->ioService, asioAcceptor->tlsContext, std::move(*socket));
				auto connectionSocket = std::make_shared<ConnectionSocket>(std::move(asioSocket), ourLimits, [this](const std::shared_ptr<ConnectionSocket> &a_ready) {
					std::lock_guard<std::mutex> guard(acceptedLock);
					accepted.push_back(a_ready);
				});
				connectionSocket->start();
			}

			acceptClients();
		});
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

	namespace {
		bool remoteIsLoopback(const asio::ip::tcp::socket& a_socket) {
			asio::error_code error;
			auto endpoint = a_socket.remote_endpoint(error);
			return !error && endpoint.address().is_loopback();
		}

		std::string remoteAddress(const asio::ip::tcp::socket& a_socket) {
			asio::error_code error;
			auto endpoint = a_socket.remote_endpoint(error);
			return error ? std::string("unknown peer") : endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
		}

		size_t unreadCost(const NetworkMessage& a_message) {
			return sizeof(NetworkMessage) + a_message.content.size();
		}

		const size_t PROTOCOL_FRAME_FIXED_BYTES = 5;
	}

	ConnectionSocket::ConnectionSocket(std::unique_ptr<AsioSocket> a_asioSocket, const MessageLimits& a_limits, std::function<void(const std::shared_ptr<ConnectionSocket> &)> a_onReady) :
		asioSocket(std::move(a_asioSocket)),
		onReady(std::move(a_onReady)),
		ourLimits(a_limits),
		ourPeerAddress(remoteAddress(asioSocket->socket->next_layer())),
		ourLoopback(remoteIsLoopback(asioSocket->socket->next_layer())) {
	}

	ConnectionSocket::~ConnectionSocket() = default;

	void ConnectionSocket::start() {
		handshaking = true;
		armDeadline(ourLimits.handshakeTimeout);
		auto stream = asioSocket->socket;
		stream->async_handshake(asio::ssl::stream_base::server, [self = shared_from_this(), stream](const asio::error_code& a_err) {
			if (stream == self->asioSocket->socket) {
				self->handshakeComplete(a_err);
			}
		});
	}

	void ConnectionSocket::handshakeComplete(const std::error_code &a_err) {
		handshaking = false;
		disarmDeadline();
		if (a_err) {
			error("[handshake] ", ourPeerAddress, " -> ", describeTlsFailure(a_err, *asioSocket->socket));
			ourFailed = true;
			closeNow();
			return;
		}
		ourPeerCertificate = tlsPeerCertificate(*asioSocket->socket);
		ourTlsSession = tlsSessionDescription(*asioSocket->socket);
		info("[handshake] ", ourPeerAddress, " -> ", ourTlsSession, ourPeerCertificate ? " certificate " + ourPeerCertificate->describe() : std::string());
		initiateRead();
		auto ready = std::move(onReady);
		ready(shared_from_this());
	}

	void ConnectionSocket::send(const std::string &a_content) {
		queue(NetworkMessage::make(a_content));
	}

	void ConnectionSocket::queue(const std::shared_ptr<NetworkMessage> &a_message) {
		asio::post(asioSocket->ioService, [self = shared_from_this(), a_message] {
			if (self->asioSocket->socket && !self->closing) {
				self->push(a_message);
			}
		});
	}

	void ConnectionSocket::push(const std::shared_ptr<NetworkMessage> &a_message) {
		outbox.push_back(a_message);
		if (outbox.size() == 1) {
			writeNext();
		}
	}

	void ConnectionSocket::close() {
		asio::post(asioSocket->ioService, [self = shared_from_this()] { self->beginClose(); });
	}

	// Messages queued before the close still go out (a kick or a rejection says why), then TLS sends its
	// close_notify and incoming bytes are discarded until the peer closes, so the peer reads the reason
	// instead of a reset; the deadline bounds a peer that stops reading or keeps sending.
	void ConnectionSocket::beginClose() {
		if (closing || !asioSocket->socket) {
			return;
		}
		closing = true;
		asioSocket->deadline.cancel();
		asioSocket->closeDeadline.expires_after(std::chrono::seconds(2));
		asioSocket->closeDeadline.async_wait([self = shared_from_this()](const asio::error_code& a_err) {
			if (!a_err) {
				self->closeNow();
			}
		});
		if (outbox.empty()) {
			linger();
		}
	}

	// A pending read is cancelled first: the TLS shutdown reads too, and a stream allows one read at a time.
	void ConnectionSocket::linger() {
		if (lingering || !asioSocket->socket) {
			return;
		}
		lingering = true;
		if (reading) {
			asio::error_code ignored;
			asioSocket->socket->lowest_layer().cancel(ignored);
		} else {
			shutdownTls();
		}
	}

	// OpenSSL fails the shutdown when data arrives after its close_notify, which is out by then: drain at the TCP level.
	void ConnectionSocket::shutdownTls() {
		auto closingStream = asioSocket->socket;
		closingStream->async_shutdown([self = shared_from_this(), closingStream](const asio::error_code& a_err) {
			if (closingStream != self->asioSocket->socket) {
				return;
			}
			if (!a_err) {
				self->closeNow();
				return;
			}
			asio::error_code ignored;
			self->asioSocket->socket->lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
			self->discarded.resize(16 * 1024);
			self->discardNext();
		});
	}

	void ConnectionSocket::discardNext() {
		auto discarding = asioSocket->socket;
		discarding->next_layer().async_read_some(asio::buffer(discarded), [self = shared_from_this(), discarding](const asio::error_code& a_err, size_t) {
			if (discarding != self->asioSocket->socket) {
				return;
			}
			if (a_err) {
				self->closeNow();
			} else {
				self->discardNext();
			}
		});
	}

	void ConnectionSocket::closeNow() {
		closeSocket(asioSocket->socket);
		outbox.clear();
		discarded = {};
		asioSocket->closeDeadline.cancel();
		asioSocket->deadline.cancel();
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
		asio::post(asioSocket->ioService, [self = shared_from_this()] { self->resumeReading(); });
	}

	void ConnectionSocket::writeNext() {
		auto message = outbox.front();
		auto writing = asioSocket->socket;
		asio::async_write(*writing, asio::buffer(message->wire()), [self = shared_from_this(), message, writing](asio::error_code a_err, size_t) {
			if (writing != self->asioSocket->socket) {
				return;
			}
			self->outbox.pop_front();
			if (a_err) {
				self->handleError(a_err, "write");
				self->outbox.clear();
			}
			if (!self->outbox.empty()) {
				self->writeNext();
			} else if (self->closing) {
				self->linger();
			}
		});
	}

	void ConnectionSocket::initiateRead() {
		reading = true;
		auto message = std::make_shared<NetworkMessage>();
		auto readSocket = asioSocket->socket;
		asio::async_read(*readSocket, asio::buffer(message->headerBuffer), [self = shared_from_this(), message, readSocket](const asio::error_code& a_err, size_t) {
			if (readSocket != self->asioSocket->socket) {
				return;
			}
			if (self->closing) {
				self->stopReading();
				return;
			}
			if (a_err) {
				self->reading = false;
				self->handleError(a_err, "header");
				return;
			}
			auto contentSize = message->headerValue();
			auto maxMessageBytes = self->ourLimits.maxMessageBytes(self->ourAuthenticated);
			if (contentSize > maxMessageBytes) {
				self->reading = false;
				self->reject(ProtocolCode::MESSAGE_TOO_LARGE, maxMessageBytes, "A " + std::to_string(contentSize) + " byte message exceeds the " + std::to_string(maxMessageBytes) + " byte limit.");
				return;
			}
			message->content.resize(contentSize);
			self->awaitingContent = true;
			self->armDeadline(self->ourLimits.unreadTimeout);
			asio::async_read(*readSocket, asio::buffer(message->content), [self, message, readSocket](const asio::error_code& a_err, size_t) {
				if (readSocket != self->asioSocket->socket) {
					return;
				}
				self->awaitingContent = false;
				if (self->closing) {
					self->stopReading();
					return;
				}
				self->disarmDeadline();
				if (a_err) {
					self->reading = false;
					self->handleError(a_err, "content");
					return;
				}
				self->receive(message);
			});
		});
	}

	void ConnectionSocket::stopReading() {
		reading = false;
		if (lingering) {
			shutdownTls();
		}
	}

	void ConnectionSocket::receive(const std::shared_ptr<NetworkMessage> &a_message) {
		bool paused = false;
		{
			std::lock_guard<std::mutex> guard(inboxLock);
			inbox.push_back(a_message);
			unreadBytes += unreadCost(*a_message);
			readingPaused = unreadBytes >= ourLimits.maxUnreadBytes(ourAuthenticated);
			paused = readingPaused;
		}
		if (paused) {
			reading = false;
			armDeadline(ourLimits.unreadTimeout);
		} else {
			initiateRead();
		}
	}

	void ConnectionSocket::resumeReading() {
		if (asioSocket->socket && !closing) {
			disarmDeadline();
			initiateRead();
		}
	}

	void ConnectionSocket::armDeadline(std::chrono::milliseconds a_timeout) {
		asioSocket->deadline.expires_after(a_timeout);
		asioSocket->deadline.async_wait([self = shared_from_this()](const asio::error_code& a_err) {
			// A deadline that was moved after it fired is not the one that fired.
			if (!a_err && self->asioSocket->socket && !self->closing && self->asioSocket->deadline.expiry() <= asio::steady_timer::clock_type::now()) {
				self->deadlineReached();
			}
		});
	}

	void ConnectionSocket::disarmDeadline() {
		asioSocket->deadline.expires_at(asio::steady_timer::time_point::max());
	}

	void ConnectionSocket::deadlineReached() {
		if (handshaking) {
			error("[handshake] ", ourPeerAddress, " -> no TLS handshake within ", ourLimits.handshakeTimeout.count(), " ms");
			ourFailed = true;
			closeNow();
			return;
		}
		auto timeout = static_cast<uint32_t>(ourLimits.unreadTimeout.count());
		if (awaitingContent) {
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
		if (asioSocket->socket && !closing) {
			push(NetworkMessage::make(ProtocolFrame {a_code, a_value, a_reason}));
			beginClose();
		}
	}

	void ConnectionSocket::handleError(const asio::error_code &a_err, const std::string &a_section) {
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
