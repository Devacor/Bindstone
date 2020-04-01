#include "download.h"
#include <atomic>
#include <sstream>
#include "MV/Utility/stringUtility.h"
#include <thread>

namespace MV {
	void HttpHeader::read(std::istream& response_stream) {
		values.clear();
		response_stream >> version;
		std::string status_code;
		response_stream >> status_code;
		try {
			status = std::stoi(status_code);
		} catch (...) {
			status = 0;
		}
		getline_platform_agnostic(response_stream, message);
		if (!message.empty() && message[0] == ' ') { message = message.substr(1); }
		std::string header;
		while (getline_platform_agnostic(response_stream, header) && !header.empty()) {
			auto index = header.find_first_of(':');
			if (index != std::string::npos && index > 0) {
				auto key = header.substr(0, index);
				auto value = (index + 2 >= header.size()) ? "" : header.substr(index + 2);
				std::transform(key.begin(), key.end(), key.begin(), [](char c) {return std::tolower(c); });
				values[key] = value;
				if (toLower(key) == "content-length") {
					try {
						contentLength = static_cast<size_t>(stol(value));
					} catch (std::exception & e) {
						std::cerr << e.what() << std::endl;
						contentLength = 0;
					}
				}
			}
		}
	}
	std::ostream& operator<<(std::ostream& os, const HttpHeader& obj) {
		os << "\\/______HTTP_HEADER______\\/\nVersion [" << obj.version << "] Status [" << obj.status << "] Message [" << obj.message << "]\n";
		os << "||-----------------------||\n";
		for (auto&& kvp : obj.values) {
			os << "[" << kvp.first << "]: " << kvp.second << "\n";
		}
		os << "\n||--------Bounces--------||\n";
		for (size_t i = 0; i < obj.bounces.size(); ++i) {
			os << i << ": " << obj.bounces[i] << "\n";
		}
		os << "/\\_______________________/\\" << std::endl;
		return os;
	}
	std::istream& operator>>(std::istream& a_is, HttpHeader& a_obj) {
		a_obj.read(a_is);
		return a_is;
	}
	std::string DownloadString(const Url& a_url) {
		auto request = DownloadRequest::make(a_url);
		if (request->success()) {
			return request->response();
		} else {
			return "";
		}
	}
	std::string GetLocalIpV4() {
		boost::asio::io_service io_service;
		boost::asio::ip::tcp::resolver resolver(io_service);
		boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
		boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
		boost::asio::ip::tcp::resolver::iterator end; // End marker.
		std::string result = "0.0.0.0";
		while (iter != end)
		{
			boost::asio::ip::tcp::endpoint ep = *iter++;
			if (ep.address().is_v4())
			{
				result = ep.address().to_string();
			}
		}
		return result;
	}
	void DownloadRequest::handleDownloadFailure(const boost::system::error_code& err, const std::string& a_errorMessage) {
		handleDownloadFailure(a_errorMessage + ": " + err.message());
	}
	void DownloadRequest::handleDownloadFailure(const std::string& a_errorMessage) {
		headerData.complete = false;
		headerData.errorMessage = a_errorMessage;
		MV::error(headerData.errorMessage);
		closeSockets();
		callOnComplete();
	}
	void DownloadRequest::callOnComplete() {
		if (!onCompleteCalled && onComplete) { onCompleteCalled = true; onComplete(shared_from_this()); }
	}
	void DownloadRequest::closeSockets() {
		if (socket) {
			boost::system::error_code ec;
			socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			if (ec && ec != boost::asio::error::eof) {
				MV::error("Error shutting down regular download socket: ", ec.message());
			}
			socket->close();
			socket.reset();
		}
		if (sslSocket) {
			boost::system::error_code ec;
			auto closingSocket = sslSocket;
			sslSocket.reset();
			closingSocket->socket.lowest_layer().cancel(ec);
			closingSocket->socket.async_shutdown([closingSocket](const boost::system::error_code& ec) {
				if (ec && ec != boost::asio::error::eof) {
					MV::error("Error shutting down SSL download socket: ", ec.message());
				}
				boost::system::error_code ec2;
				closingSocket->socket.lowest_layer().close(ec2);
				if (ec2) {
					MV::error("Error cancelling SSL download socket: ", ec.message());
				}
			});
		}
	}
	void DownloadRequest::handleReadContent(const boost::system::error_code& err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!err) {
			continueReadingContent();
		} else if (err != boost::asio::error::eof) {
			handleDownloadFailure(err, "Download Read Content Failure");
		}
	}
	void DownloadRequest::continueReadingContent() {
		amountReadSoFar += intermediateResponse->size();
		auto amountLeftToRead = headerData.contentLength - amountReadSoFar;
		if (onProgress) {
			onProgress(headerData.contentLength, amountLeftToRead == 0 ? 1.0f : headerData.contentLength / static_cast<float>(amountReadSoFar));
		}
		if (intermediateResponse->size() > 0) {
			readResponseToStream();
		}
		if (amountLeftToRead > 0) {
			if (sslSocket) {
				boost::asio::async_read(sslSocket->socket, *intermediateResponse, boost::asio::transfer_at_least(std::min(ChunkSize, amountLeftToRead)), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
			} else {
				boost::asio::async_read(*socket, *intermediateResponse, boost::asio::transfer_at_least(std::min(ChunkSize, amountLeftToRead)), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
			}
		} else {
			headerData.readAllData = true;
			closeSockets();
			callOnComplete();
		}
	}
	void DownloadRequest::handleReadHeaders(const boost::system::error_code& err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!err) {
			responseStream = std::make_unique<std::istream>(&(*intermediateResponse));
			headerData.read(*responseStream);
			headerData.complete = true;
			headerData.errorMessage = "";
			if (headerData.status >= 300 && headerData.status < 400 && headerData.bounces.size() < 32 && headerData.values.find("location") != headerData.values.end()) {
				headerData.bounces.push_back(currentUrl.toString());
				initiateRequest(headerData.values["location"]);
			} else {
				continueReadingContent();
			}
		} else {
			handleDownloadFailure(err, "Download Read Header Failure");
		}
	}
	void DownloadRequest::handleWriteRequest(const boost::system::error_code& err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!err) {
			if (sslSocket) {
				boost::asio::async_read_until(sslSocket->socket, *intermediateResponse, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
			} else {
				boost::asio::async_read_until(*socket, *intermediateResponse, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
			}
		} else {
			handleDownloadFailure(err, "Download Write Failure");
		}
	}
	void DownloadRequest::handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!err) {
			// The connection was successful. Send the request.
			if (sslSocket) {
				auto self = shared_from_this();
				sslSocket->socket.async_handshake(boost::asio::ssl::stream_base::client, [this, self](const boost::system::error_code& err) {
					if (!err) {
						boost::asio::async_write(sslSocket->socket, *request, std::bind(&DownloadRequest::handleWriteRequest, self, std::placeholders::_1));
					} else {
						handleDownloadFailure(err, "Download Connection Handshake Failure");
					}
				});
			} else {
				boost::asio::async_write(*socket, *request, std::bind(&DownloadRequest::handleWriteRequest, shared_from_this(), std::placeholders::_1));
			}
		} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
			// The connection failed. Try the next endpoint in the list.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			if (sslSocket) {
				sslSocket->socket.lowest_layer().close();
				sslSocket->socket.lowest_layer().async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			} else {
				socket->close();
				socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
		}
		else {
			handleDownloadFailure(err, "Download Connection Failure");
		}
	}
	void DownloadRequest::handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!err) {
			// Attempt a connection to the first endpoint in the list. Each endpoint
			// will be tried until we successfully establish a connection.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			if (sslSocket) {
				sslSocket->socket.lowest_layer().async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			} else {
				socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
		} else {
			handleDownloadFailure(err, "Download Resolve Failure");
		}
	}
	void DownloadRequest::initiateRequest(const MV::Url& a_url) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		currentUrl = a_url;
		initializeSocket();
		request = std::make_unique<boost::asio::streambuf>();
		intermediateResponse = std::make_unique<boost::asio::streambuf>();
		using boost::asio::ip::tcp;
		std::ostream requestStream(&(*request));
		requestStream << "GET " << a_url.pathAndQuery() << " HTTP/1.1\r\n";
		requestStream << "Host: " << a_url.host() << "\r\n";
		requestStream << "Accept: */*\r\n";
		requestStream << "Connection: close\r\n\r\n";
		tcp::resolver::query query(a_url.host(), a_url.scheme());
		resolver->async_resolve(query, std::bind(&DownloadRequest::handleResolve, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
	}
	void DownloadRequest::initializeSocket() {
		if (!resolver) {
			resolver = std::make_unique<boost::asio::ip::tcp::resolver>(*ioService);
		}
		closeSockets();
		if (currentUrl.scheme() == "https") {
			sslSocket = std::make_shared<SSLSocket>(*ioService);
		} else {
			socket = std::make_unique<boost::asio::ip::tcp::socket>(*ioService);
		}
	}
	void DownloadRequest::start() {
		try {
			initiateRequest(originalUrl);
		} catch (...) {
			handleDownloadFailure("Exception thrown to top level.");
		}
	}
}