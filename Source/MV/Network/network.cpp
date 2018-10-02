#include "network.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Utility/scopeGuard.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/portable_binary.hpp"

namespace MV {

	void Client::initiateConnection() {
		if (ourConnectionState == DISCONNECTED) {
			ourConnectionState = CONNECTING;
			socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
			boost::asio::ip::tcp::resolver::query query(url.host(), std::to_string(url.port()), boost::asio::ip::tcp::resolver::query::canonical_name);
			auto self = shared_from_this();
			resolver.async_resolve(query, [=](const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
				if (!a_err && socket) {
					boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
					socket->async_connect(endpoint, boost::bind(&Client::handleConnect, self, boost::asio::placeholders::error, ++endpoint_iterator));
				} else {
					handleError(a_err, "resolve");
				}
			});
		}
	}

	void Client::handleConnect(const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		if (ourConnectionState == CONNECTING) {
			if (!a_err) {
				//socket->set_option(boost::asio::ip::tcp::no_delay(true));
				ourConnectionState = CONNECTING_SUCCESS;
				initiateRead();
			}
			else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
				// Try the next endpoint in the list.
				info("Trying next endpoint: ", a_err.message());
				socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
				auto endpoint = *endpoint_iterator;
				auto self = shared_from_this();
				socket->async_connect(endpoint, boost::bind(&Client::handleConnect, self, boost::asio::placeholders::error, ++endpoint_iterator));
			}
			else {
				handleError(a_err, "connect");
			}
		}
	}

	void Client::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto self = shared_from_this();
		auto copiedSocket = socket;
		boost::asio::async_read(*copiedSocket, boost::asio::buffer(message->headerBuffer, 4), boost::asio::transfer_exactly(4), [&, message, self, copiedSocket](const boost::system::error_code& a_err, size_t a_amount) {
			if (!a_err && socket) {
				message->readHeaderFromBuffer();
				boost::asio::async_read(*copiedSocket, message->buffer, boost::asio::transfer_exactly(message->content.size()), [&, message, self, copiedSocket](const boost::system::error_code& a_err, size_t a_amount) {
					if (!a_err && socket) {
						if (ourConnectionState != DISCONNECTED) {
							message->readContentFromBuffer();
							{
								std::lock_guard<std::recursive_mutex> guard(lock);
								inbox.push_back(message);
							}
							initiateRead();
						}
					} else if (socket) {
						handleError(a_err, "content");
					}
				});
			} else if (socket) {
				handleError(a_err, "header");
			}
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


	void Client::disconnectIfFailed() {
		std::unique_lock<std::mutex> guard(errorLock);
		if (!failMessage.empty()) {
			auto failMessageCached = failMessage;
			failMessage.clear();
			guard.unlock();

			if (onConnectionFail) {
				onConnectionFail(failMessageCached);
			}
			disconnect();
		}
	}

	void Client::update() {
		disconnectIfFailed();

		if (ourConnectionState == DISCONNECTED) {
			closeSocket(socket);
			inbox.clear();
		} else {
			tryInitializeCallback();
			if (!inbox.empty()) {
				std::lock_guard<std::recursive_mutex> guard(lock);

				for (auto&& message : inbox) {
					try {
						onMessageGet(message->content);
					} catch (std::exception &e) {
						error("Exception caught in network message handler: ", e.what());
					}
				}
				inbox.clear();
			}
		}
	}

	void Client::send(const std::string &a_content) {
		require<DeviceException>(connected(), "Failed to send message due to lack of connection.");
		auto self = shared_from_this();
		ioService.post([=] {
			self;
			auto message = std::make_shared<NetworkMessage>(a_content);
			boost::asio::async_write(*socket, boost::asio::buffer(message->headerAndContent(), message->headerAndContentSize()), [self, message](boost::system::error_code a_err, size_t a_amount) {
				if (a_err) {
					self->handleError(a_err, "write");
				}
			});
		});
	}

	void Client::tryInitializeCallback() {
		if (ourConnectionState == CONNECTING_SUCCESS) {
			ourConnectionState = CONNECTED;
			if (onInitialized) {
				try {
					onInitialized();
				} catch (std::exception &e) {
					error("Exception caught in network initialization call: ", e.what());
				}
			}
		}
	}

	Server::Server(const boost::asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<ConnectionStateBase>(const std::shared_ptr<Connection> &)> a_connectionStateFactory) :
		acceptor(ioService, a_endpoint),
		connectionStateFactory(a_connectionStateFactory),
		work(std::make_unique<boost::asio::io_context::work>(ioService)) {

		acceptClients();
		worker = std::make_unique<std::thread>([this] { ioService.run(); });
	}

	Server::~Server() {
		info("Server::~Server");
		ioService.stop();
		if (worker && worker->joinable()) { worker->join(); }
	}

	void Server::sendAll(const std::string &a_message) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		for (auto&& connection : ourConnections) {
			connection->send(a_message);
		}
	}

	void Server::sendExcept(const std::string &a_message, Connection* a_exceptConnection) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		for (auto&& connection : ourConnections) {
			if (connection.get() != a_exceptConnection) {
				connection->send(a_message);
			}
		}
	}

	void Server::acceptClients() {
		auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
		acceptor.async_accept(*socket, [this, socket](boost::system::error_code ec) {
			if (!ec) {
				//socket->set_option(boost::asio::ip::tcp::no_delay(true));
				auto connection = std::make_shared<Connection>(*this, socket, ioService);
				connection->initialize(connectionStateFactory);
				{
					std::lock_guard<std::recursive_mutex> guard(lock);
					ourConnections.push_back(connection);
				}
				connection->initiateRead();
			}

			acceptClients();
		});
	}

	void Server::update(double a_dt) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto startSize = ourConnections.size();
		ourConnections.erase(std::remove_if(ourConnections.begin(), ourConnections.end(), [&](auto c) {
			if (!c) { return true; }
			try {
				c->update(a_dt);
			} catch (std::exception &e) {
				error("Exception for connection update: ", e.what());
			} catch (...) {
				error("Unknown Exception for connection update!");
			}
			return c->disconnected();
		}), ourConnections.end());
		if (startSize != ourConnections.size()) {
			info("Connections lost: ", startSize - ourConnections.size());
		}
	}

	void Connection::send(const std::string &a_content) {
		auto self = shared_from_this();
		ioService.post([&, self, a_content] {
			auto message = std::make_shared<NetworkMessage>(a_content);
			boost::asio::async_write(*socket, boost::asio::buffer(message->headerAndContent(), message->headerAndContentSize()), [self, message](boost::system::error_code a_err, size_t a_amount) {
				if (a_err) {
					self->handleError(a_err, "write");
				}
			});
		});
	}

	void Connection::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto self = shared_from_this();
		auto copiedSocket = socket;
		boost::asio::async_read(*copiedSocket, boost::asio::buffer(message->headerBuffer, 4), boost::asio::transfer_exactly(4), [&, message, self, copiedSocket](const boost::system::error_code& a_err, size_t a_amount) {
			if (!a_err && socket) {
				message->readHeaderFromBuffer();
				boost::asio::async_read(*copiedSocket, message->buffer, boost::asio::transfer_exactly(message->content.size()), [&, message, self, copiedSocket](const boost::system::error_code& a_err, size_t a_amount) {
					if (!a_err && socket) {
						message->readContentFromBuffer();
						{
							std::lock_guard<std::recursive_mutex> guard(lock);
							inbox.push_back(message);
						}
						initiateRead();
					} else if(socket) {
						handleError(a_err, "content");
					}
				});
			} else if(socket) {
				handleError(a_err, "header");
			}
		});
	}

	void Connection::update(double a_dt) {
		if (ourState->disconnected()) {
			closeSocket(socket);
			inbox.clear();
			return;
		}
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto self = shared_from_this();
		for (auto&& message : inbox) {
			try {
				ourState->message(message->content);
			} catch (std::exception &e) {
				error("Caught an exception in Connection::update: ", e.what());
				disconnect();
				return;
			}
		}
		inbox.clear();
		ourState->update(a_dt);
	}

	void Connection::handleError(const boost::system::error_code &a_err, const std::string &a_section) {
		error("[", a_section, "] -> ", a_err.message());
		disconnect();
	}

	uint32_t NetworkMessage::sizeFromHeaderBuffer() {
		return *reinterpret_cast<uint32_t*>(headerBuffer);
	}

	void NetworkMessage::readHeaderFromBuffer() {
		swapBytesForNetwork<4>(headerBuffer);
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

}