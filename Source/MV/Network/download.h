#ifndef _MV_DOWNLOAD_H_
#define _MV_DOWNLOAD_H_

#include <chrono>
#include <cstddef>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#include "MV/Network/url.h"
#include "MV/Network/networkContext.h"
#include "MV/Utility/generalUtility.h"

namespace MV {
	struct HttpResponse {
		std::string version = "HTTP/1.1";
		int status = 0;
		std::string message;

		// Keys are lower case.
		std::map<std::string, std::string> headerValues;
		std::string body;

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
		}

		bool ok() const {
			return status >= 200 && status < 300;
		}

		// The status line and headers, replacing any read before; framing the body is the reader's job.
		void readHeader(std::istream& a_stream);

		void writeResponse(std::ostream& a_stream) const;
		std::string to_string() const;
	};

	struct HttpRequest {
		enum class Type { GET, POST };

		HttpRequest() {
		}

		HttpRequest(const MV::Url& a_url) :
			url(a_url) {
		}

		HttpRequest(const MV::Url& a_url, const std::map<std::string, std::string>& a_postParams) :
			HttpRequest(a_url, MV::Url::makeQueryString(a_postParams, ""), "application/x-www-form-urlencoded") {
		}

		// a_postContentType is any Content-Type: text/plain, application/octet-stream, ...
		HttpRequest(const MV::Url& a_url, const std::string& a_postBody, const std::string& a_postContentType = "application/json") :
			url(a_url),
			type(Type::POST),
			postBody(a_postBody),
			postContentType(a_postContentType) {
		}

		void writeRequest(std::ostream& a_os) const {
			writeRequest(a_os, url);
		}
		void writeRequest(std::ostream& a_os, const MV::Url& a_url) const;

		void readHeaderFromStream(std::istream& a_is);

		std::map<std::string, std::string> parsePostParameters() const;
		std::map<std::string, std::string> parseGetParameters() const;

		MV::Url url;
		Type type = Type::GET;
		std::string version = "HTTP/1.1";
		std::string postBody;
		std::string postContentType;

		size_t expectedBodyLength = 0;
		std::map<std::string, std::string> headerValues;
	};

	std::ostream& operator<<(std::ostream& a_os, const HttpResponse& a_response);
	std::istream& operator>>(std::istream& a_is, HttpResponse& a_response);

	enum class DownloadError { NONE, CANCELLED, TIMED_OUT, START, RESOLVE, CONNECT, TLS, WRITE, READ, MALFORMED_BODY, TRUNCATED_BODY, BODY_TOO_LARGE, TOO_MANY_REDIRECTS };

	struct DownloadResult {
		DownloadError error = DownloadError::NONE;
		// What went wrong, for logs; empty on success.
		std::string errorMessage;
		// The last response, after any redirects; status 0 when none arrived.
		HttpResponse response;
		// Every URL that answered with a redirect, in order.
		std::vector<MV::Url> redirects;
		MV::Url finalUrl;

		// A whole response arrived, whatever its status.
		bool received() const {
			return error == DownloadError::NONE;
		}

		// A whole 2xx response arrived.
		bool ok() const {
			return received() && response.ok();
		}
	};

	struct DownloadOptions {
		// The whole download, redirects included, fails with TIMED_OUT when it runs longer.
		std::chrono::milliseconds timeout {std::chrono::seconds(60)};
		// A longer body fails with BODY_TOO_LARGE; 0 accepts any length.
		size_t maxBodyBytes = 0;
		// https only: trust just this CA (PEM) instead of the system's root certificates.
		std::string trustedCaFile;
		// (total bytes, fraction arrived) after each read, once the length is known.
		std::function<void(size_t, float)> onProgress;
		// When set, onProgress and the completion callback are queued here and run when its owner calls execute();
		// otherwise they run on the thread running the download.
		std::shared_ptr<CallbackQueue> callbackQueue;
	};

	class DownloadRequest : public std::enable_shared_from_this<DownloadRequest> {
		friend class DownloadPool;
		friend DownloadResult downloadBlocking(const HttpRequest& a_request, DownloadOptions a_options);
	public:
		struct AsioDownload;
		using CompletionCallback = std::function<void(const std::shared_ptr<DownloadRequest>&)>;

		// Runs on a_context's threads; a_onComplete runs once, when the download succeeds or fails.
		static std::shared_ptr<DownloadRequest> make(const std::shared_ptr<NetworkContext>& a_context, const HttpRequest& a_request, CompletionCallback a_onComplete, DownloadOptions a_options = {});

		DownloadRequest(const DownloadRequest&) = delete;
		DownloadRequest& operator=(const DownloadRequest&) = delete;
		~DownloadRequest();

		// Fails the download with CANCELLED unless it already finished; safe from any thread.
		void cancel();

		bool complete() const;

		// Read it once complete() is true, or from the completion callback.
		const DownloadResult& result() const {
			return ourResult;
		}

		const HttpRequest& request() const {
			return ourRequest;
		}

	private:
		DownloadRequest(const HttpRequest& a_request, DownloadOptions a_options);

		void start();

		bool handleCancellation();
		void fail(DownloadError a_error, const std::error_code& a_err, const std::string& a_what);
		void fail(DownloadError a_error, const std::string& a_message);

		void initializeSocket();
		void closeSockets();
		void callOnComplete();
		void deliver(std::function<void()> a_callback);

		void initiateRequest(const MV::Url& a_url);

		void handleWriteRequest(const std::error_code& a_err);
		void handleReadHeaders(const std::error_code& a_err);
		void handleReadContent(const std::error_code& a_err);
		void initiateReadingMoreContent(size_t a_minimumTransferAmount);
		void continueReadingContent(bool a_endOfStream);
		void finishBody();

		HttpRequest ourRequest;
		DownloadOptions options;
		DownloadResult ourResult;

		std::unique_ptr<AsioDownload> asioDownload;

		std::string bodyBytes;
		size_t expectedBodyBytes = 0;
		bool chunkedBody = false;

		CompletionCallback onComplete;
		// Runs on the network thread even when onComplete is queued, so the pool forgets a finished download at once.
		CompletionCallback releaseFromPool;

		static constexpr size_t ChunkSize = 64 * 1024;

		// Every handler takes the lock: a context may run a request's handlers on different threads, and cancel() comes from any thread.
		mutable std::recursive_mutex criticalPath;
		bool cancelled = false;
		bool onCompleteCalled = false;
	};

	// Runs the download on the calling thread and returns when it finishes.
	DownloadResult downloadBlocking(const HttpRequest& a_request, DownloadOptions a_options = {});

	// The body of a 2xx response, or nullopt; blocks like downloadBlocking.
	std::optional<std::string> downloadStringBlocking(const MV::Url& a_url);

	std::string localIpV4();

	// Destroying the pool cancels the downloads still running on it.
	class DownloadPool : public std::enable_shared_from_this<DownloadPool> {
	public:
		// 0 threads = jai::thread_pool's default worker count
		static std::shared_ptr<DownloadPool> make(size_t a_threads = 0) {
			return std::shared_ptr<DownloadPool>(new DownloadPool(a_threads));
		}

		DownloadPool(const DownloadPool&) = delete;
		DownloadPool& operator=(const DownloadPool&) = delete;
		~DownloadPool() {
			cancelAll();
		}

		const std::shared_ptr<NetworkContext>& context() const {
			return ourContext;
		}

		std::shared_ptr<DownloadRequest> download(const HttpRequest& a_request, DownloadRequest::CompletionCallback a_onComplete, DownloadOptions a_options = {});

		void cancelAll();

	private:
		explicit DownloadPool(size_t a_threads) :
			ourContext(std::make_shared<NetworkContext>(a_threads)) {
		}

		std::shared_ptr<NetworkContext> ourContext;
		std::mutex activeRequestsMutex;
		std::set<std::shared_ptr<DownloadRequest>> activeRequests;
	};
}

#endif
