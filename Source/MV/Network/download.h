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


#include <boost/asio/ssl/context.hpp>

#ifdef _WIN32
#include <wincrypt.h>
#include <tchar.h>

inline void add_root_certs(boost::asio::ssl::context& ctx) {
	HCERTSTORE hStore = CertOpenSystemStore(0, _T("ROOT"));
	if (!hStore) {
		return;
	}

	X509_STORE* store = X509_STORE_new();
	PCCERT_CONTEXT pContext = NULL;
	while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
		X509* x509 = d2i_X509(NULL, (const unsigned char**)&pContext->pbCertEncoded, pContext->cbCertEncoded);
		if (x509) {
			X509_STORE_add_cert(store, x509);
			X509_free(x509);
		}
	}

	CertFreeCertificateContext(pContext);
	CertCloseStore(hStore, 0);

	SSL_CTX_set_cert_store(ctx.native_handle(), store);
}
#else
inline void add_root_certs(boost::asio::ssl::context& ctx) {}
#endif


namespace MV {
	struct HttpResponse {
		//canonical response data for returning a result.
		std::string version;
		int status = 0;
		std::string message;

		std::map<std::string, std::string> headerValues;
		std::string body; //populated by DownloadRequest on success.

		static HttpResponse make200(const std::string &a_content = "", const std::string& a_contentType = "text/html;charset=utf-8") {
			HttpResponse result;
			result.setDefaults();
			result.status = 200;
			result.message = "OK";
			if (!a_content.empty()) {
				result.setContent(a_content, a_contentType);
			}
			return result;
		}

		static HttpResponse make404() {
			HttpResponse result;
			result.setDefaults();
			result.status = 404;
			result.message = "Not Found";
			return result;
		}

		static HttpResponse make500() {
			HttpResponse result;
			result.setDefaults();
			result.status = 500;
			result.message = "Internal Server Error";
			return result;
		}

		void setDefaults() {
			version = "HTTP/1.1";
			headerValues["connection"] = "close";
		}

		void setContent(const std::string& a_body, const std::string& a_contentType = "text/html;charset=utf-8") {
			headerValues["content-type"] = a_contentType;
			body = a_body;
			expectedBodyLength = body.size();
		}

		//populated by the process of running a DownloadRequest.
		size_t expectedBodyLength = 0;
		bool headerComplete = false;
		bool readAllData = false;
		std::string errorMessage;

		std::vector<std::string> bounces;
		
		bool success() const {
			//implicitly complete and status will always be that way if readAllData is true, but for clarity, these are the conditions for success.
			return headerComplete && status == 200 && readAllData;
		}

		HttpResponse() {
		}

		HttpResponse(std::istream& response_stream) {
			readHeader(response_stream);
		}

		void readHeader(std::istream& response_stream);

		void writeResponse(std::ostream& response_stream) const;
		std::string to_string() const;
	};

	struct HttpRequest {
		enum class Type { GET, POST };
		HttpRequest() :
			type(Type::GET) {
		}

		HttpRequest(const MV::Url& a_url) :
			url(a_url),
			type(Type::GET) {
		}

		HttpRequest(const MV::Url& a_url, const std::map<std::string, std::string>& a_postParams) :
			HttpRequest(a_url, MV::Url::makeQueryString(a_postParams, ""), "application/x-www-form-urlencoded") {
		}
		//a_postContentType could be text/plain or any other valid Content-Type.
		HttpRequest(const MV::Url& a_url, const std::string& a_postBody, const std::string& a_postContentType = "application/json") :
			url(a_url),
			type(Type::POST),
			postBody(a_postBody),
			postContentType(a_postContentType) {
		}

		HttpRequest(const HttpRequest&) = default;
		HttpRequest(HttpRequest&&) = default;
		HttpRequest& operator=(const HttpRequest&) = default;

		inline void writeRequest(std::ostream& a_os) const {
			writeRequest(a_os, url);
		}
		void writeRequest(std::ostream& a_os, const MV::Url& a_url) const;

		void readHeaderFromStream(std::istream& a_is);

		std::map<std::string, std::string> parsePostParameters() const; //from post body
		std::map<std::string, std::string> parseGetParameters() const; //from url

		MV::Url url;
		Type type;
		std::string version = "HTTP/1.1";
		std::string postBody;
		std::string postContentType;

		size_t expectedBodyLength = 0;
		std::map<std::string, std::string> headerValues;
	};

	std::ostream& operator<<(std::ostream& os, const HttpResponse& obj);
	std::istream& operator>>(std::istream& a_is, HttpResponse& a_obj);

	class DownloadRequest : public std::enable_shared_from_this<DownloadRequest> {
	public:
		//Single threaded blocking method. Fire this from a non-ui thread if you use it at all.
		static std::shared_ptr<DownloadRequest> make(const HttpRequest& a_target) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_target));
			result->ioService = std::make_shared<boost::asio::io_context>();
			result->start();
			result->ioService->run();
			return result;
		}

		static std::shared_ptr<DownloadRequest> make(const MV::Url& a_url) {
			return make(HttpRequest{ a_url });
		}

		//onComplete is called on success or error at the end of the download.
		//onProgress(totalBytes, percentBytesDownloaded) is called after each chunk arrives.
		static std::shared_ptr<DownloadRequest> make(const std::shared_ptr<boost::asio::io_context>& a_ioService, const HttpRequest& a_target, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void(size_t, float)> a_onProgress = {}, bool a_delayStart = false) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_target));
			result->onComplete = a_onComplete;
			result->onProgress = a_onProgress;
			result->ioService = a_ioService;
			if (!a_delayStart) {
				result->start();
			}
			return result;
		}

		static std::shared_ptr<DownloadRequest> make(const std::shared_ptr<boost::asio::io_context>& a_ioService, const MV::Url& a_url, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void(size_t, float)> a_onProgress = {}, bool a_delayStart = false) {
			return make(a_ioService, HttpRequest{ a_url }, a_onComplete, a_onProgress, a_delayStart);
		}
		
		bool success() const {
			return headerData.success();
		}

		HttpResponse& response() {
			return headerData;
		}

		MV::Url finalUrl() {
			return currentUrl;
		}

		MV::Url inputUrl() {
			return target.url;
		}
		
		void cancel() {
			std::scoped_lock<std::recursive_mutex> lock(criticalPath);
			if(!onCompleteCalled){
				cancelled = true;
			}
		}
		
		//automatically called unless initiated with delayStart
		void start();
	private:
		DownloadRequest(const HttpRequest &a_target) :
			target(a_target),
			streamOutput(std::make_shared<std::ostringstream >(std::ofstream::out | std::ofstream::binary)) {
		}
		DownloadRequest(const DownloadRequest&) = delete;
		DownloadRequest& operator=(const DownloadRequest&) = delete;
		
		bool handleCancellation(){
			if(cancelled){
				handleDownloadFailure("Download cancelled.");
				return true;
			}
			return false;
		}
		
		void handleDownloadFailure(const boost::system::error_code& err, const std::string &a_errorMessage);
		void handleDownloadFailure(const std::string &a_errorMessage);
		
		void initializeSocket();
		void closeSockets();
		void callOnComplete();

		void initiateRequest(const MV::Url& a_url);

		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleWriteRequest(const boost::system::error_code& err);
		void handleReadHeaders(const boost::system::error_code& err);
		void handleReadContent(const boost::system::error_code& err);
		void initiateReadingMoreContent(size_t a_minimumTransferAmount);
		void continueReadingContent();

		bool readResponseToStream() {
			if (intermediateResponse->size() > 0) {
				if (intermediateResponse->size() > 1) {
					(*streamOutput) << &(*intermediateResponse);
					return true;
				} else {
					char input = getResponseChar();
					if (input != 0) {
						(*streamOutput) << input;
						return true;
					}
				}
			}
			return false;
		}

		char getResponseChar() {
			if (intermediateResponse->size() == 1) {
				std::ostringstream result;
				result << &(*intermediateResponse);
				return result.str()[0];
			}
			return '!';
		}
		
		MV::Url currentUrl;
		HttpRequest target;

		std::shared_ptr<boost::asio::io_context> ioService;
		std::unique_ptr<boost::asio::ip::tcp::resolver> resolver;
		std::unique_ptr<boost::asio::ip::tcp::socket> socket;
		struct SSLSocket {
			SSLSocket(boost::asio::io_context &ioService):
				socket(ioService, context.context){
			}

			struct SSLContextInitializer {
				SSLContextInitializer() :
					context(boost::asio::ssl::context::sslv23) {

					context.set_default_verify_paths();
					context.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::sslv23);
					context.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
					add_root_certs(context);
				}
				boost::asio::ssl::context context;
			};

			SSLContextInitializer context;
			boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket;
		};
		std::shared_ptr<SSLSocket> sslSocket;
		
		boost::asio::ip::tcp* activeSocket = nullptr;

		std::unique_ptr<std::istream> responseStream;

		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> intermediateResponse;

		std::shared_ptr<std::ostringstream> streamOutput;

		HttpResponse headerData;

		std::function<void(std::shared_ptr<DownloadRequest>)> onComplete;
		std::function<void (size_t, float)> onProgress;
		
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
		~DownloadPool(){
			cancelAll();
			ioService->stop();
		}
		static std::shared_ptr<DownloadPool> make(){
			return std::shared_ptr<DownloadPool>(new DownloadPool());
		}
		static std::shared_ptr<DownloadPool> make(size_t a_threads){
			return std::shared_ptr<DownloadPool>(new DownloadPool(a_threads));
		}
		
		std::shared_ptr<boost::asio::io_context> service() {
			return ioService;
		}
		
		std::shared_ptr<DownloadRequest> make(const HttpRequest& a_target, std::function<void (std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void (size_t, float)> a_onProgress = {}) {
			auto self = shared_from_this();
			auto newRequest = DownloadRequest::make(ioService, a_target, [this, self, a_onComplete](std::shared_ptr<DownloadRequest> a_requestSelf){
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

		std::shared_ptr<DownloadRequest> make(const MV::Url& a_url, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete, std::function<void(size_t, float)> a_onProgress = {}) {
			return make(HttpRequest{ a_url }, a_onComplete, a_onProgress);
		}
		
		void cancelAll(){
			std::scoped_lock<std::mutex> lock(activeRequestsMutex);
			for(auto&& item : activeRequests){
				item->cancel();
			}
			activeRequests.clear();
		}
	private:
		DownloadPool() :
			pool(),
			ioService(std::make_shared<boost::asio::io_context>()),
			keepIoServiceRunning(*ioService){
			initialize();
		}
		DownloadPool(size_t a_threads) :
			pool(a_threads),
			ioService(std::make_shared<boost::asio::io_context>()),
			keepIoServiceRunning(*ioService){
			initialize();
		}
		DownloadPool(const DownloadPool&) = delete;
		DownloadPool& operator=(const DownloadPool&) = delete;
		
		void initialize(){
			for(size_t i = 0;i < pool.threads();++i){
				pool.task([ioService=ioService](){
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
