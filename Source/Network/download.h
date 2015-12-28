#ifndef _MV_DOWNLOAD_H_
#define _MV_DOWNLOAD_H_

#include <string>
#include <iostream>
#include <istream>
#include <ostream>
#include <fstream>
#include "Network/url.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>

namespace MV {
	inline std::istream& getline_platform_agnostic(std::istream& is, std::string& t)
	{
		t.clear();

		// The characters in the stream are read one-by-one using a std::streambuf.
		// That is faster than reading them one-by-one using the std::istream.
		// Code that uses streambuf this way must be guarded by a sentry object.
		// The sentry object performs various tasks,
		// such as thread synchronization and updating the stream state.

		std::istream::sentry se(is, true);
		std::streambuf* sb = is.rdbuf();

		for (;;) {
			int c = sb->sbumpc();
			switch (c) {
			case '\n':
				return is;
			case '\r':
				if (sb->sgetc() == '\n')
					sb->sbumpc();
				return is;
			case EOF:
				// Also handle the case when the last line has no line ending
				if (t.empty())
					is.setstate(std::ios::eofbit);
				return is;
			default:
				t += (char)c;
			}
		}
	}

	struct HttpHeader {
		std::string version;
		int status = 0;
		std::string message;
		std::map<std::string, std::string> values;

		std::vector<std::string> bounces;

		bool success = false;
		std::string errorMessage;

		HttpHeader() {
		}

		HttpHeader(std::istream& response_stream) {
			read(response_stream);
		}

		void read(std::istream& response_stream) {
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
			std::cout << status_code << std::endl;

			getline_platform_agnostic(response_stream, message);

			if (!message.empty() && message[0] == ' ') { message = message.substr(1); }

			std::string header;
			while (getline_platform_agnostic(response_stream, header) && !header.empty()) {
				auto index = header.find_first_of(':');
				if (index != std::string::npos && index > 0) {
					auto key = header.substr(0, index);
					auto value = (index + 2 >= header.size()) ? "" : header.substr(index + 2);
					std::transform(key.begin(), key.end(), key.begin(), std::tolower);
					values[key] = value;
				}
			}
		}
	};


	inline std::ostream& operator<<(std::ostream& os, const HttpHeader& obj)
	{
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
	inline std::istream& operator>>(std::istream& a_is, HttpHeader& a_obj)
	{
		a_obj.read(a_is);
		return a_is;
	}

	class DownloadRequest {
	public:
		DownloadRequest(const MV::Url& a_url, std::ostream &a_streamOutput) :
			streamOutput(a_streamOutput),
			resolver(ioService),
			socket(ioService) {

			try {
				initiateRequest(a_url);
				ioService.run();
			}
			catch (...) {
				headerData.success = false;
				headerData.errorMessage = "Exception thrown to top level.";
				std::cerr << headerData.errorMessage << std::endl;
			}
		}

		HttpHeader& header() {
			return headerData;
		}

	private:
		void initiateRequest(const MV::Url& a_url) {
			socket.close();
			currentUrl = a_url;
			request = std::make_unique<boost::asio::streambuf>();
			response = std::make_unique<boost::asio::streambuf>();
			using boost::asio::ip::tcp;

			std::ostream requestStream(&(*request));
			requestStream << "GET " << a_url.pathAndQuery() << " HTTP/1.1\r\n";
			requestStream << "Host: " << a_url.host() << "\r\n";
			requestStream << "Accept: */*\r\n";
			requestStream << "Connection: close\r\n\r\n";

			tcp::resolver::query query(a_url.host(), "http");
			resolver.async_resolve(query, boost::bind(&DownloadRequest::handleResolve, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
		{
			if (!err) {
				// Attempt a connection to the first endpoint in the list. Each endpoint
				// will be tried until we successfully establish a connection.
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket.async_connect(endpoint, boost::bind(&DownloadRequest::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
			}
			else {
				headerData.success = false;
				headerData.errorMessage = "Download Resolve Failure: " + err.message();
				std::cerr << headerData.errorMessage << std::endl;
			}
		}

		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
		{
			if (!err) {
				// The connection was successful. Send the request.
				boost::asio::async_write(socket, *request, boost::bind(&DownloadRequest::handleWriteRequest, this, boost::asio::placeholders::error));
			}
			else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
				// The connection failed. Try the next endpoint in the list.
				socket.close();
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket.async_connect(endpoint, boost::bind(&DownloadRequest::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
			}
			else {
				headerData.success = false;
				headerData.errorMessage = "Download Connection Failure: " + err.message();
				std::cerr << headerData.errorMessage << std::endl;
			}
		}

		void handleWriteRequest(const boost::system::error_code& err)
		{
			if (!err) {
				boost::asio::async_read_until(socket, *response, "\r\n\r\n", boost::bind(&DownloadRequest::handleReadHeaders, this, boost::asio::placeholders::error));
			}
			else {
				headerData.success = false;
				headerData.errorMessage = "Download Write Failure: " + err.message();
				std::cerr << headerData.errorMessage << std::endl;
			}
		}

		void handleReadHeaders(const boost::system::error_code& err)
		{
			if (!err)
			{
				responseStream = std::make_unique<std::istream>(&(*response));

				headerData.read(*responseStream);
				headerData.success = true;
				headerData.errorMessage = "";
				if (headerData.status >= 300 && headerData.status < 400 && headerData.bounces.size() < 32 && headerData.values.find("location") != headerData.values.end()) {
					headerData.bounces.push_back(currentUrl.toString());
					initiateRequest(headerData.values["location"]);
				}
				else {
					if (response->size() > 0) {
						readResponseToStream();
					}
					boost::asio::async_read(socket, *response, boost::asio::transfer_at_least(1), boost::bind(&DownloadRequest::handleReadContent, this, boost::asio::placeholders::error));
				}
			}
			else {
				headerData.success = false;
				headerData.errorMessage = "Download Read Header Failure: " + err.message();
				std::cerr << headerData.errorMessage << std::endl;
			}
		}


		void handleReadContent(const boost::system::error_code& err)
		{
			if (!err) {
				readResponseToStream();

				boost::asio::async_read(socket, *response, boost::asio::transfer_at_least(1), boost::bind(&DownloadRequest::handleReadContent, this, boost::asio::placeholders::error));
			}
			else if (err != boost::asio::error::eof) {
				headerData.success = false;
				headerData.errorMessage = "Download Read Content Failure: " + err.message();
				std::cerr << headerData.errorMessage << std::endl;
			}
		}

		void readResponseToStream() {
			streamOutput << &(*response);
		}

		boost::asio::io_service ioService;
		boost::asio::ip::tcp::resolver resolver;
		boost::asio::ip::tcp::socket socket;

		std::unique_ptr<std::istream> responseStream;

		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> response;

		std::ostream &streamOutput;

		HttpHeader headerData;

		MV::Url currentUrl;
	};

	inline std::string DownloadString(const MV::Url& a_url) {
		std::stringstream result;
		if (DownloadRequest(a_url, result).header().success) {
			return result.str();
		} else {
			return "";
		}
	}

	inline HttpHeader DownloadFile(const MV::Url& a_url, const std::string &a_path) {
		HttpHeader header;
		{
			std::ofstream outFile(a_path, std::ofstream::out | std::ofstream::binary);
			DownloadRequest request(a_url, outFile);
			header = request.header();
		}
		if (!header.success) {
			std::remove(a_path.c_str());
		}
		return header;
	}
}
#endif