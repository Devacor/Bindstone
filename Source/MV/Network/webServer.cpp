#include "webServer.h"
#include "MV/Network/networkStream.h"
#include "MV/Utility/require.hpp"

#include <jaiscript/thread_pool.hpp>

#include <algorithm>
#include <sstream>

namespace MV {

	WebServer::WebServer(const std::string& a_address, uint16_t a_port, std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory, int a_totalThreads) :
		handlers(std::make_unique<jai::thread_pool>(static_cast<size_t>(std::max(a_totalThreads, 1)))),
		network(std::make_unique<NetworkContext>(1)),
		connectionStateFactory(a_connectionStateFactory) {

		initializeThreadPool(*handlers);
		auto address = SocketAddress::parse(a_address, a_port);
		require<ResourceException>(address.has_value(), "WebServer: [", a_address, "] is not an IPv4 or IPv6 address");
		listener = NetworkListener::make(network->reactor(), *address, [this](Socket a_socket) { accept(std::move(a_socket)); });
		ourPort = listener->port();
		info("WebServer: Accepting Clients on port ", ourPort);
	}

	// The network thread stops first, then the handler threads finish what they run; answers they send are discarded.
	WebServer::~WebServer() {
		info("WebServer::~WebServer");
		network->stop();
	}

	void WebServer::accept(Socket a_socket) {
		auto stream = NetworkStream::make(network->reactor(), std::move(a_socket));
		auto connection = std::make_shared<WebConnection>(network->reactor(), stream, *handlers);
		connection->initialize(connectionStateFactory);
		connection->initiateRead();
	}

	WebConnection::WebConnection(NetworkReactor &a_reactor, std::shared_ptr<NetworkStream> a_stream, jai::thread_pool &a_handlers) :
		reactor(a_reactor),
		mailbox(a_reactor.mailbox()),
		stream(std::move(a_stream)),
		handlers(a_handlers) {
		MV::info("Opened Connection: ", ++ConnectionCount);
	}

	void WebConnection::initialize(std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory) {
		ourState = a_connectionStateFactory(shared_from_this());
		mailbox->post([self = shared_from_this()] { self->resetTimeout(); });
	}

	WebConnection::~WebConnection() {
		MV::info("Closed Connection: ", --ConnectionCount);
	}

	void WebConnection::close() {
		if (!completed.exchange(true)) {
			mailbox->post([self = shared_from_this()] { self->closeNow(); });
		}
	}

	void WebConnection::closeNow() {
		reactor.cancel(timeout);
		timeout = 0;
		stream->close();
	}

	// After the response the send side shuts and the connection closes when the client does (or at the timeout).
	void WebConnection::send(const HttpResponse &a_content) {
		mailbox->post([self = shared_from_this(), content = a_content.to_string()] {
			if (self->completed) {
				return;
			}
			self->resetTimeout();
			self->stream->write(content);
			self->stream->finish([self] {
				if (!self->completed.exchange(true)) {
					self->closeNow();
				}
			});
		});
	}

	void WebConnection::initiateRead() {
		mailbox->post([self = shared_from_this()] {
			self->resetTimeout();
			self->stream->onInput([self] { self->readRequest(); });
			self->stream->onClosed([self](const std::error_code& a_err) { self->handleError(a_err, self->headerParsed ? "body" : "header"); });
			self->stream->reading(true);
		});
	}

	void WebConnection::readRequest() {
		if (completed) {
			return;
		}
		resetTimeout();
		auto limit = ourState->maxRequestBytes();
		if (!headerParsed) {
			auto available = stream->input();
			auto end = available.find("\r\n\r\n");
			if (end == std::string_view::npos || end + 4 > limit) {
				if (available.size() >= limit) {
					error("Refused a request header over the ", limit, " byte limit.");
					close();
				}
				return;
			}
			std::istringstream header(std::string(available.substr(0, end + 4)), std::ios::in | std::ios::binary);
			parsedRequest.readHeaderFromStream(header);
			stream->consume(end + 4);
			headerParsed = true;
		}
		auto expected = parsedRequest.expectedBodyLength;
		if (expected > limit) {
			error("Refused a ", expected, " byte request body over the ", limit, " byte limit.");
			close();
			return;
		}
		body.append(stream->input());
		stream->consume(stream->input().size());
		if (body.size() < expected) {
			return;
		}
		body.resize(expected);
		parsedRequest.postBody = std::move(body);
		body.clear();
		stream->reading(false);
		handlers.submit([self = shared_from_this()] { self->process(); });
	}

	void WebConnection::process() {
		try {
			if (auto response = ourState->processRequest(parsedRequest)) {
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

	void WebConnection::resetTimeout() {
		if (!completed && ourState) {
			reactor.cancel(timeout);
			timeout = reactor.scheduleAfter(std::chrono::duration_cast<std::chrono::milliseconds>(ourState->timeout()), [self = shared_from_this()] {
				self->timeout = 0;
				if (!self->completed) {
					MV::error("Connection Timed Out!");
					self->close();
				}
			});
		}
	}

	void WebConnection::handleError(const std::error_code &a_err, const std::string &a_section) {
		error("[", a_section, "] -> ", a_err.message());
		close();
	}

}
