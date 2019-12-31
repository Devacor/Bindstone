#include "download.h"
#include <boost/filesystem.hpp>
#include <atomic>
#include "MV/Utility/stringUtility.h"

namespace MV{
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
					} catch (std::exception &e) {
						std::cerr << e.what() << std::endl;
						contentLength = 0;
					}
				}
			}
		}
	}

	std::string DownloadString(const Url& a_url) {
		auto result = std::make_shared<std::stringstream>();
		if (DownloadRequest::make(a_url, result)->header().success) {
			return result->str();
		} else {
			return "";
		}
	}

	MV::HttpHeader DownloadFile(const Url& a_url, const std::string &a_path) {
		HttpHeader header;
		{
			boost::filesystem::create_directories(boost::filesystem::path(a_path).parent_path());
			auto outFile = std::make_shared<std::ofstream>(a_path, std::ofstream::out | std::ofstream::binary);
			auto request = DownloadRequest::make(a_url, outFile);
			header = request->header();
		}
		if (!header.success) {
			std::remove(a_path.c_str());
		}
		return header;
	}

	void DownloadFile(const std::shared_ptr<boost::asio::io_context> &a_ioService, const MV::Url& a_url, const std::string &a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete) {
		boost::filesystem::create_directories(boost::filesystem::path(a_path).parent_path());
		auto outFile = std::make_shared<std::ofstream>(a_path, std::ofstream::out | std::ofstream::binary);
		auto request = DownloadRequest::make(a_ioService, a_url, outFile, [a_path, a_onComplete](std::shared_ptr<DownloadRequest> a_result) {
			if (!a_result->header().success) {
				std::remove(a_path.c_str());
			}
			if (a_onComplete) { a_onComplete(a_result); }
		});
	}

	void DownloadFiles(const std::vector<MV::Url>& a_urls, const std::string &a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete) {
		auto service = std::make_shared<boost::asio::io_context>();
		for (auto&& url : a_urls) {
			DownloadFile(service, url, a_path + boost::filesystem::path(url.path()).filename().string(), a_onComplete);
		}
		service->run();
	}
	void DownloadFiles(const std::shared_ptr<boost::asio::io_context> &a_ioService, const std::vector<MV::Url>& a_urls, const std::string &a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void()> a_onAllComplete) {
		size_t totalFiles = a_urls.size();
		for (auto&& url : a_urls) {
			auto counter = std::make_shared<std::atomic<size_t>>(0);
			DownloadFile(a_ioService, url, a_path + boost::filesystem::path(url.path()).filename().string(), [=](std::shared_ptr<DownloadRequest> a_request) {
				a_onComplete(a_request);
				if (++(*counter) == totalFiles) {
					a_onAllComplete();
				}
			});
		}
	}

	void DownloadRequest::handleReadContent(const boost::system::error_code& err) {
		if (!err) {
			readResponseToStream();
			if (onComplete) { onComplete(shared_from_this()); }
		} else if (err != boost::asio::error::eof) {
			headerData.success = false;
			headerData.errorMessage = "Download Read Content Failure: " + err.message();
			std::cerr << headerData.errorMessage << std::endl;
			if (onComplete) { onComplete(shared_from_this()); }
		}
	}

	void DownloadRequest::handleReadHeaders(const boost::system::error_code& err) {
		if (!err) {
			responseStream = std::make_unique<std::istream>(&(*response));

			headerData.read(*responseStream);
			headerData.success = true;
			headerData.errorMessage = "";
			if (headerData.status >= 300 && headerData.status < 400 && headerData.bounces.size() < 32 && headerData.values.find("location") != headerData.values.end()) {
				headerData.bounces.push_back(currentUrl.toString());
				initiateRequest(headerData.values["location"]);
			} else {
				auto amountLeftToRead = headerData.contentLength - response->size();
				if (response->size() > 0) {
					readResponseToStream();
				}
				if (amountLeftToRead > 0) {
					boost::asio::async_read(*socket, *response, boost::asio::transfer_at_least(amountLeftToRead), std::bind(&DownloadRequest::handleReadContent, shared_from_this(), std::placeholders::_1));
				} else {
					if (onComplete) { onComplete(shared_from_this()); }
				}
			}
		} else {
			headerData.success = false;
			headerData.errorMessage = "Download Read Header Failure: " + err.message();
			std::cerr << headerData.errorMessage << std::endl;
			if (onComplete) { onComplete(shared_from_this()); }
		}
	}

	void DownloadRequest::handleWriteRequest(const boost::system::error_code& err) {
		if (!err) {
			boost::asio::async_read_until(*socket, *response, "\r\n\r\n", std::bind(&DownloadRequest::handleReadHeaders, shared_from_this(), std::placeholders::_1));
		} else {
			headerData.success = false;
			headerData.errorMessage = "Download Write Failure: " + err.message();
			std::cerr << headerData.errorMessage << std::endl;
			if (onComplete) { onComplete(shared_from_this()); }
		}
	}

	void DownloadRequest::handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		if (!err) {
			// The connection was successful. Send the request.
			boost::asio::async_write(*socket, *request, std::bind(&DownloadRequest::handleWriteRequest, shared_from_this(), std::placeholders::_1));
		} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
			// The connection failed. Try the next endpoint in the list.
			socket->close();
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
		} else {
			headerData.success = false;
			headerData.errorMessage = "Download Connection Failure: " + err.message();
			std::cerr << headerData.errorMessage << std::endl;
			if (onComplete) { onComplete(shared_from_this()); }
		}
	}

	void DownloadRequest::handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
		if (!err) {
			// Attempt a connection to the first endpoint in the list. Each endpoint
			// will be tried until we successfully establish a connection.
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			socket->async_connect(endpoint, std::bind(&DownloadRequest::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
		} else {
			headerData.success = false;
			headerData.errorMessage = "Download Resolve Failure: " + err.message();
			std::cerr << headerData.errorMessage << std::endl;
			if (onComplete) { onComplete(shared_from_this()); }
		}
	}

	void DownloadRequest::initiateRequest(const MV::Url& a_url) {
		socket->close();
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
		resolver->async_resolve(query, std::bind(&DownloadRequest::handleResolve, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
	}

	bool DownloadRequest::initializeSocket() {
		bool created = false;
		if (!ioService) {
			ioService = std::make_shared<boost::asio::io_context>();
			created = true;
		}

		resolver = std::make_unique<boost::asio::ip::tcp::resolver>(*ioService);
		socket = std::make_unique<boost::asio::ip::tcp::socket>(*ioService);

		return created;
	}

	void DownloadRequest::perform(const MV::Url& a_url) {
		originalUrl = a_url;
		try {
			bool needToCallRun = initializeSocket();
			initiateRequest(a_url);
			if (needToCallRun) {
				ioService->run();
			}
		} catch (...) {
			headerData.success = false;
			headerData.errorMessage = "Exception thrown to top level.";
			std::cerr << headerData.errorMessage << std::endl;
			onComplete(shared_from_this());
		}
	}

}

