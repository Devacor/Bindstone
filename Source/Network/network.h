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

		void readHeaderFromBuffer() {
			uint64_t contentSize;
			std::stringstream input;
			input << &buffer;
			input.read(reinterpret_cast<char *>(&contentSize), 8);
			content.resize(contentSize);
			input.clear();
		}

		void readContentFromBuffer() {
			std::stringstream input;
			input << &buffer;
			content = input.str();
		}

		std::string headerAndContent() const {
			uint64_t contentSize = content.size();
			char* headerChar = reinterpret_cast<char *>(&contentSize);
			std::stringstream result;
			result << *headerChar << *(++headerChar) << content;
			return result.str();
		}

		boost::asio::streambuf buffer;
		std::string content;
	};

	class Client : public std::enable_shared_from_this<Client> {
	public:
		Client(const MV::Url& a_url, const std::function<void(const std::string &)> &a_onMessageGet, const std::function<void(const std::string &)> &a_onConnectionFail) :
			resolver(ioService),
			socket(ioService),
			onMessageGet(a_onMessageGet),
			onConnectionFail(a_onConnectionFail),
			work(std::make_unique<boost::asio::io_service::work>(ioService)) {

			try {
				initiateConnection(a_url);
				worker = std::make_unique<std::thread>([this] { ioService.run(); });
			} catch (std::exception &e) {
				socket.close();
				std::cerr << "Exception caught in Client: " << e.what() << std::endl;
			}
		}

		~Client() {
			socket.close();
			ioService.stop();
			worker->join();
		}

		void send(const std::string &a_content) {
			ioService.post([=] {
				auto self = shared_from_this();
				NetworkMessage message(a_content);
				boost::asio::async_write(socket, boost::asio::buffer(message.headerAndContent(), message.content.size()), [self](boost::system::error_code a_err, size_t) {
					if (a_err) {
						self->HandleError(a_err, "write");
					}
				});
			});
		}

		void update() {
			if (!failMessage.empty()) {
				failMessage.clear();
				onConnectionFail(failMessage);
			} else if (!inbox.empty()) {
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

	private:
		void initiateConnection(const MV::Url& a_url) {
			socket.close();

			using boost::asio::ip::tcp;

			tcp::resolver::query query(a_url.host(), "bindstone");
			resolver.async_resolve({ a_url.host(), "" }, [=](const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
				if (a_err) {
					tcp::endpoint endpoint = *endpoint_iterator;
					socket.async_connect(endpoint, boost::bind(&Client::handleConnect, shared_from_this(), boost::asio::placeholders::error, ++endpoint_iterator));
				} else {
					HandleError(a_err, "resolve");
				}
			});
		}

		void handleConnect(const boost::system::error_code& a_err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
			if (!a_err) {
				initiateRead();
			} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
				// The connection failed. Try the next endpoint in the list.
				socket.close();
				auto endpoint = *endpoint_iterator;
				socket.async_connect(endpoint, boost::bind(&Client::handleConnect, shared_from_this(), boost::asio::placeholders::error, ++endpoint_iterator));
			} else {
				HandleError(a_err, "connect");
			}
		}

		void initiateRead() {
			auto message = std::make_shared<NetworkMessage>();
			auto self = shared_from_this();
			boost::asio::async_read(socket, message->buffer, boost::asio::transfer_exactly(8), [&, message, self](const boost::system::error_code& a_err, size_t) {
				if (!a_err) {
					message->readHeaderFromBuffer();
					boost::asio::async_read(socket, message->buffer, boost::asio::transfer_exactly(message->content.size()), [&, message, self](const boost::system::error_code& a_err, size_t) {
						if (!a_err) {
							message->readContentFromBuffer();
							{
								std::lock_guard<std::mutex> guard(lock);
								inbox.push_back(message);
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

		void HandleError(const boost::system::error_code &a_err, const std::string &a_section) {
			socket.close();
			failMessage = "[" + a_section + "] ERROR: " + a_err.message();
		}

		std::mutex lock;

		std::unique_ptr<boost::asio::io_service::work> work;
		boost::asio::io_service ioService;
		boost::asio::ip::tcp::resolver resolver;
		boost::asio::ip::tcp::socket socket;

		std::vector<std::shared_ptr<NetworkMessage>> inbox;

		std::function<void(const std::string &)> onMessageGet;

		std::string failMessage;
		std::function<void(const std::string &)> onConnectionFail;

		std::unique_ptr<std::thread> worker;
	};

	class Server {
	public:
		Server(const boost::asio::ip::tcp::endpoint& endpoint):
			acceptor(ioService, endpoint),
			socket(ioService){

			acceptClients();
		}
		void acceptClients() {
// 			acceptor.async_accept(socket, [this](boost::system::error_code ec) {
// 				if (!ec) {
// 					std::make_shared<chat_session>(std::move(socket), room_)->start();
// 				}
// 
// 				acceptClients();
// 			});
// 			
		}

	private:
		boost::asio::io_service ioService;
 		boost::asio::ip::tcp::acceptor acceptor;
 		boost::asio::ip::tcp::socket socket;

	};

};

#endif
