#include "webServer.h"

#include <asio.hpp>
#include <algorithm>

namespace MV {

	namespace {
		using tcp = asio::ip::tcp;
	}

	struct WebServer::AsioWebAcceptor {
		AsioWebAcceptor() :
			acceptor(ioContext),
			keepRunning(asio::make_work_guard(ioContext)) {
		}

		asio::io_context ioContext;
		tcp::acceptor acceptor;
		asio::executor_work_guard<asio::io_context::executor_type> keepRunning;
	};

	struct WebConnection::AsioWebSocket {
		AsioWebSocket(asio::io_context& a_ioContext, std::shared_ptr<tcp::socket> a_socket) :
			strand(asio::make_strand(a_ioContext)),
			socket(std::move(a_socket)),
			timeout(strand) {
		}

		asio::strand<asio::io_context::executor_type> strand;
		std::shared_ptr<tcp::socket> socket;
		asio::steady_timer timeout;
	};

	struct WebConnection::WebActiveRequestState {
		explicit WebActiveRequestState(size_t a_maxRequestBytes) :
			buffer(a_maxRequestBytes) {
		}

		void readHeaderFromBuffer() {
			std::istream is(&buffer);
			parsedRequest.readHeaderFromStream(is);
		}

		void moveBufferToBody() {
			auto data = buffer.data();
			body.append(asio::buffers_begin(data), asio::buffers_end(data));
			buffer.consume(buffer.size());
		}

		HttpRequest parsedRequest;
		asio::streambuf buffer;
		std::string body;
	};

	WebServer::WebServer(const std::string& a_address, uint16_t a_port, std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory, int a_totalThreads) :
		asioAcceptor(std::make_unique<AsioWebAcceptor>()),
		connectionStateFactory(a_connectionStateFactory) {
		const tcp::endpoint endpoint(asio::ip::make_address(a_address), a_port);
		auto& acceptor = asioAcceptor->acceptor;
		acceptor.open(endpoint.protocol());
#ifdef _WIN32
		// SO_REUSEADDR on Windows would let another process bind a specific address over this port (see network.cpp's Server)
		acceptor.set_option(asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>(true));
#else
		acceptor.set_option(tcp::acceptor::reuse_address(true));
#endif
		acceptor.bind(endpoint);
		acceptor.listen();
		ourPort = acceptor.local_endpoint().port();
		acceptClients();
		info("WebServer: Accepting Clients on port ", ourPort);
		for (int i = 0; i < a_totalThreads; ++i) {
			workers.push_back(std::make_unique<std::thread>([this] { asioAcceptor->ioContext.run(); }));
		}
	}

	WebServer::~WebServer() {
		info("WebServer::~WebServer");
		asioAcceptor->keepRunning.reset();
		asioAcceptor->ioContext.stop();
		for (auto&& worker : workers) {
			if (worker && worker->joinable()) { worker->join(); }
		}
	}

	void WebServer::acceptClients() {
		auto socket = std::make_shared<tcp::socket>(asioAcceptor->ioContext);
		asioAcceptor->acceptor.async_accept(*socket, [this, socket](asio::error_code ec) {
			if (!ec) {
				auto connection = std::make_shared<WebConnection>(std::make_unique<WebConnection::AsioWebSocket>(asioAcceptor->ioContext, socket));
				connection->initialize(connectionStateFactory);
				connection->initiateRead();
			}
			if (ec != asio::error::operation_aborted) {
				acceptClients();
			}
		});
	}

	WebConnection::WebConnection(std::unique_ptr<AsioWebSocket> a_asioSocket) :
		asioSocket(std::move(a_asioSocket)) {
		MV::info("Opened Connection: ", ++ConnectionCount);
	}

	void WebConnection::initialize(std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory) {
		ourState = a_connectionStateFactory(shared_from_this());
		asio::post(asioSocket->strand, [self = shared_from_this()] { self->resetTimeout(); });
	}

	// No handler holds this connection any more, so nothing else touches the socket
	WebConnection::~WebConnection() {
		if (!completed.exchange(true)) {
			closeNow();
		}
		MV::info("Closed Connection: ", --ConnectionCount);
	}

	void WebConnection::close() {
		if (!completed.exchange(true)) {
			asio::post(asioSocket->strand, [self = shared_from_this()] { self->closeNow(); });
		}
	}

	void WebConnection::closeNow() {
		asio::error_code ignored;
		asioSocket->timeout.cancel();
		if (asioSocket->socket) {
			asioSocket->socket->shutdown(tcp::socket::shutdown_both, ignored);
			asioSocket->socket->close(ignored);
		}
	}

	void WebConnection::send(const HttpResponse &a_content) {
		auto content = std::make_shared<std::string>(a_content.to_string());
		asio::post(asioSocket->strand, [self = shared_from_this(), content] {
			if (self->completed) {
				return;
			}
			self->resetTimeout();
			asio::async_write(*self->asioSocket->socket, asio::buffer(*content), asio::bind_executor(self->asioSocket->strand, [self, content](asio::error_code a_err, size_t) {
				if (a_err) {
					self->handleError(a_err, "write");
				} else {
					self->close();
				}
			}));
		});
	}

	void WebConnection::handleReadContent(std::shared_ptr<WebActiveRequestState> a_message, const std::error_code& err) {
		resetTimeout();
		if (!err) {
			continueReadingContent(a_message);
		} else {
			handleError(err, "body");
		}
	}

	void WebConnection::continueReadingContent(std::shared_ptr<WebActiveRequestState> a_message) {
		a_message->moveBufferToBody();
		auto expected = a_message->parsedRequest.expectedBodyLength;
		if (expected > ourState->maxRequestBytes()) {
			error("Refused a ", expected, " byte request body over the ", ourState->maxRequestBytes(), " byte limit.");
			close();
			return;
		}
		if (a_message->body.size() < expected) {
			auto self = shared_from_this();
			asio::async_read(*asioSocket->socket, a_message->buffer, asio::transfer_at_least(1), asio::bind_executor(asioSocket->strand, [self, a_message](const asio::error_code& a_err, size_t) {
				self->handleReadContent(a_message, a_err);
			}));
			return;
		}
		a_message->body.resize(expected);
		a_message->parsedRequest.postBody = std::move(a_message->body);
		try {
			if (auto response = ourState->processRequest(a_message->parsedRequest)) {
				send(*response);
			}
		} catch (std::exception& e) {
			error("Failed to processRequest and reply: ", e.what());
			close();
		} catch (...) {
			error("Failed to processRequest and reply: Unknown Exception");
			close();
		}
	}

	void WebConnection::initiateRead() {
		asio::post(asioSocket->strand, [self = shared_from_this()] {
			self->resetTimeout();
			auto message = std::make_shared<WebActiveRequestState>(self->ourState->maxRequestBytes());
			asio::async_read_until(*self->asioSocket->socket, message->buffer, "\r\n\r\n", asio::bind_executor(self->asioSocket->strand, [self, message](const asio::error_code& a_err, size_t) {
				if (self->completed) {
					return;
				}
				if (a_err) {
					self->handleError(a_err, "header");
					return;
				}
				message->readHeaderFromBuffer();
				self->continueReadingContent(message);
			}));
		});
	}

	void WebConnection::resetTimeout() {
		if (!completed && ourState) {
			asioSocket->timeout.expires_after(ourState->timeout());
			asioSocket->timeout.async_wait(asio::bind_executor(asioSocket->strand, [self = shared_from_this()](const asio::error_code& a_err) {
				self->checkTimeout(a_err);
			}));
		}
	}

	void WebConnection::checkTimeout(const std::error_code& a_ec) {
		// A timer moved after it fired is not the one that fired
		if (!completed && a_ec != asio::error::operation_aborted && asioSocket->timeout.expiry() <= std::chrono::steady_clock::now()) {
			MV::error("Connection Timed Out!");
			close();
		}
	}

	void WebConnection::handleError(const std::error_code &a_err, const std::string &a_section) {
		error("[", a_section, "] -> ", a_err.message());
		close();
	}

}
