#ifndef _MV_DOWNLOAD_H_
#define _MV_DOWNLOAD_H_
#include <string>
#include <iostream>
#include <istream>
#include <ostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <string>
#include "MV/Network/url.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "MV/Utility/threadPool.hpp"

namespace MV {
	struct HttpHeader {
		std::string version;
		int status = 0;
		std::string message;
		std::map<std::string, std::string> values;
		std::vector<std::string> bounces;
		bool complete = false;
		bool readAllData = false;
		std::string errorMessage;
		size_t contentLength = 0;
		bool success() const {
			//implicitly complete and status will always be that way if readAllData is true, but for clarity, these are the conditions for success.
			return complete && status == 200 && readAllData;
		}
		HttpHeader() {
		}
		HttpHeader(std::istream& response_stream) {
			read(response_stream);
		}
		void read(std::istream& response_stream);
	};
	std::ostream& operator<<(std::ostream& os, const HttpHeader& obj);
	std::istream& operator>>(std::istream& a_is, HttpHeader& a_obj);
	class DownloadRequest : public std::enable_shared_from_this<DownloadRequest> {
	public:
		static std::shared_ptr<DownloadRequest> make(const MV::Url& a_url) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest());
			result->perform(a_url);
			return result;
		}
		//onComplete is called on success or error at the end of the download.
		static std::shared_ptr<DownloadRequest> make(const std::shared_ptr<boost::asio::io_context>& a_ioService, const MV::Url& a_url, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest());
			result->onComplete = a_onComplete;
			result->ioService = a_ioService;
			result->perform(a_url);
			return result;
		}
		bool success() const {
			return headerData.success();
		}
		HttpHeader& header() {
			return headerData;
		}
		MV::Url finalUrl() {
			return currentUrl;
		}
		MV::Url inputUrl() {
			return originalUrl;
		}
		std::string response() {
			return streamOutput->str();
		}
	private:
		DownloadRequest() :
			streamOutput(std::make_shared<std::ostringstream >(std::ofstream::out | std::ofstream::binary)) {
		}
		void handleHeaderFailure(const boost::system::error_code& err, const std::string& a_errorMessage);
		void perform(const MV::Url& a_url);
		bool initializeSocket();
		void closeSockets();
		void initiateRequest(const MV::Url& a_url);
		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleWriteRequest(const boost::system::error_code& err);
		void handleReadHeaders(const boost::system::error_code& err);
		void handleReadContent(const boost::system::error_code& err);
		void readResponseToStream() {
			(*streamOutput) << &(*intermediateResponse);
		}
		std::shared_ptr<boost::asio::io_context> ioService;
		std::unique_ptr<boost::asio::ip::tcp::resolver> resolver;
		std::unique_ptr<boost::asio::ip::tcp::socket> socket;
		std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> sslSocket;
		std::unique_ptr<boost::asio::ssl::context> sslContext;
		boost::asio::ip::tcp* activeSocket = nullptr;
		std::unique_ptr<std::istream> responseStream;
		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> intermediateResponse;
		std::shared_ptr<std::ostringstream> streamOutput;
		HttpHeader headerData;
		MV::Url currentUrl;
		MV::Url originalUrl;
		std::function<void(std::shared_ptr<DownloadRequest>)> onComplete;
	};
	std::string DownloadString(const MV::Url& a_url);
	HttpHeader DownloadFile(const MV::Url& a_url, const std::string& a_path);
	void DownloadFile(const std::shared_ptr<boost::asio::io_context>& a_ioService, const MV::Url& a_url, const std::string& a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete = std::function<void(std::shared_ptr<DownloadRequest>)>());
	void DownloadFiles(const std::vector<MV::Url>& a_url, const std::string& a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete = std::function<void(std::shared_ptr<DownloadRequest>)>());
	void DownloadFiles(const std::shared_ptr<boost::asio::io_context>& a_ioService, const std::vector<MV::Url>& a_url, const std::string& a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete = std::function<void(std::shared_ptr<DownloadRequest>)>(), std::function<void()> a_onAllComplete = std::function<void()>());
	class DownloadPool {
	public:
		DownloadPool() :
			pool(),
			ioService(std::make_shared<boost::asio::io_context>()) {
			initialize();
		}
		DownloadPool(size_t a_threads) :
			pool(a_threads),
			ioService(std::make_shared<boost::asio::io_context>()) {
			initialize();
		}
		std::shared_ptr<boost::asio::io_context> service() {
			return ioService;
		}
	private:
		void initialize() {
			for (size_t i = 0; i < pool.threads(); ++i) {
				pool.task([ioService = ioService]() {
					ioService->run();
					});
			}
		}
		ThreadPool pool;
		std::shared_ptr<boost::asio::io_context> ioService;
	};
}
#endif