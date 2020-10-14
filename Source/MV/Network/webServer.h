#ifndef _MV_WEBSERVER_H_
#define _MV_WEBSERVER_H_

/*******************************************************************************************************************************\
|                                 Michael Hamilton (maxmike@gmail.com) www.mutedvision.net                                      |
|-------------------------------------------------------------------------------------------------------------------------------|
WebServer Example:

#include "webServer.h"

class HelloWorldResponder : public MV::WebConnectionStateBase {
public:
	using WebConnectionStateBase::WebConnectionStateBase;

	void processRequest(const MV::HttpRequest& a_recieve) override {
		connection()->send(MV::HttpResponse::make200("<html><body>Hello World!</body></head>"));
	}
};

int main(){
	auto webServer = std::make_shared<MV::WebServer>(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 80),
		[](const std::shared_ptr<MV::WebConnection>& a_connection) {
			return std::make_unique<HelloWorldResponder>(a_connection);
		});

	while (true){} //loop forever to accept infinite connections while the program runs, or write an exit condition if you like.
}
\*******************************************************************************************************************************/

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
#include <optional>
#include "MV/Network/url.h"
#include "MV/Utility/generalUtility.h"
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include "MV/Utility/log.h"
#include "MV/Network/download.h"

namespace MV {
	class WebServer;
	class WebConnection;

	//derive from this in your own code.
	class WebConnectionStateBase {
	public:
		WebConnectionStateBase(const std::shared_ptr<WebConnection>& a_connection) :
			ourConnection(a_connection) {
		}

		//Get a shared_ptr handle keep the connection alive in long running requests even if disconnected.
		std::shared_ptr<WebConnection> connection() { return ourConnection.lock(); }

		//must either call "connection()->send(HttpResponse::make404())" or "connection()->close()" to gracefully exit.
		//If performing asynchronous work, capture a copy of "connection()" to keep the socket alive.
		inline virtual void processRequest(const HttpRequest& a_recieve);

		//override to supply a custom timeout.
		virtual boost::posix_time::seconds timeout() const {
			return boost::posix_time::seconds{ 10 };
		}

	protected:
		std::weak_ptr<WebConnection> ourConnection;
	};

	//Handles a single HTTP Request and response. Closes the connection upon successfully sending, or upon an error or timeout.
	class WebConnection : public std::enable_shared_from_this<WebConnection> {
	public:
		WebConnection(WebServer& a_server, const std::shared_ptr<boost::asio::ip::tcp::socket> &a_socket, boost::asio::io_context& a_ioService) :
			server(a_server),
			socket(a_socket),
			ioService(a_ioService),
			timeout(a_ioService){
			MV::info("Opened Connection: ", ++ConnectionCount);
		}

		//can't happen in the constructor due to shared_from_this
		void initialize(std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory) {
			ourState = a_connectionStateFactory(shared_from_this());
			resetTimeout();
		}

		~WebConnection() {
			close();
			MV::info("Closed Connection: ", --ConnectionCount);
		}

		void send(const HttpResponse& a_content);

		void initiateRead();

		WebConnectionStateBase* state() const {
			return ourState.get();
		}

		void close();

	private:
		struct WebActiveRequestState {
			WebActiveRequestState() :
				streamOutput(std::ofstream::out | std::ofstream::binary) {
			}
			void readHeaderFromBuffer();
			bool readResponseToStream();
			char getResponseChar();

			HttpRequest parsedRequest;

			boost::asio::streambuf buffer;
			std::ostringstream streamOutput;
		};

		void handleReadContent(std::shared_ptr<WebActiveRequestState> a_message, const boost::system::error_code& err);
		void initiateReadingMoreContent(std::shared_ptr<WebActiveRequestState> a_message, size_t minimumTransferAmount);
		void continueReadingContent(std::shared_ptr<WebActiveRequestState> a_message);

		std::recursive_mutex lock;

		void handleError(const boost::system::error_code &a_err, const std::string &a_section);

		void resetTimeout();

		WebServer& server;
		boost::asio::io_context& ioService;

		std::atomic<bool> completed = false;

		std::shared_ptr<boost::asio::ip::tcp::socket> socket;
		std::vector<std::shared_ptr<WebActiveRequestState>> inbox;

		std::unique_ptr<WebConnectionStateBase> ourState = nullptr;

		boost::asio::deadline_timer timeout;

		void checkTimeout(const boost::system::error_code& a_ec) {
			if (!completed && a_ec != boost::asio::error::operation_aborted) {
				if (timeout.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
					MV::error("Connection Timed Out!");
					close();
				}
			}
		}

		inline static int64_t ConnectionCount = 0;
		static constexpr size_t ChunkSize = 256;
	};


	inline void WebConnectionStateBase::processRequest(const HttpRequest& a_recieve) {
		connection()->send(HttpResponse::make404());
	}

	class WebServer {
		friend WebConnection;
	public:
		WebServer(const boost::asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<WebConnectionStateBase> (const std::shared_ptr<WebConnection> &)> a_connectionStateFactory, int a_totalThreads = 8);
		~WebServer();

		uint16_t port() {
			return acceptor.local_endpoint().port();
		}

	private:
		void acceptClients();

		boost::asio::io_context ioService;
 		boost::asio::ip::tcp::acceptor acceptor;

		std::vector<std::unique_ptr<std::thread>> workers;
		std::unique_ptr<boost::asio::io_context::work> work;

		std::function<std::unique_ptr<WebConnectionStateBase> (const std::shared_ptr<WebConnection> &)> connectionStateFactory;
		double accumulatedTime = 0.0;
	};
};

#endif
