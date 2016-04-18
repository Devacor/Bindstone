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
		char headerBuffer[4];

		std::string content;
	};

	class Client : public std::enable_shared_from_this<Client> {
	public:
		static std::shared_ptr<Client> make(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail) {
			auto self = std::shared_ptr<Client>(new Client(a_url, a_onMessageGet, a_onConnectionFail));
			self->initialize();
			return self;
		}

		~Client() {
			if (socket) {
				socket->close();
			}
			ioService.stop();
			worker->join();
		}

		void send(const std::string &a_content);

		void update();

	private:
		Client(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail) :
			url(a_url),
			resolver(ioService),
			onMessageGet(a_onMessageGet),
			onConnectionFail(a_onConnectionFail),
			work(std::make_unique<boost::asio::io_service::work>(ioService)) {
		}

		void initialize();

		void initiateConnection();

		void handleConnect(const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);

		void initiateRead();

		void HandleError(const boost::system::error_code &a_err, const std::string &a_section) {
			socket->close();
			socket.reset();
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

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<boost::asio::io_service::work> work;
	};

	class Server;

	class Connection : public std::enable_shared_from_this<Connection> {
	public:
		Connection(Server& a_server, const std::shared_ptr<boost::asio::ip::tcp::socket> &a_socket, boost::asio::io_service& a_ioService) :
			server(a_server),
			socket(a_socket),
			ioService(a_ioService){
		}

		~Connection() {
			socket->close();
		}

		void send(const std::string &a_content);

		void initiateRead();
		void update();
	private:
		std::mutex lock;

		void HandleError(const boost::system::error_code &a_err, const std::string &a_section) {
			socket->close();
			std::cerr << "[" << a_section << "] ERROR: " << a_err.message() << std::endl;
		}

		Server& server;
		boost::asio::io_service& ioService;

		std::shared_ptr<boost::asio::ip::tcp::socket> socket;
		std::vector<std::shared_ptr<NetworkMessage>> inbox;
	};

	class Server {
		friend Connection;
	public:
		Server(const boost::asio::ip::tcp::endpoint& a_endpoint, std::function<void (const std::string &, Connection*)> a_onMessageGet);

		~Server() {
			ioService.stop();
			worker->join();
		}

		void disconnect(Connection* a_connection) {
			std::lock_guard<std::mutex> guard(lock);
			connections.erase(std::remove_if(connections.begin(), connections.end(), [&](auto &c) {return c.get() == a_connection; }), connections.end());
		}

		void send(const std::string &a_message);
		void update();
	private:
		void acceptClients();

		std::mutex lock;

		boost::asio::io_service ioService;
 		boost::asio::ip::tcp::acceptor acceptor;
		std::vector<std::shared_ptr<Connection>> connections;

		std::unique_ptr<std::thread> worker;
		std::unique_ptr<boost::asio::io_service::work> work;

		std::function<void(const std::string &, Connection*)> onMessageGet;
	};

};

#endif
