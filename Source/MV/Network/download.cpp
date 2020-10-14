#include "download.h"
#include <atomic>
#include <sstream>
#include "MV/Utility/stringUtility.h"
#include <thread>

namespace MV {

	void HttpResponse::readHeader(std::istream& response_stream) {
		headerValues.clear();

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
				headerValues[key] = value;
				if (key == "content-length") {
					try {
						expectedBodyLength = static_cast<size_t>(stoul(value));
					} catch (std::exception& e) {
						MV::warning("Failed to parse content-length as an unsigned long: [", value, "] ", e.what());
						expectedBodyLength = 0;
					}
				}
			}
		}
	}

	void HttpResponse::writeResponse(std::ostream& response_stream) const {
		response_stream << version << " " << status << " " << message << "\r\n";
		if (!body.empty()) {
			response_stream << "content-length: " << body.size() << "\r\n";
		}
		for (auto&& kvp : headerValues) {
			if (body.empty() || MV::toLower(kvp.first) != "content-length") {
				response_stream << kvp.first << ": " << kvp.second << "\r\n";
			}
		}
		response_stream << "\r\n";
		response_stream << body;
		response_stream << 0;
	}

	std::string HttpResponse::to_string() const {
		std::ostringstream response_stream(std::ofstream::out | std::ofstream::binary);
		writeResponse(response_stream);
		return response_stream.str();
	}

	std::ostream& operator<<(std::ostream& a_os, const HttpResponse& a_obj) {
		a_obj.writeResponse(a_os);
		return a_os;
	}
	std::istream& operator>>(std::istream& a_is, HttpResponse& a_obj) {
		a_obj.readHeader(a_is);
		return a_is;
	}

	void HttpRequest::writeRequest(std::ostream& a_os, const MV::Url& a_url) const {
		a_os << (type == HttpRequest::Type::POST ? "POST" : "GET") << " " << a_url.pathAndQuery() << " " << version << "\r\n";
		a_os << "Host: " << a_url.host() << "\r\n";
		if (type == HttpRequest::Type::POST && !postBody.empty()) {
			a_os << "Content-Type: application/x-www-form-urlencoded\r\n";
			a_os << "Content-Length: " << postBody.size() << "\r\n";
		}
		a_os << "Accept: */*\r\n";
		a_os << "Connection: close\r\n\r\n";
		if (type == HttpRequest::Type::POST && !postBody.empty()) {
			a_os << postBody << 0;
		}
	}

	bool iSubstringEquals(const std::string& a_fullString, const std::string& a_substring, size_t a_begin = 0) {
		size_t end = a_begin + a_substring.size();
		if (a_fullString.size() < end) { return false; }
		return std::equal(a_fullString.begin() + a_begin, a_fullString.begin() + end, a_substring.begin(), [](char l, char r) {return std::tolower(l) == std::tolower(r); });
	}

	void HttpRequest::readHeaderFromStream(std::istream& a_is) {
		std::string line;
		if (!getline_platform_agnostic(a_is, line)) { return; }
		std::stringstream lineStream;
		lineStream << line;
		{
			std::string method;
			lineStream >> method;
			type = (MV::iSubstringEquals(method, "post") ? Type::POST : Type::GET);
		}
		std::string pathAndQuery;
		lineStream >> pathAndQuery >> version;
		
		while (getline_platform_agnostic(a_is, line) && !line.empty()) {
			auto index = line.find_first_of(':');
			if (index != std::string::npos && index > 0) {
				auto key = line.substr(0, index);
				auto value = (index + 2 >= line.size()) ? "" : line.substr(index + 2);
				std::transform(key.begin(), key.end(), key.begin(), [](char c) {return std::tolower(c); });
				headerValues[key] = value;
				if (key == "host") {
					url = value + pathAndQuery;
				} else if (key == "content-length") {
					try {
						expectedBodyLength = static_cast<size_t>(stoul(value));
					} catch (std::exception& e) {
						MV::warning("Failed to parse content-length as an unsigned long: [", value, "] ", e.what());
						expectedBodyLength = 0;
					}
				}
			}
		}

	}

	std::map<std::string, std::string> HttpRequest::parsePostParameters() const {
		return MV::Url::parseQueryString(postBody);
	}
	std::map<std::string, std::string> HttpRequest::parseGetParameters() const {
		return MV::Url::parseQueryString(url.rawQuery());
	}

	std::string DownloadString(const Url& a_url) {
		auto request = DownloadRequest::make(a_url);
		if (request->success()) {
			return request->response().body;
		} else {
			return "";
		}
	}
		
	std::string GetLocalIpV4() {
		std::string result = "0.0.0.0";
		try {
			boost::asio::io_service io_service;
			boost::asio::ip::tcp::resolver resolver(io_service);
			boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
			boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
			boost::asio::ip::tcp::resolver::iterator end; // End marker.
			
			while (iter != end) {
				boost::asio::ip::tcp::endpoint ep = *iter++;
				if(ep.address().is_v4()) {
					result = ep.address().to_string();
				}
			}
		} catch(...) {
		}
		return result;
	}
	
	void DownloadRequest::handleDownloadFailure(const boost::system::error_code& err, const std::string &a_errorMessage){
		handleDownloadFailure(a_errorMessage + ": " + err.message());
	}
		
	void DownloadRequest::handleDownloadFailure(const std::string &a_errorMessage){
		headerData.headerComplete = false;
		headerData.errorMessage = a_errorMessage;
		MV::error(headerData.errorMessage);
		closeSockets();
		callOnComplete();
	}
	
	void DownloadRequest::callOnComplete(){
		if (!onCompleteCalled && onComplete) { onCompleteCalled = true; onComplete(shared_from_this()); }
	}
	
	void DownloadRequest::closeSockets(){
		if(socket){
			boost::system::error_code ec;
			socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			socket->close();
			socket.reset();
		}
		if(sslSocket){
			boost::system::error_code ec;
			auto closingSocket = sslSocket;
			sslSocket.reset();
			closingSocket->socket.lowest_layer().cancel(ec);
			closingSocket->socket.async_shutdown([closingSocket](const boost::system::error_code &ec){
				if (ec && ec != boost::asio::error::eof){
					MV::error("Error shutting down SSL socket: ", ec.message());
				}
				boost::system::error_code ec2;
				closingSocket->socket.lowest_layer().close(ec2);
				if(ec2){
					MV::error("Error cancelling SSL socket: ", ec.message());
				}
			});
		}
	}

	void DownloadRequest::handleReadContent(const boost::system::error_code& err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if(handleCancellation()){
			return;
		}
		
		if (!err || err == boost::asio::error::eof) {
			continueReadingContent();
		} else {
			handleDownloadFailure(err, "Download Read Content Failure");
		}
	}

	void DownloadRequest::initiateReadingMoreContent(size_t a_minimumTransferAmount) {
		if (sslSocket) {
			boost::asio::async_read(sslSocket->socket, *intermediateResponse, boost::asio::transfer_at_least(a_minimumTransferAmount), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
		} else {
			boost::asio::async_read(*socket, *intermediateResponse, boost::asio::transfer_at_least(a_minimumTransferAmount), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
		}
	}
		
	void DownloadRequest::continueReadingContent(){
		amountReadSoFar += intermediateResponse->size();
		size_t amountLeftToRead = 1;

		if(headerData.expectedBodyLength){
			amountLeftToRead = headerData.expectedBodyLength - std::min(amountReadSoFar, headerData.expectedBodyLength);
			if (onProgress) {
				onProgress(headerData.expectedBodyLength, headerData.expectedBodyLength / static_cast<float>(amountReadSoFar));
			}
		}

		bool ableToReadData = readResponseToStream();

		if (ableToReadData && amountLeftToRead > 0) {
			initiateReadingMoreContent(headerData.expectedBodyLength ? std::min(ChunkSize, amountLeftToRead) : 1);
		} else {
			headerData.body = streamOutput->str();
			headerData.readAllData = true;
			streamOutput->clear();
			closeSockets();
			callOnComplete();
		}
	}

	void DownloadRequest::handleReadHeaders(const boost::system::error_code& err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if(handleCancellation()){
			return;
		}
		if (!err) {
			responseStream = std::make_unique<std::istream>(&(*intermediateResponse));

			headerData.readHeader(*responseStream);
			headerData.headerComplete = true;
			headerData.errorMessage = "";
			if (headerData.status >= 300 && headerData.status < 400 && headerData.bounces.size() < 32 && headerData.headerValues.find("location") != headerData.headerValues.end()) {
				headerData.bounces.push_back(currentUrl.toString());
				initiateRequest(headerData.headerValues["location"]);
			} else {
				continueReadingContent();
			}
		} else {
			handleDownloadFailure(err, "Download Read Header Failure");
		}
	}

	void DownloadRequest::handleWriteRequest(const boost::system::error_code& err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if(handleCancellation()){
			return;
		}
		if (!err) {
			if(sslSocket){
				boost::asio::async_read_until(sslSocket->socket, *intermediateResponse, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
			}else{
				boost::asio::async_read_until(*socket, *intermediateResponse, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
			}
		} else {
			handleDownloadFailure(err, "Download Write Failure");
		}
	}

	void DownloadRequest::handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if(handleCancellation()){
			return;
		}
		if (!err) {
			// The connection was successful. Send the request.
			if(sslSocket){
				auto self = shared_from_this();
				sslSocket->socket.async_handshake(boost::asio::ssl::stream_base::client, [this, self](const boost::system::error_code &err){
					if (!err) {
						boost::asio::async_write(sslSocket->socket, *request, std::bind(&DownloadRequest::handleWriteRequest, self, std::placeholders::_1));
					}else{
						handleDownloadFailure(err, "Download Connection Handshake Failure");
					}
				});
			}else{
				boost::asio::async_write(*socket, *request, std::bind(&DownloadRequest::handleWriteRequest, shared_from_this(), std::placeholders::_1));
			}
		} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
			// The connection failed. Try the next endpoint in the list.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			if(sslSocket){
				sslSocket->socket.lowest_layer().close();
				sslSocket->socket.lowest_layer().async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}else{
				socket->close();
				socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
		} else {
			handleDownloadFailure(err, "Download Connection Failure");
		}
	}

	void DownloadRequest::handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if(handleCancellation()){
			return;
		}
		if (!err) {
			// Attempt a connection to the first endpoint in the list. Each endpoint
			// will be tried until we successfully establish a connection.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			if(sslSocket){
				sslSocket->socket.lowest_layer().async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}else{
				socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			}
		} else {
			handleDownloadFailure(err, "Download Resolve Failure");
		}
	}

	void DownloadRequest::initiateRequest(const MV::Url& a_url) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if(handleCancellation()){
			return;
		}
		currentUrl = a_url;
		initializeSocket();
		request = std::make_unique<boost::asio::streambuf>();
		intermediateResponse = std::make_unique<boost::asio::streambuf>();
		using boost::asio::ip::tcp;

		std::ostream requestStream(&(*request));
		target.writeRequest(requestStream, a_url);
		
		tcp::resolver::query query(a_url.host(), a_url.scheme());
		resolver->async_resolve(query, std::bind(&DownloadRequest::handleResolve, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
	}

	void DownloadRequest::initializeSocket() {
		if(!resolver){
			resolver = std::make_unique<boost::asio::ip::tcp::resolver>(*ioService);
		}
		
		closeSockets();
		if(currentUrl.scheme() == "https"){
			sslSocket = std::make_shared<SSLSocket>(*ioService);
		}else{
			socket = std::make_unique<boost::asio::ip::tcp::socket>(*ioService);
		}
	}

	void DownloadRequest::start() {
		try {
			initiateRequest(target.url);
		} catch (...) {
			handleDownloadFailure("Exception thrown to top level.");
		}
	}
}

