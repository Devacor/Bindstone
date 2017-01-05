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
#include "Network/url.h"
#include "Utility/generalUtility.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "chaiscript/chaiscript.hpp"

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

		boost::asio::streambuf buffer;
		uint8_t headerBuffer[4];

		std::string content;
	};

	class Client : public std::enable_shared_from_this<Client> {
	public:
		static const int EXPECTED_TIMESTEPS = 5;

		static std::shared_ptr<Client> make(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized = std::function<void ()>()) {
			auto self = std::shared_ptr<Client>(new Client(a_url, a_onMessageGet, a_onConnectionFail, a_onInitialized));
			self->initialize();
			return self;
		}

		~Client() {
			socket.reset();
			ioService.stop();
			if (worker && worker->joinable()) {
				worker->detach(); //getting errors if we join for some reason? :<
			}
		}

		void send(const std::string &a_content);

		void update();

		void reconnect() {
			initialize();
		}

		void reconnect(const std::function<void()> &a_onInitialized) {
			onInitialized = a_onInitialized;
			initialize();
		}

		bool connected() const{
			return remainingTimeDeltas <= 0;
		}

		double clientServerTime() const {
			return clientServerTimeValue;
		}

		static void hook(chaiscript::ChaiScript& a_script) {
			a_script.add(chaiscript::user_type<Client>(), "Client");

			a_script.add(chaiscript::fun(&Client::send), "send");
			a_script.add(chaiscript::fun(&Client::connected), "connected");
			a_script.add(chaiscript::fun([](Client& a_self) { a_self.reconnect(); }), "reconnect");
			a_script.add(chaiscript::fun([](Client& a_self, const std::function<void()> &a_onInitialized) { a_self.reconnect(a_onInitialized); }), "reconnect");
			
			a_script.add(chaiscript::fun(&Client::clientServerTime), "clientServerTime");
		}

	private:
		Client(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail, const std::function<void()> &a_onInitialized) :
			url(a_url),
			resolver(ioService),
			onMessageGet(a_onMessageGet),
			onConnectionFail(a_onConnectionFail),
			onInitialized(a_onInitialized),
			work(std::make_unique<boost::asio::io_service::work>(ioService)) {
		}

		void initialize();
		void tryInitializeCallback();

		void initiateConnection();

		void handleConnect(const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);

		void initiateRead();

		void appendClientServerTime(std::shared_ptr<NetworkMessage> message);

		void HandleError(const boost::system::error_code &a_err, const std::string &a_section) {
			if (socket) {
				socket->close();
				socket.reset();
			}
			remainingTimeDeltas = EXPECTED_TIMESTEPS;
			failMessage = "[" + a_section + "] ERROR: " + a_err.message();
			std::cerr << failMessage << std::endl;
		}

		MV::Url url;

		std::mutex lock;

		boost::asio::io_service ioService;
		boost::asio::ip::tcp::resolver resolver;
		std::shared_ptr<boost::asio::ip::tcp::socket> socket;

		std::vector<std::shared_ptr<NetworkMessage>> inbox;

		std::function<void(const std::string &)> onMessageGet;

		std::string failMessage;
		std::function<void(const std::string &)> onConnectionFail;

		std::function<void()> onInitialized;

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<boost::asio::io_service::work> work;

		std::vector<double> serverTimeDeltas;
		bool hasBeenInitialized = false;
		int remainingTimeDeltas = EXPECTED_TIMESTEPS;
		double clientServerTimeValue = 0.0;
	};

	class Server;
	class Connection;

	//derive from this in your own code.
	class ConnectionStateBase {
	public:
		ConnectionStateBase(const std::shared_ptr<Connection>& a_connection) :
			ourConnection(a_connection) {
		}

		void connect() { disconnectRecieved = false; connectImplementation(); }
		void disconnect() { disconnectRecieved = true; disconnectImplementation(); }

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

	class Connection : public std::enable_shared_from_this<Connection> {
	public:
		Connection(Server& a_server, const std::shared_ptr<boost::asio::ip::tcp::socket> &a_socket, boost::asio::io_service& a_ioService) :
			server(a_server),
			socket(a_socket),
			ioService(a_ioService){
		}

		//can't happen in the constructor due to shared_from_this
		void initialize(std::function<std::unique_ptr<ConnectionStateBase>(const std::shared_ptr<Connection> &)> a_connectionStateFactory) {
			ourState = a_connectionStateFactory(shared_from_this());
		}

		~Connection() {
			socket.reset();
		}

		void send(const std::string &a_content);

		void initiateRead();
		void update(double a_dt);
		void sendTimeStamp();

		ConnectionStateBase* state() const {
			return ourState.get();
		}

		void disconnect() {
			ourState->disconnect();
		}

		bool disconnected() const {
			return ourState->disconnected();
		}
	private:
		std::mutex lock;

		void HandleError(const boost::system::error_code &a_err, const std::string &a_section);

		Server& server;
		boost::asio::io_service& ioService;

		std::shared_ptr<boost::asio::ip::tcp::socket> socket;
		std::vector<std::shared_ptr<NetworkMessage>> inbox;

		std::unique_ptr<ConnectionStateBase> ourState = nullptr;

		int timeRequestsRemaining = Client::EXPECTED_TIMESTEPS;
	};

	class Server {
		friend Connection;
	public:
		Server(const boost::asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<ConnectionStateBase> (const std::shared_ptr<Connection> &)> a_connectionStateFactory);

		~Server();

		void send(const std::string &a_message);
		void sendExcept(const std::string &a_message, Connection* a_connectionToSkip);
		void update(double a_dt);

		size_t activeConnections() const {
			return connections.size();
		}
	private:
		void acceptClients();

		std::mutex lock;

		boost::asio::io_service ioService;
 		boost::asio::ip::tcp::acceptor acceptor;

		std::vector<std::shared_ptr<Connection>> connections;

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<boost::asio::io_service::work> work;

		std::function<std::unique_ptr<ConnectionStateBase> (const std::shared_ptr<Connection> &)> connectionStateFactory;

		uint32_t randomSeed;
	};

};

#endif
