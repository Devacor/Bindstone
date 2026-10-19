// MV::WebServer / MV::DownloadRequest / MV::Email over loopback, Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Network/webServer.h"
#include "MV/Network/download.h"
#include "MV/Network/email.h"
#include "MV/Utility/require.hpp"

#include <asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace jai::foundry;

namespace mv_tests {

namespace web_detail {

class EchoState : public MV::WebConnectionStateBase {
public:
	using WebConnectionStateBase::WebConnectionStateBase;

	std::optional<MV::HttpResponse> processRequest(const MV::HttpRequest& a_request) override {
		auto kind = a_request.type == MV::HttpRequest::Type::POST ? std::string("POST:") : std::string("GET:");
		return MV::HttpResponse::make200(kind + a_request.postBody, a_request.postContentType.empty() ? "text/plain" : a_request.postContentType);
	}
};

std::shared_ptr<MV::WebServer> echoServer() {
	return std::make_shared<MV::WebServer>("127.0.0.1", uint16_t{0}, [](const std::shared_ptr<MV::WebConnection>& a_connection) {
		return std::make_unique<EchoState>(a_connection);
	}, 2);
}

// Answers each connection, in order, with what a_respond returns for its request head (read first when
// a_readsRequestFirst, otherwise empty); an empty answer holds the connection open for a_holdOpen before closing it.
class RawTcpServer {
public:
	explicit RawTcpServer(std::vector<std::function<std::string(const std::string&)>> a_responders, std::chrono::milliseconds a_holdOpen = std::chrono::milliseconds(0), bool a_readsRequestFirst = true) :
		acceptor(io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)),
		responders(std::move(a_responders)),
		holdOpen(a_holdOpen),
		readsRequestFirst(a_readsRequestFirst) {
		worker = std::thread([this] { serve(); });
	}

	~RawTcpServer() {
		asio::error_code ignored;
		acceptor.close(ignored);
		if (worker.joinable()) {
			worker.join();
		}
	}

	uint16_t port() const {
		return acceptor.local_endpoint().port();
	}

	std::vector<std::string> requests() {
		std::lock_guard<std::mutex> guard(lock);
		return seen;
	}

private:
	void serve() {
		for (auto&& respond : responders) {
			asio::ip::tcp::socket socket(io);
			asio::error_code error;
			acceptor.accept(socket, error);
			if (error) {
				return;
			}
			std::string head;
			if (readsRequestFirst) {
				asio::streambuf buffer;
				asio::read_until(socket, buffer, "\r\n\r\n", error);
				head.assign(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
			}
			{
				std::lock_guard<std::mutex> guard(lock);
				seen.push_back(head);
			}
			auto answer = respond(head);
			if (answer.empty()) {
				std::this_thread::sleep_for(holdOpen);
			} else {
				asio::write(socket, asio::buffer(answer), error);
			}
			socket.shutdown(asio::ip::tcp::socket::shutdown_both, error);
		}
	}

	asio::io_context io;
	asio::ip::tcp::acceptor acceptor;
	std::vector<std::function<std::string(const std::string&)>> responders;
	std::chrono::milliseconds holdOpen;
	bool readsRequestFirst;
	std::thread worker;
	std::mutex lock;
	std::vector<std::string> seen;
};

std::string localUrl(uint16_t a_port, const std::string& a_path) {
	return "http://127.0.0.1:" + std::to_string(a_port) + a_path;
}

}

class web_suite : public suite {
public:
	web_suite() : suite("MV Web") {}

	void forge_tests() override {
		using namespace web_detail;

		test("a serialized response ends exactly at its body", [this]() {
			auto wire = MV::HttpResponse::make200("payload", "text/plain").to_string();
			check_true(wire.size() >= 7 && wire.compare(wire.size() - 7, 7, "payload") == 0, "nothing follows the body: [" + wire.substr(wire.size() > 12 ? wire.size() - 12 : 0) + "]");
			check_true(wire.find("content-length: 7\r\n") != std::string::npos, "the length matches the body");
		});

		test("a serialized POST carries its own Content-Type and ends exactly at its body", [this]() {
			std::ostringstream wire;
			MV::HttpRequest(MV::Url("http://example.com/api"), std::string("{\"a\":1}"), "application/json").writeRequest(wire);
			auto text = wire.str();
			check_true(text.find("Content-Type: application/json\r\n") != std::string::npos, "the request names its content type");
			check_true(text.size() >= 7 && text.compare(text.size() - 7, 7, "{\"a\":1}") == 0, "nothing follows the body");
		});

		test("a blocking GET returns the web server's body exactly, with nothing after it", [this]() {
			auto server = echoServer();
			auto result = MV::downloadBlocking(MV::Url(localUrl(server->port(), "/hello")));
			check_true(result.ok(), "the download succeeds: " + result.errorMessage);
			check_eq(std::string("GET:"), result.response.body);
			check_eq(200, result.response.status);
		});

		test("downloadStringBlocking returns the body of a 2xx response and nullopt for anything else", [this]() {
			auto server = echoServer();
			check_true(MV::downloadStringBlocking(MV::Url(localUrl(server->port(), "/hello"))) == std::optional<std::string>("GET:"), "the body of a 200");
			RawTcpServer raw({[](const std::string&) {
				return std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\nConnection: close\r\n\r\nnope");
			}});
			check_true(!MV::downloadStringBlocking(MV::Url(localUrl(raw.port(), "/missing"))).has_value(), "nothing for a 404");
		});

		test("a 404 is a received response that is not ok, and the transfer itself reports no error", [this]() {
			RawTcpServer raw({[](const std::string&) {
				return std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\nConnection: close\r\n\r\nnope");
			}});
			auto result = MV::downloadBlocking(MV::Url(localUrl(raw.port(), "/missing")));
			check_true(result.received(), "the whole response arrived");
			check_true(!result.ok(), "a 404 is not ok");
			check_true(result.error == MV::DownloadError::NONE, "the transfer did not fail");
			check_eq(404, result.response.status);
			check_eq(std::string("nope"), result.response.body);
		});

		test("a POST body of hundreds of KiB, zero bytes included, reaches the server whole and echoes back byte for byte", [this]() {
			auto server = echoServer();
			std::string body(300 * 1024, 'x');
			for (size_t i = 0; i < body.size(); i += 997) {
				body[i] = '\0';
			}
			auto result = MV::downloadBlocking(MV::HttpRequest(MV::Url(localUrl(server->port(), "/echo")), body, "application/octet-stream"));
			check_true(result.ok(), "the download succeeds: " + result.errorMessage);
			check_true(result.response.body == "POST:" + body, "the echo matches byte for byte");
			check_eq(std::string("application/octet-stream"), result.response.headerValues["content-type"]);
		});

		test("a pooled download reports progress within 0..1, ending at 1, and completes on the pool", [this]() {
			auto server = echoServer();
			auto pool = MV::DownloadPool::make(1);
			std::mutex lock;
			std::vector<float> progress;
			std::promise<std::shared_ptr<MV::DownloadRequest>> finished;
			MV::DownloadOptions options;
			options.onProgress = [&](size_t, float a_fraction) {
				std::lock_guard<std::mutex> guard(lock);
				progress.push_back(a_fraction);
			};
			pool->download(MV::HttpRequest(MV::Url(localUrl(server->port(), "/big")), std::string(512 * 1024, 'p'), "text/plain"), [&](const std::shared_ptr<MV::DownloadRequest>& a_request) {
				finished.set_value(a_request);
			}, options);
			auto result = finished.get_future();
			check_true(result.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "the download completes");
			auto download = result.get();
			check_true(download->complete(), "it says it is complete");
			check_true(download->result().ok(), "the download succeeds");
			std::lock_guard<std::mutex> guard(lock);
			check_true(!progress.empty(), "progress was reported");
			check_true(std::all_of(progress.begin(), progress.end(), [](float a_value) { return a_value >= 0.0f && a_value <= 1.0f; }), "progress stays within 0..1");
			check_true(std::is_sorted(progress.begin(), progress.end()), "progress never goes backwards");
			check_eq(1.0f, progress.back());
		});

		test("with a callback queue, progress and completion run only when the queue executes, on its thread", [this]() {
			auto server = echoServer();
			auto pool = MV::DownloadPool::make(1);
			auto queue = std::make_shared<MV::CallbackQueue>();
			std::atomic<int> progressCalls {0};
			std::atomic<bool> completed {false};
			std::thread::id completedOn;
			MV::DownloadOptions options;
			options.callbackQueue = queue;
			options.onProgress = [&](size_t, float) { ++progressCalls; };
			auto download = pool->download(MV::HttpRequest(MV::Url(localUrl(server->port(), "/queued")), std::string(256 * 1024, 'q'), "text/plain"), [&](const std::shared_ptr<MV::DownloadRequest>&) {
				completedOn = std::this_thread::get_id();
				completed = true;
			}, options);
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!download->complete() && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			check_true(download->complete(), "the download finishes");
			check_true(!completed && progressCalls == 0, "nothing ran before the queue executed");
			queue->execute();
			check_true(completed.load(), "completion ran when the queue executed");
			check_true(progressCalls > 0, "progress ran when the queue executed");
			check_true(completedOn == std::this_thread::get_id(), "on the thread that executed the queue");
		});

		test("dropping the pool cancels a download in flight, whose callback still runs, and nothing joins itself", [this]() {
			RawTcpServer raw({[](const std::string&) { return std::string(); }}, std::chrono::milliseconds(500));
			auto pool = MV::DownloadPool::make(1);
			std::promise<MV::DownloadError> finished;
			// Unheld here, so the download, and with it the pool's context, dies on that context's own worker
			pool->download(MV::Url(localUrl(raw.port(), "/slow")), [&](const std::shared_ptr<MV::DownloadRequest>& a_request) {
				finished.set_value(a_request->result().error);
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			pool.reset();
			auto result = finished.get_future();
			check_true(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "the callback runs");
			check_true(result.get() == MV::DownloadError::CANCELLED, "with CANCELLED");
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		});

		test("a chunked response is decoded into its body", [this]() {
			RawTcpServer raw({[](const std::string&) {
				return std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\n6; note=1\r\n world\r\n0\r\n\r\n");
			}});
			auto result = MV::downloadBlocking(MV::Url(localUrl(raw.port(), "/chunked")));
			check_true(result.ok(), "the download succeeds: " + result.errorMessage);
			check_eq(std::string("hello world"), result.response.body);
		});

		test("a relative redirect is followed against the current URL and a fresh response replaces the first", [this]() {
			RawTcpServer raw({[](const std::string&) {
				return std::string("HTTP/1.1 302 Found\r\nLocation: /final?x=1\r\nContent-Length: 7\r\nConnection: close\r\n\r\nmoved!!");
			}, [](const std::string&) {
				return std::string("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\ndone");
			}});
			auto result = MV::downloadBlocking(MV::Url(localUrl(raw.port(), "/start")));
			check_true(result.ok(), "the download succeeds: " + result.errorMessage);
			check_eq(std::string("done"), result.response.body);
			check_true(result.response.headerValues.count("location") == 0, "no header of the redirect survives");
			check_eq(static_cast<size_t>(1), result.redirects.size());
			check_eq(std::string("/start"), result.redirects[0].pathAndQuery());
			check_eq(std::string("/final?x=1"), result.finalUrl.pathAndQuery());
			auto requests = raw.requests();
			check_eq(static_cast<size_t>(2), requests.size());
			check_true(requests[1].rfind("GET /final?x=1 ", 0) == 0, "the second request asks for the redirect's target: " + requests[1].substr(0, 40));
		});

		test("a server that never answers fails the download at its timeout", [this]() {
			RawTcpServer raw({[](const std::string&) { return std::string(); }}, std::chrono::milliseconds(3000));
			MV::DownloadOptions options;
			options.timeout = std::chrono::milliseconds(300);
			auto started = std::chrono::steady_clock::now();
			auto result = MV::downloadBlocking(MV::Url(localUrl(raw.port(), "/silent")), options);
			auto elapsed = std::chrono::steady_clock::now() - started;
			check_true(result.error == MV::DownloadError::TIMED_OUT, "it times out: " + result.errorMessage);
			check_true(elapsed < std::chrono::seconds(2), "it fails at the timeout, not when the server gives up");
		});

		test("a body shorter than its Content-Length fails the download instead of passing as complete", [this]() {
			RawTcpServer raw({[](const std::string&) {
				return std::string("HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabcd");
			}});
			auto result = MV::downloadBlocking(MV::Url(localUrl(raw.port(), "/short")));
			check_true(result.error == MV::DownloadError::TRUNCATED_BODY, "it is truncated");
			check_true(result.errorMessage.find("4 of 10") != std::string::npos, "it says how much arrived: " + result.errorMessage);
		});

		test("a body over maxBodyBytes fails the download", [this]() {
			auto server = echoServer();
			MV::DownloadOptions options;
			options.maxBodyBytes = 1024;
			auto result = MV::downloadBlocking(MV::HttpRequest(MV::Url(localUrl(server->port(), "/large")), std::string(64 * 1024, 'l'), "text/plain"), options);
			check_true(result.error == MV::DownloadError::BODY_TOO_LARGE, "it is too large: " + result.errorMessage);
		});

		test("a request header over the server's limit closes the connection unanswered", [this]() {
			class TinyLimitState : public EchoState {
			public:
				using EchoState::EchoState;
				size_t maxRequestBytes() const override { return 512; }
			};
			auto server = std::make_shared<MV::WebServer>("127.0.0.1", uint16_t{0}, [](const std::shared_ptr<MV::WebConnection>& a_connection) {
				return std::make_unique<TinyLimitState>(a_connection);
			}, 1);
			MV::DownloadOptions options;
			options.timeout = std::chrono::milliseconds(3000);
			auto result = MV::downloadBlocking(MV::Url(localUrl(server->port(), "/" + std::string(2000, 'a'))), options);
			check_true(!result.received(), "the oversized request gets no answer");
		});

		test("a state that answers later, from another thread, reaches the client", [this]() {
			class DeferredState : public MV::WebConnectionStateBase {
			public:
				using WebConnectionStateBase::WebConnectionStateBase;

				std::optional<MV::HttpResponse> processRequest(const MV::HttpRequest&) override {
					std::thread([connection = connection()] {
						std::this_thread::sleep_for(std::chrono::milliseconds(50));
						connection->send(MV::HttpResponse::make200("later", "text/plain"));
					}).detach();
					return std::nullopt;
				}
			};
			auto server = std::make_shared<MV::WebServer>("127.0.0.1", uint16_t{0}, [](const std::shared_ptr<MV::WebConnection>& a_connection) {
				return std::make_unique<DeferredState>(a_connection);
			}, 1);
			auto result = MV::downloadBlocking(MV::Url(localUrl(server->port(), "/later")));
			check_true(result.ok(), "the deferred answer arrives: " + result.errorMessage);
			check_eq(std::string("later"), result.response.body);
		});

		test("an SMTP server that refuses the greeting fails the email as REJECTED with its reply", [this]() {
			RawTcpServer raw({[](const std::string&) { return std::string("554 no service here\r\n"); }}, std::chrono::milliseconds(0), false);
			auto email = MV::Email::make("127.0.0.1", std::to_string(raw.port()), {"user", "password"});
			auto result = email->sendBlocking({"from@example.com", "to@example.com"}, "title", "body");
			check_true(email->complete(), "the email is complete");
			check_true(result.error == MV::EmailError::REJECTED, "it was rejected: " + result.errorMessage);
			check_eq(554, result.lastReply.status);
			check_eq(std::string("no service here"), result.lastReply.message);
		});

		test("an Email sends one message: a second send throws", [this]() {
			RawTcpServer raw({[](const std::string&) { return std::string("554 no\r\n"); }}, std::chrono::milliseconds(0), false);
			auto email = MV::Email::make("127.0.0.1", std::to_string(raw.port()), {"user", "password"});
			email->sendBlocking({"from@example.com", "to@example.com"}, "title", "body");
			bool threw = false;
			try {
				email->sendBlocking({"from@example.com", "to@example.com"}, "title", "body");
			} catch (MV::LogicException&) {
				threw = true;
			}
			check_true(threw, "the second send throws");
		});
	}
};

}

using web_suite = mv_tests::web_suite;
FOUNDRY_REGISTER(web_suite)
