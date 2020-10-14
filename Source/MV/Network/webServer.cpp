#include "webServer.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Utility/scopeGuard.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/portable_binary.hpp"

namespace MV {

	WebServer::WebServer(const boost::asio::ip::tcp::endpoint& a_endpoint, std::function<std::unique_ptr<WebConnectionStateBase>(const std::shared_ptr<WebConnection> &)> a_connectionStateFactory, int a_totalThreads) :
		acceptor(ioService, a_endpoint),
		connectionStateFactory(a_connectionStateFactory),
		work(std::make_unique<boost::asio::io_context::work>(ioService)) {
		acceptClients();
		info("WebServer: Accepting Clients");
		for (int i = 0; i < a_totalThreads; ++i) {
			workers.push_back(std::make_unique<std::thread>([this] { ioService.run(); }));
		}
	}
	
	WebServer::~WebServer() {
		info("WebServer::~WebServer");
		ioService.stop();
		for (auto&& worker : workers) {
			if (worker && worker->joinable()) { worker->join(); }
		}
	}

	void WebServer::acceptClients() {
		auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
		acceptor.async_accept(*socket, [this, socket](boost::system::error_code ec) {
			if (!ec) {
				auto connection = std::make_shared<WebConnection>(*this, socket, ioService);
				connection->initialize(connectionStateFactory);
				connection->initiateRead();
			}

			acceptClients();
		});
	}

	void WebConnection::close() {
		if (!completed) {
			completed = true;
			timeout.cancel();
			boost::system::error_code ignored_ec;
			if (socket) { socket->close(ignored_ec); }
		}
	}

	void WebConnection::send(const HttpResponse &a_content) {
		resetTimeout();
		auto self = shared_from_this();

		ioService.post([this, self, a_content] {
			std::string content = a_content.to_string();
			boost::asio::async_write(*socket, boost::asio::buffer(content, content.size()), [self](boost::system::error_code a_err, size_t a_amount) {
				if (a_err) {
					self->handleError(a_err, "write");
				} else {
					self->close();
				}
			});
		});
	}

	void WebConnection::initiateReadingMoreContent(std::shared_ptr<WebActiveRequestState> a_message, size_t a_minimumTransferAmount) {
		boost::asio::async_read(*socket, a_message->buffer, boost::asio::transfer_at_least(a_minimumTransferAmount), std::bind(&WebConnection::handleReadContent, shared_from_this(), a_message, std::placeholders::_1));
	}

	void WebConnection::handleReadContent(std::shared_ptr<WebActiveRequestState> a_message, const boost::system::error_code& err) {
		resetTimeout();

		if (!err) {
			continueReadingContent(a_message);
		} else if (err != boost::asio::error::eof) {
			handleError(err, "Download Read Content Failure");
		}
	}

	void WebConnection::continueReadingContent(std::shared_ptr<WebActiveRequestState> a_message) {
		size_t amountReadSoFar = a_message->parsedRequest.postBody.size() + a_message->buffer.size();
		size_t amountLeftToRead = 1;

		if (a_message->parsedRequest.expectedBodyLength) {
			amountLeftToRead = a_message->parsedRequest.expectedBodyLength - std::min(amountReadSoFar, a_message->parsedRequest.expectedBodyLength);
		}

		bool ableToReadData = a_message->readResponseToStream();

		if (ableToReadData && amountLeftToRead > 0) {
			initiateReadingMoreContent(a_message, a_message->parsedRequest.expectedBodyLength ? std::min(ChunkSize, amountLeftToRead) : 1);
		} else {
			a_message->parsedRequest.postBody = a_message->streamOutput.str();
			a_message->streamOutput.clear();
			try { 
				ourState->processRequest(a_message->parsedRequest); 
			} catch (std::runtime_error& e) {
				error("Failed to processRequest and reply: ", e.what());
				close();
			} catch (...) {
				error("Failed to processRequest and reply: Unknown Exception");
				close();
			}
		}
	}

	void WebConnection::initiateRead() {
		resetTimeout();
		auto message = std::make_shared<WebActiveRequestState>();
		auto self = shared_from_this();
		auto copiedSocket = socket;

		boost::asio::async_read_until(*copiedSocket, message->buffer, "\r\n\r\n", [this, message, self, copiedSocket](const boost::system::error_code& a_err, size_t a_amount) {
			if (!a_err && socket) {
				message->readHeaderFromBuffer();
				continueReadingContent(message);
			} else if(socket) {
				handleError(a_err, "header");
			}
		});
	}

	void WebConnection::resetTimeout() {
		if (!completed && ourState) {
			timeout.cancel();
			timeout.expires_from_now(ourState->timeout());
			timeout.async_wait(std::bind(&WebConnection::checkTimeout, shared_from_this(), std::placeholders::_1));
		}
	}

	void WebConnection::handleError(const boost::system::error_code &a_err, const std::string &a_section) {
		error("[", a_section, "] -> ", a_err.message());
		close();
	}

	void WebConnection::WebActiveRequestState::readHeaderFromBuffer() {
		std::istream is(&buffer);
		parsedRequest.readHeaderFromStream(is);
	}

	bool WebConnection::WebActiveRequestState::readResponseToStream() {
		if (buffer.size() > 0) {
			if (buffer.size() > 1) {
				streamOutput << &buffer;
				return true;
			} else {
				char input = getResponseChar();
				if (input != 0) {
					streamOutput << input;
					return true;
				}
			}
		}
		return false;
	}

	char WebConnection::WebActiveRequestState::getResponseChar() {
		if (buffer.size() == 1) {
			std::ostringstream result;
			result << &buffer;
			return result.str()[0];
		}
		return '!';
	}

}