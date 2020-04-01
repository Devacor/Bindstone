#ifndef _MV_DOWNLOAD_H_
#define _MV_DOWNLOAD_H_
#include <string>
#include <iostream>
#include <istream>
#include <ostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include "MV/Network/url.h"
#include "boost/asio.hpp"
#include "boost/asio/ssl.hpp"
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
		//Single threaded blocking method. Fire this from a non-ui thread if you use it at all.
		static std::shared_ptr<DownloadRequest> make(const MV::Url& a_url) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_url));
			result->ioService = std::make_shared<boost::asio::io_context>();
			result->start();
			result->ioService->run();
			return result;
		}
		//onComplete is called on success or error at the end of the download.
		//onProgress(totalBytes, percentBytesDownloaded) is called after each chunk arrives.
		static std::shared_ptr<DownloadRequest> make(const std::shared_ptr<boost::asio::io_context>& a_ioService, const MV::Url& a_url, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void(size_t, float)> a_onProgress = {}, bool a_delayStart = false) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_url));
			result->onComplete = a_onComplete;
			result->onProgress = a_onProgress;
			result->ioService = a_ioService;
			if (!a_delayStart) {
				result->start();
			}
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
		void cancel() {
			std::scoped_lock<std::recursive_mutex> lock(criticalPath);
			if (!onCompleteCalled) {
				cancelled = true;
			}
		}
		//automatically called unless initiated with delayStart
		void start();
	private:
		DownloadRequest(const MV::Url& a_url) :
			originalUrl(a_url),
			streamOutput(std::make_shared<std::ostringstream >(std::ofstream::out | std::ofstream::binary)) {
		}
		DownloadRequest(const DownloadRequest&) = delete;
		DownloadRequest& operator=(const DownloadRequest&) = delete;
		bool handleCancellation() {
			if (cancelled) {
				handleDownloadFailure("Download cancelled.");
				return true;
			}
			return false;
		}
		void handleDownloadFailure(const boost::system::error_code& err, const std::string& a_errorMessage);
		void handleDownloadFailure(const std::string& a_errorMessage);
		void initializeSocket();
		void closeSockets();
		void callOnComplete();
		void initiateRequest(const MV::Url& a_url);
		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleWriteRequest(const boost::system::error_code& err);
		void handleReadHeaders(const boost::system::error_code& err);
		void handleReadContent(const boost::system::error_code& err);
		void continueReadingContent();
		void readResponseToStream() {
			(*streamOutput) << &(*intermediateResponse);
		}
		MV::Url currentUrl;
		MV::Url originalUrl;
		std::shared_ptr<boost::asio::io_context> ioService;
		std::unique_ptr<boost::asio::ip::tcp::resolver> resolver;
		std::unique_ptr<boost::asio::ip::tcp::socket> socket;
		struct SSLSocket {
			SSLSocket(boost::asio::io_context& ioService) :
				context(boost::asio::ssl::context::sslv23),
				socket(ioService, context) {
			}
			boost::asio::ssl::context context;
			boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket;
		};
		std::shared_ptr<SSLSocket> sslSocket;
		boost::asio::ip::tcp* activeSocket = nullptr;
		std::unique_ptr<std::istream> responseStream;
		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> intermediateResponse;
		std::shared_ptr<std::ostringstream> streamOutput;
		HttpHeader headerData;
		std::function<void(std::shared_ptr<DownloadRequest>)> onComplete;
		std::function<void(size_t, float)> onProgress;
		static constexpr size_t ChunkSize = 256;
		size_t amountReadSoFar = 0;
		//Mutex is only necessary for the cancellation path. Otherwise it runs in an implicit callback strand.
		std::recursive_mutex criticalPath;
		bool cancelled = false;
		bool onCompleteCalled = false;
	};
	//Very plain blocking method for downloading a string, returns "" on error.
	std::string DownloadString(const MV::Url& a_url);
	std::string GetLocalIpV4();
	class DownloadPool : public std::enable_shared_from_this<DownloadPool> {
	public:
		~DownloadPool() {
			cancelAll();
			ioService->stop();
		}
		static std::shared_ptr<DownloadPool> make() {
			return std::shared_ptr<DownloadPool>(new DownloadPool());
		}
		static std::shared_ptr<DownloadPool> make(size_t a_threads) {
			return std::shared_ptr<DownloadPool>(new DownloadPool(a_threads));
		}
		std::shared_ptr<boost::asio::io_context> service() {
			return ioService;
		}
		std::shared_ptr<DownloadRequest> make(const MV::Url& a_url, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void(size_t, float)> a_onProgress = {}) {
			auto self = shared_from_this();
			auto newRequest = DownloadRequest::make(ioService, a_url, [this, self, a_onComplete](std::shared_ptr<DownloadRequest> a_requestSelf) {
				{
					std::scoped_lock<std::mutex> lock(activeRequestsMutex);
					activeRequests.erase(a_requestSelf);
				}
				a_onComplete(a_requestSelf);
			}, a_onProgress, true);

			{
				std::scoped_lock<std::mutex> lock(activeRequestsMutex);
				activeRequests.insert(newRequest);
			}

			//Order matters here, we don't want to start a download request only to have it finish before we've even registered it with this class.
			newRequest->start();
			return newRequest;
		}
		void cancelAll() {
			std::scoped_lock<std::mutex> lock(activeRequestsMutex);
			for (auto&& item : activeRequests) {
				item->cancel();
			}
			activeRequests.clear();
		}
	private:
		DownloadPool() :
			pool(),
			ioService(std::make_shared<boost::asio::io_context>()),
			keepIoServiceRunning(*ioService) {
			initialize();
		}
		DownloadPool(size_t a_threads) :
			pool(a_threads),
			ioService(std::make_shared<boost::asio::io_context>()),
			keepIoServiceRunning(*ioService) {
			initialize();
		}
		DownloadPool(const DownloadPool&) = delete;
		DownloadPool& operator=(const DownloadPool&) = delete;
		void initialize() {
			for (size_t i = 0; i < pool.threads(); ++i) {
				pool.task([ioService = ioService]() {
					ioService->run();
					});
			}
		}
		ThreadPool pool;
		std::shared_ptr<boost::asio::io_context> ioService;
		boost::asio::io_service::work keepIoServiceRunning;
		std::mutex activeRequestsMutex;
		std::set<std::shared_ptr<DownloadRequest>> activeRequests;
	};
}
#endif