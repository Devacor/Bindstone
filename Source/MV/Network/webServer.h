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

	std::optional<MV::HttpResponse> processRequest(const MV::HttpRequest& a_request) override {
		return MV::HttpResponse::make200("<html><body>Hello World!</body></html>");
	}
};

int main(){
	auto webServer = std::make_shared<MV::WebServer>("0.0.0.0", uint16_t{80},
		[](const std::shared_ptr<MV::WebConnection>& a_connection) {
			return std::make_unique<HelloWorldResponder>(a_connection);
		});

	std::this_thread::sleep_for(std::chrono::hours(24)); //serves on its own threads until it is destroyed
}
\*******************************************************************************************************************************/

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include "MV/Network/url.h"
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

		virtual ~WebConnectionStateBase() = default;

		// Holding the shared_ptr keeps the connection alive through long running work, even if the peer disconnects.
		std::shared_ptr<WebConnection> connection() const { return ourConnection.lock(); }

		// Returns the response to send now, or nullopt to answer later: then hold connection() and call its send() or
		// close() when the work is done.
		virtual std::optional<HttpResponse> processRequest(const HttpRequest& a_request) {
			return HttpResponse::make404();
		}

		//override to supply a custom timeout.
		virtual std::chrono::seconds timeout() const {
			return std::chrono::seconds{ 10 };
		}

		// A request whose header or body is larger than this closes the connection unanswered.
		virtual size_t maxRequestBytes() const {
			return 8 * 1024 * 1024;
		}

	protected:
		std::weak_ptr<WebConnection> ourConnection;
	};

	//Handles a single HTTP Request and response. Closes the connection upon successfully sending, or upon an error or timeout.
	//Every handler of one connection runs on its strand; send() and close() may be called from any thread.
	class WebConnection : public std::enable_shared_from_this<WebConnection> {
	public:
		struct AsioWebSocket;

		explicit WebConnection(std::unique_ptr<AsioWebSocket> a_asioSocket);

		//can't happen in the constructor due to shared_from_this
		void initialize(std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory);

		~WebConnection();

		void send(const HttpResponse& a_content);

		void initiateRead();

		WebConnectionStateBase* state() const {
			return ourState.get();
		}

		void close();

	private:
		struct WebActiveRequestState;

		void handleReadContent(std::shared_ptr<WebActiveRequestState> a_message, const std::error_code& err);
		void continueReadingContent(std::shared_ptr<WebActiveRequestState> a_message);

		void handleError(const std::error_code &a_err, const std::string &a_section);

		void resetTimeout();
		void checkTimeout(const std::error_code& a_ec);
		void closeNow();

		std::unique_ptr<AsioWebSocket> asioSocket;

		std::atomic<bool> completed = false;

		std::unique_ptr<WebConnectionStateBase> ourState = nullptr;

		inline static std::atomic<int64_t> ConnectionCount = 0;
	};

	class WebServer {
	public:
		// a_address is an IPv4 or IPv6 literal ("0.0.0.0" listens everywhere, "127.0.0.1" on this machine only); port 0 picks a free one.
		WebServer(const std::string& a_address, uint16_t a_port, std::function<std::unique_ptr<WebConnectionStateBase> (const std::shared_ptr<WebConnection> &)> a_connectionStateFactory, int a_totalThreads = 8);
		~WebServer();

		uint16_t port() const {
			return ourPort;
		}

	private:
		struct AsioWebAcceptor;

		void acceptClients();

		std::unique_ptr<AsioWebAcceptor> asioAcceptor;
		uint16_t ourPort = 0;

		std::vector<std::unique_ptr<std::thread>> workers;

		std::function<std::unique_ptr<WebConnectionStateBase> (const std::shared_ptr<WebConnection> &)> connectionStateFactory;
	};
};

#endif
