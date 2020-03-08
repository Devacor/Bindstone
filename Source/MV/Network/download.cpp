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
		}
		catch (...) {
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
					}
					catch (std::exception & e) {
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
		if (auto request = DownloadRequest::make(a_url); request->success()) {
			return request->response();
		}
		else {
			return "";
		}
	}
	MV::HttpHeader DownloadFile(const Url& a_url, const std::string& a_path) {
		MV::HttpHeader header;
		if (auto request = DownloadRequest::make(a_url); request->success()) {
			header = request->header();
			std::ofstream file(a_path, std::ofstream::out | std::ofstream::binary);
			file << request->response();
		}
		return header;
	}
	void DownloadFile(const std::shared_ptr<boost::asio::io_context>& a_ioService, const MV::Url& a_url, const std::string& a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete) {
		auto request = DownloadRequest::make(a_ioService, a_url, [a_path, a_onComplete](std::shared_ptr<DownloadRequest> a_result) {
			if (a_result->success()) {
				std::ofstream file(a_path, std::ofstream::out | std::ofstream::binary);
				file << a_result->response();
			}
			if (a_onComplete) { a_onComplete(a_result); }
			});
	}
	void DownloadFiles(const std::vector<MV::Url>& a_urls, const std::string& a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete) {
		auto service = std::make_shared<boost::asio::io_context>();
		for (auto&& url : a_urls) {
			DownloadFile(service, url, a_path + filenameFromPath(url.path()), a_onComplete);
		}
		service->run();
	}
	void DownloadFiles(const std::shared_ptr<boost::asio::io_context>& a_ioService, const std::vector<MV::Url>& a_urls, const std::string& a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void()> a_onAllComplete) {
		size_t totalFiles = a_urls.size();
		for (auto&& url : a_urls) {
			auto counter = std::make_shared<std::atomic<size_t>>(0);
			DownloadFile(a_ioService, url, a_path + filenameFromPath(url.path()), [=](std::shared_ptr<DownloadRequest> a_request) {
				a_onComplete(a_request);
				if (++(*counter) == totalFiles) {
					a_onAllComplete();
				}
				});
		}
	}
	void DownloadRequest::handleHeaderFailure(const boost::system::error_code& err, const std::string& a_errorMessage) {
		headerData.complete = false;
		headerData.errorMessage = a_errorMessage + ": " + err.message();
		MV::error(headerData.errorMessage);
		closeSockets();
		if (onComplete) { onComplete(shared_from_this()); }
	}
	void DownloadRequest::closeSockets() {
		if (socket) {
			socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
			socket->close();
			socket.reset();
		}
		if (sslSocket) {
			sslSocket->shutdown();
			sslSocket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
			sslSocket->lowest_layer().close();
			sslSocket.reset();
			sslContext.reset();
		}
	}
	void DownloadRequest::handleReadContent(const boost::system::error_code& err) {
		if (!err) {
			readResponseToStream();
			if (onComplete) { onComplete(shared_from_this()); }
		}
		else if (err != boost::asio::error::eof) {
			handleHeaderFailure(err, "Download Read Content Failure");
		}
	}
	void DownloadRequest::handleReadHeaders(const boost::system::error_code& err) {
		if (!err) {
			responseStream = std::make_unique<std::istream>(&(*intermediateResponse));
			headerData.read(*responseStream);
			headerData.complete = true;
			headerData.errorMessage = "";
			if (headerData.status >= 300 && headerData.status < 400 && headerData.bounces.size() < 32 && headerData.values.find("location") != headerData.values.end()) {
				headerData.bounces.push_back(currentUrl.toString());
				initiateRequest(headerData.values["location"]);
			}
			else {
				auto amountLeftToRead = headerData.contentLength - intermediateResponse->size();
				if (intermediateResponse->size() > 0) {
					readResponseToStream();
				}
				if (amountLeftToRead > 0) {
					if (sslSocket) {
						boost::asio::async_read(*sslSocket, *intermediateResponse, boost::asio::transfer_at_least(amountLeftToRead), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
					}
					else {
						boost::asio::async_read(*socket, *intermediateResponse, boost::asio::transfer_at_least(amountLeftToRead), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
					}
				}
				else {
					headerData.readAllData = true;
					closeSockets();
					if (onComplete) { onComplete(shared_from_this()); }
				}
			}
		}
		else {
			handleHeaderFailure(err, "Download Read Header Failure");
		}
	}
	void DownloadRequest::handleWriteRequest(const boost::system::error_code& err) {
		if (!err) {
			if (sslSocket) {
				boost::asio::async_read_until(*sslSocket, *intermediateResponse, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
			}
			else {
				boost::asio::async_read_until(*socket, *intermediateResponse, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
			}
		}
		else {
			handleHeaderFailure(err, "Download Write Failure");
		}
	}
	void DownloadRequest::handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		if (!err) {
			// The connection was successful. Send the request.
			if (sslSocket) {
				auto self = shared_from_this();
				sslSocket->async_handshake(boost::asio::ssl::stream_base::client, [this, self](const boost::system::error_code& err) {
					if (!err) {
						boost::asio::async_write(*sslSocket, *request, std::bind(&DownloadRequest::handleWriteRequest, self, std::placeholders::_1));
					}
					else {
						handleHeaderFailure(err, "Download Connection Handshake Failure");
					}
					});
			}
			else {
				boost::asio::async_write(*socket, *request, std::bind(&DownloadRequest::handleWriteRequest, shared_from_this(), std::placeholders::_1));
			}
		}
		else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
			// The connection failed. Try the next endpoint in the list.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			if (sslSocket) {
				sslSocket->lowest_layer().close();
				sslSocket->lowest_layer().async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
			else {
				socket->close();
				socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
		}
		else {
			handleHeaderFailure(err, "Download Connection Failure");
		}
	}
	void DownloadRequest::handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		if (!err) {
			// Attempt a connection to the first endpoint in the list. Each endpoint
			// will be tried until we successfully establish a connection.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			if (sslSocket) {
				sslSocket->lowest_layer().async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
			else {
				socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
		}
		else {
			handleHeaderFailure(err, "Download Resolve Failure");
		}
	}
	void DownloadRequest::initiateRequest(const MV::Url& a_url) {
		currentUrl = a_url;
		bool needToCallRun = initializeSocket();
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
		if (needToCallRun) {
			ioService->run();
			ioService.reset();
		}
	}
	bool DownloadRequest::initializeSocket() {
		bool created = false;
		if (!ioService) {
			ioService = std::make_shared<boost::asio::io_context>();
			created = true;
		}
		if (!resolver) {
			resolver = std::make_unique<boost::asio::ip::tcp::resolver>(*ioService);
		}
		closeSockets();
		if (currentUrl.scheme() == "https") {
			sslContext = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv13);
			sslSocket = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(*ioService, *sslContext);
		}
		else {
			socket = std::make_unique<boost::asio::ip::tcp::socket>(*ioService);
		}
		return created;
	}
	void DownloadRequest::perform(const MV::Url& a_url) {
		originalUrl = a_url;
		try {
			initiateRequest(a_url);
		}
		catch (...) {
			headerData.complete = false;
			headerData.errorMessage = "Exception thrown to top level.";
			MV::error(headerData.errorMessage);
			onComplete(shared_from_this());
		}
	}
}