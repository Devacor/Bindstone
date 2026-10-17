#include "network.h"
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
	builder.auto_bind();

	builder.method("send", &MV::Client::send);
	builder.method("connected", &MV::Client::connected);
	builder.method("disconnect", [](MV::Client& self) { self.disconnect(); });
	builder.method("reconnect", [](MV::Client& self) { self.reconnect(); });
});

namespace MV {

	Client::~Client() {
		ourConnectionState = DISCONNECTED;
		ioService.stop();
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

	void Client::handleError(const asio::error_code &a_err, const std::string &a_section) {
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
		if (ioService.get_executor().running_in_this_thread()) {
			closeNow();
		} else {
			asio::post(ioService, [this] { closeNow(); });
		}
	}

	void Client::closeNow() {
		closeSocket(socket);
		outbox.clear();
	}

	void Client::initiateConnection() {
		auto expected = DISCONNECTED;
		if (ourConnectionState.compare_exchange_strong(expected, CONNECTING)) {
			disconnectRequested = false;
			connectionAnnounced = false;
			{
				std::lock_guard<std::mutex> guard(errorLock);
				failMessage.clear();
			}
			asio::post(ioService, [this] {
				if (ourConnectionState != CONNECTING) {
					return;
				}
				auto attempt = std::make_shared<asio::ip::tcp::socket>(ioService);
				socket = attempt;
				asio::ip::tcp::resolver::query query(url.host(), std::to_string(url.port()), asio::ip::tcp::resolver::query::canonical_name);
				resolver.async_resolve(query, [this, attempt](const asio::error_code& a_err, asio::ip::tcp::resolver::iterator a_endpoints) {
					if (attempt != socket) {
						return;
					}
					if (a_err) {
						handleError(a_err, "resolve");
					} else {
						connectTo(attempt, a_endpoints);
					}
				});
			});
		}
	}

	void Client::connectTo(const std::shared_ptr<asio::ip::tcp::socket>& a_attempt, asio::ip::tcp::resolver::iterator a_endpoints) {
		asio::ip::tcp::endpoint endpoint = *a_endpoints;
		a_attempt->async_connect(endpoint, [this, a_attempt, next = ++a_endpoints](const asio::error_code& a_err) {
			handleConnect(a_attempt, a_err, next);
		});
	}

	void Client::handleConnect(const std::shared_ptr<asio::ip::tcp::socket>& a_attempt, const asio::error_code& a_err, asio::ip::tcp::resolver::iterator a_nextEndpoint) {
		if (a_attempt != socket) {
			return;
		}
		if (!a_err) {
			auto expected = CONNECTING;
			if (ourConnectionState.compare_exchange_strong(expected, CONNECTING_SUCCESS)) {
				initiateRead();
			}
		} else if (a_nextEndpoint != asio::ip::tcp::resolver::iterator()) {
			info("Trying next endpoint: ", a_err.message());
			auto retry = std::make_shared<asio::ip::tcp::socket>(ioService);
			socket = retry;
			connectTo(retry, a_nextEndpoint);
		} else {
			handleError(a_err, "connect");
		}
	}

	void Client::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto reading = socket;
		asio::async_read(*reading, asio::buffer(message->headerBuffer, 4), asio::transfer_exactly(4), [this, message, reading](const asio::error_code& a_err, size_t) {
			if (reading != socket) {
				return;
			}
			if (a_err) {
				handleError(a_err, "header");
				return;
			}
			message->readHeaderFromBuffer();
			asio::async_read(*reading, message->buffer, asio::transfer_exactly(message->content.size()), [this, message, reading](const asio::error_code& a_err, size_t) {
				if (reading != socket) {
					return;
				}
				if (a_err) {
					handleError(a_err, "content");
				} else if (ourConnectionState != DISCONNECTED) {
					message->readContentFromBuffer();
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
				worker = std::make_unique<std::thread>([this] { ioService.run(); });
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
		if (connectionAnnounced && !disconnectRequested) {
			for (auto&& message : received) {
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
				onConnectionFail(failure);
			}
		}
	}

	void Client::send(const std::string &a_content) {
		require<DeviceException>(connected(), "Failed to send message due to lack of connection.");
		auto message = std::make_shared<NetworkMessage>(a_content);
		asio::post(ioService, [this, message] {
			if (socket) {
				outbox.push_back(message);
				if (outbox.size() == 1) { writeNext(); }
			}
		});
	}

	// One async_write in flight per socket: overlapping writes interleave their bytes on the wire.
	void Client::writeNext() {
		auto message = outbox.front();
		auto writing = socket;
		asio::async_write(*writing, asio::buffer(message->wire()), [this, message, writing](asio::error_code a_err, size_t) {
			if (writing != socket) {
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

	Server::Server(const asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<ConnectionStateBase>(const std::shared_ptr<Connection> &)> a_connectionStateFactory) :
		acceptor(ioService),
		connectionStateFactory(a_connectionStateFactory),
		work(std::make_unique<asio::io_context::work>(ioService)) {
		acceptor.open(a_endpoint.protocol());
#ifdef _WIN32
		// asio's endpoint constructor would set SO_REUSEADDR, which on Windows lets another process
		// bind a specific address over this wildcard port and capture its loopback connections
		acceptor.set_option(asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>(true));
#else
		acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
#endif
		acceptor.bind(a_endpoint);
		acceptor.listen();
		ourPort = acceptor.local_endpoint().port();
		info("Server Startup");
		acceptClients();
		info("Accept Clients");
		worker = std::make_unique<std::thread>([this] { ioService.run(); });
	}

	Server::~Server() {
		info("Server::~Server");
		ioService.stop();
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
		auto socket = std::make_shared<asio::ip::tcp::socket>(ioService);
		acceptor.async_accept(*socket, [this, socket](asio::error_code ec) {
			if (!ec) {
				//socket->set_option(asio::ip::tcp::no_delay(true));
				auto connectionSocket = std::make_shared<ConnectionSocket>(socket, ioService);
				connectionSocket->start();
				std::lock_guard<std::mutex> guard(acceptedLock);
				accepted.push_back(connectionSocket);
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
	}

	ConnectionSocket::ConnectionSocket(const std::shared_ptr<asio::ip::tcp::socket> &a_socket, asio::io_context& a_ioService) :
		ioService(a_ioService),
		socket(a_socket),
		closeDeadline(a_ioService),
		ourLoopback(remoteIsLoopback(*a_socket)) {
	}

	void ConnectionSocket::start() {
		initiateRead();
	}

	void ConnectionSocket::send(const std::string &a_content) {
		auto message = std::make_shared<NetworkMessage>(a_content);
		asio::post(ioService, [self = shared_from_this(), message] {
			if (self->socket && !self->closing) {
				self->outbox.push_back(message);
				if (self->outbox.size() == 1) { self->writeNext(); }
			}
		});
	}

	// Messages sent before the close still go out (a kick says why): the socket closes once the outbox
	// drains, or at the deadline when the peer stops reading.
	void ConnectionSocket::close() {
		asio::post(ioService, [self = shared_from_this()] {
			if (self->closing || !self->socket) {
				return;
			}
			self->closing = true;
			if (self->outbox.empty()) {
				self->closeNow();
			} else {
				self->closeDeadline.expires_after(std::chrono::seconds(2));
				self->closeDeadline.async_wait([self](const asio::error_code&) { self->closeNow(); });
			}
		});
	}

	void ConnectionSocket::closeNow() {
		closeSocket(socket);
		outbox.clear();
		closeDeadline.cancel();
	}

	std::vector<std::shared_ptr<NetworkMessage>> ConnectionSocket::takeInbox() {
		std::vector<std::shared_ptr<NetworkMessage>> result;
		std::lock_guard<std::mutex> guard(inboxLock);
		result.swap(inbox);
		return result;
	}

	void ConnectionSocket::writeNext() {
		auto message = outbox.front();
		auto writing = socket;
		asio::async_write(*writing, asio::buffer(message->wire()), [self = shared_from_this(), message, writing](asio::error_code a_err, size_t) {
			if (writing != self->socket) {
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
				self->closeNow();
			}
		});
	}

	void ConnectionSocket::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto reading = socket;
		asio::async_read(*reading, asio::buffer(message->headerBuffer), asio::transfer_exactly(4), [self = shared_from_this(), message, reading](const asio::error_code& a_err, size_t) {
			if (reading != self->socket) {
				return;
			}
			if (a_err) {
				self->handleError(a_err, "header");
				return;
			}
			message->readHeaderFromBuffer();
			asio::async_read(*reading, message->buffer, asio::transfer_exactly(message->content.size()), [self, message, reading](const asio::error_code& a_err, size_t) {
				if (reading != self->socket) {
					return;
				}
				if (a_err) {
					self->handleError(a_err, "content");
					return;
				}
				message->readContentFromBuffer();
				{
					std::lock_guard<std::mutex> guard(self->inboxLock);
					self->inbox.push_back(message);
				}
				self->initiateRead();
			});
		});
	}

	void ConnectionSocket::handleError(const asio::error_code &a_err, const std::string &a_section) {
		error("[", a_section, "] -> ", a_err.message());
		ourFailed = true;
	}

	uint32_t NetworkMessage::sizeFromHeaderBuffer() {
		uint32_t headerSizeResult = 0;
		memcpy(&headerSizeResult, &headerBuffer[0], 4);
		return headerSizeResult;
	}

	void NetworkMessage::readHeaderFromBuffer() {
		swapBytesForNetwork<4>(&headerBuffer[0]);
		content.resize(sizeFromHeaderBuffer());
	}

	void NetworkMessage::readContentFromBuffer() {
		std::stringstream input;
		input << &buffer;
		content = input.str();
	}

	void NetworkMessage::pushSizeToHeaderBuffer() {
		uint32_t contentSize = static_cast<uint32_t>(content.size());

		uint8_t* headerChar = reinterpret_cast<uint8_t *>(&contentSize);
		swapBytesForNetwork<4>(headerChar);
		for (int i = 0; i < 4; ++i, ++headerChar) {
			headerBuffer[i] = *headerChar;
		}
	}

	std::string NetworkMessage::headerAndContent() const {
		uint32_t contentSize = static_cast<uint32_t>(content.size());

		uint8_t* headerChar = reinterpret_cast<uint8_t *>(&contentSize);
		swapBytesForNetwork<4>(headerChar);
		
		std::stringstream result;
		for (int i = 0; i < 4; ++i) {
			result << *(headerChar++);
		}
		result << content;
		return result.str();
	}

	uint32_t NetworkMessage::headerAndContentSize() const {
		return 4 + static_cast<uint32_t>(content.size());
	}

	// Owned by the message for the whole async_write: a temporary would be freed mid-send.
	const std::string& NetworkMessage::wire() {
		if (wireBytes.empty()) { wireBytes = headerAndContent(); }
		return wireBytes;
	}

}
