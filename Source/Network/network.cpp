#include "network.h"
#include "Utility/stopwatch.h"
#include "Utility/scopeGuard.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/portable_binary.hpp"

namespace MV {

	void Client::initiateConnection() {
		if (socket) {
			socket->close();
		}
		remainingTimeDeltas = EXPECTED_TIMESTEPS;
		socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);

		boost::asio::ip::tcp::resolver::query query(url.host(), std::to_string(url.port()));
		resolver.async_resolve(query, [=](const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
			if (!a_err) {
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket->async_connect(endpoint, boost::bind(&Client::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
			} else {
				HandleError(a_err, "resolve");
			}
		});
	}

	void Client::handleConnect(const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		if (!a_err) {
			serverTimeDeltas.push_back(Stopwatch::systemTime());
			initiateRead();
		} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
			// The connection failed. Try the next endpoint in the list.
			std::cerr << "Trying next endpoint: " << a_err.message() << std::endl;
			socket->close();
			auto endpoint = *endpoint_iterator;
			socket->async_connect(endpoint, boost::bind(&Client::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
		} else {
			HandleError(a_err, "connect");
		}
	}

	void Client::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto self = shared_from_this();
		boost::asio::async_read(*socket, boost::asio::buffer(message->headerBuffer, 4), boost::asio::transfer_exactly(4), [&, message, self](const boost::system::error_code& a_err, size_t a_amount) {
			if (!a_err) {
				message->readHeaderFromBuffer();
				boost::asio::async_read(*socket, message->buffer, boost::asio::transfer_exactly(message->content.size()), [&, message, self](const boost::system::error_code& a_err, size_t a_amount) {
					if (!a_err) {
						message->readContentFromBuffer();
						{
							std::lock_guard<std::mutex> guard(lock);
							if (remainingTimeDeltas > 0) {
								appendClientServerTime(message);
							} else {
								inbox.push_back(message);
							}
						}
						initiateRead();
					} else {
						HandleError(a_err, "content");
					}
				});
			} else {
				HandleError(a_err, "header");
			}
		});
	}

	void Client::initialize() {
		try {
			hasBeenInitialized = false;
			initiateConnection();
			worker = std::make_unique<std::thread>([this] { ioService.run(); });
		} catch (std::exception &e) {
			remainingTimeDeltas = EXPECTED_TIMESTEPS;
			socket->close();
			std::cerr << "Exception caught in Client: " << e.what() << std::endl;
		}
	}

	void Client::update() {
		if (!failMessage.empty()) {
			failMessage.clear();
			if (onConnectionFail) {
				onConnectionFail(failMessage);
			}
			onConnectionFail = std::function<void(const std::string &)>();
			onMessageGet = std::function<void(const std::string &)>();
			onInitialized = std::function<void()>();
		} else{
			tryInitializeCallback();
			if (!inbox.empty()) {
				std::lock_guard<std::mutex> guard(lock);

				for (auto&& message : inbox) {
					try {
						onMessageGet(message->content);
					} catch (std::exception &e) {
						std::cerr << "Exception caught in network message handler: " << e.what() << std::endl;
					}
				}
				inbox.clear();
			}
		}
	}

	void Client::send(const std::string &a_content) {
		require<DeviceException>(connected() || a_content == "T", "Failed to send message due to lack of connection.");
		ioService.post([=] {
			auto self = shared_from_this();
			auto message = std::make_shared<NetworkMessage>(a_content);
			boost::asio::async_write(*socket, boost::asio::buffer(message->headerAndContent(), message->headerAndContentSize()), [self, message](boost::system::error_code a_err, size_t a_amount) {
				if (a_err) {
					self->HandleError(a_err, "write");
				}
			}); 
		});
	}

	void Client::appendClientServerTime(std::shared_ptr<NetworkMessage> message) {
		auto currentTime = Stopwatch::systemTime();
		serverTimeDeltas.back() = (currentTime - serverTimeDeltas.back()) / 2.0;
		{
			std::stringstream messageStream(message->content);
			cereal::PortableBinaryInputArchive input(messageStream);
			double serverTimeStamp;
			input(serverTimeStamp);
			serverTimeDeltas.back() = (serverTimeStamp - currentTime) + serverTimeDeltas.back();
		}
		--remainingTimeDeltas;
		if (remainingTimeDeltas > 0) {
			serverTimeDeltas.push_back(Stopwatch::systemTime());
			send("T");
		} else {
			std::sort(serverTimeDeltas.begin(), serverTimeDeltas.end());
			clientServerTimeValue = serverTimeDeltas[serverTimeDeltas.size() / 2];
		}
	}

	void Client::tryInitializeCallback() {
		if (!hasBeenInitialized && connected()) {
			hasBeenInitialized = true;
			if (onInitialized) {
				try {
					onInitialized();
				} catch (std::exception &e) {
					std::cerr << "Exception caught in network initialization call: " << e.what() << std::endl;
				}
			}
		}
	}

	Server::Server(const boost::asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<ConnectionStateBase>(const std::shared_ptr<Connection> &)> a_connectionStateFactory) :
		acceptor(ioService, a_endpoint),
		connectionStateFactory(a_connectionStateFactory),
		work(std::make_unique<boost::asio::io_service::work>(ioService)) {

		acceptClients();
		worker = std::make_unique<std::thread>([this] { ioService.run(); });
	}

	Server::~Server() {
		std::cout << "Server Died" << std::endl;
		ioService.stop();
		if (worker && worker->joinable()) { worker->join(); }
	}

	void Server::send(const std::string &a_message) {
		for (auto&& connection : connections) {
			connection->send(a_message);
		}
	}

	void Server::sendExcept(const std::string &a_message, Connection* a_exceptConnection) {
		for (auto&& connection : connections) {
			if (connection.get() != a_exceptConnection) {
				connection->send(a_message);
			}
		}
	}

	void Server::acceptClients() {
		auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
		acceptor.async_accept(*socket, [this, socket](boost::system::error_code ec) {
			if (!ec) {
				auto connection = std::make_shared<Connection>(*this, socket, ioService);
				connection->initialize(connectionStateFactory);

				std::lock_guard<std::mutex> guard(lock);
				connections.push_back(connection);

				connection->sendTimeStamp();
				connection->initiateRead();
			}

			acceptClients();
		});
	}

	void Connection::sendTimeStamp() {
		--timeRequestsRemaining;
		std::stringstream message;
		{
			cereal::PortableBinaryOutputArchive output(message);
			double serverTime = Stopwatch::systemTime();
			output(serverTime);
		}
		send(message.str());
		if (timeRequestsRemaining == 0) {
			ourState->connect();
		}
	}

	void Server::update(double a_dt) {
		connections.erase(std::remove_if(connections.begin(), connections.end(), [&](auto &c) {
			try {
				c->update(a_dt);
			} catch (std::exception &e) {
				std::cerr << "Exception for connection update: " << e.what() << std::endl;
			} catch (...) {
				std::cerr << "Unknown Exception for connection update!" << std::endl;
			}
			return c->disconnected();
		}), connections.end());
	}

	void Connection::send(const std::string &a_content) {
		ioService.post([=] {
			auto self = shared_from_this();
			auto message = std::make_shared<NetworkMessage>(a_content);
			boost::asio::async_write(*socket, boost::asio::buffer(message->headerAndContent(), message->headerAndContentSize()), [self, message](boost::system::error_code a_err, size_t a_amount) {
				if (a_err) {
					self->HandleError(a_err, "write");
				}
			});
		});
	}

	void Connection::initiateRead() {
		auto message = std::make_shared<NetworkMessage>();
		auto self = shared_from_this();
		boost::asio::async_read(*socket, boost::asio::buffer(message->headerBuffer, 4), boost::asio::transfer_exactly(4), [&, message, self](const boost::system::error_code& a_err, size_t a_amount) {
			if (!a_err) {
				message->readHeaderFromBuffer();
				boost::asio::async_read(*socket, message->buffer, boost::asio::transfer_exactly(message->content.size()), [&, message, self](const boost::system::error_code& a_err, size_t a_amount) {
					if (!a_err) {
						message->readContentFromBuffer();
						{
							std::lock_guard<std::mutex> guard(lock);
							if (timeRequestsRemaining > 0) {
								sendTimeStamp();
							} else {
								inbox.push_back(message);
							}
						}
						initiateRead();
					} else {
						HandleError(a_err, "content");
					}
				});
			} else {
				HandleError(a_err, "header");
			}
		});
	}

	void Connection::update(double a_dt) {
		if (ourState->disconnected()) {
			inbox.clear();
			return;
		}
		std::lock_guard<std::mutex> guard(lock);
		auto self = shared_from_this();
		for (auto&& message : inbox) {
			try {
				ourState->message(message->content);
			} catch (std::exception &e) {
				std::cerr << "Caught an exception in Connection::update: " << e.what() << std::endl;
				ourState->disconnect();
				inbox.clear();
				return;
			}
		}
		inbox.clear();
		ourState->update(a_dt);
	}

	void Connection::HandleError(const boost::system::error_code &a_err, const std::string &a_section) {
		socket->close();
		std::cerr << "[" << a_section << "] ERROR: " << a_err.message() << std::endl;
		ourState->disconnect();
		inbox.clear();
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