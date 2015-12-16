#ifndef _MV_DOWNLOAD_H_
#define _MV_DOWNLOAD_H_

#include <fstream>
#include <string>
#include "Network/url.h"
#include <boost/asio.hpp>

namespace MV {
	template<typename T>
	void DownloadStream(const MV::Url& url, T& stream) {
		using namespace boost::asio;
		using boost::asio::ip::tcp;

		io_service io_service;

		// Get a list of endpoints corresponding to the server name.
		tcp::resolver resolver(io_service);
		tcp::resolver::query query(url.host(), "http");
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;

		// Try each endpoint until we successfully establish a connection.
		tcp::socket socket(io_service);
		boost::system::error_code error = boost::asio::error::host_not_found;
		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}

		boost::asio::streambuf request;
		std::ostream request_stream(&request);

		request_stream << "GET " << url.pathAndQuery() << " HTTP/1.0\r\n";
		request_stream << "Host: " << url.host() << "\r\n";
		request_stream << "Accept: */*\r\n";
		request_stream << "Connection: close\r\n\r\n";

		// Send the request.
		boost::asio::write(socket, request);

		// Read the response status line.
		boost::asio::streambuf response;
		boost::asio::read_until(socket, response, "\r\n");

		// Check that response is OK.
		std::istream response_stream(&response);
		std::string http_version;
		response_stream >> http_version;
		unsigned int status_code;
		response_stream >> status_code;
		std::string status_message;
		std::getline(response_stream, status_message);


		// Read the response headers, which are terminated by a blank line.
		boost::asio::read_until(socket, response, "\r\n\r\n");

		// Process the response headers.
		std::string header;
		while (std::getline(response_stream, header) && header != "\r") {}

		// Write whatever content we already have to output.
		if (response.size() > 0) {
			stream << &response;
		}
		// Read until EOF, writing data to output as we go.
		while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error)) {
			stream << &response;
		}
	}

	inline void Download(const MV::Url& url, const std::string& a_filePath) {
		std::ofstream outFile(a_filePath, std::ofstream::out | std::ofstream::binary);
		DownloadStream(url, outFile);
	}

	inline std::string DownloadString(const MV::Url &url) {
		std::stringstream outStream;
		DownloadStream(url, outStream);
		return outStream.str();
	}
}
#endif