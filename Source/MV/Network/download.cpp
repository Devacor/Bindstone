#include "download.h"
#include "MV/Network/networkContextAsio.h"
#include "MV/Network/tlsAsio.h"
#include "MV/Utility/log.h"
#include "MV/Utility/stringUtility.h"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <algorithm>
#include <optional>
#include <sstream>

namespace MV {

	namespace {
		using tcp = asio::ip::tcp;

		constexpr size_t MAX_REDIRECTS = 32;

		std::string trimmedHeaderValue(const std::string& a_line, size_t a_colon) {
			auto first = a_line.find_first_not_of(" \t", a_colon + 1);
			if (first == std::string::npos) {
				return {};
			}
			auto last = a_line.find_last_not_of(" \t\r");
			return a_line.substr(first, last - first + 1);
		}

		size_t parsedContentLength(const std::string& a_value) {
			try {
				return static_cast<size_t>(std::stoull(a_value));
			} catch (std::exception& e) {
				MV::warning("Failed to parse content-length as an unsigned long: [", a_value, "] ", e.what());
				return 0;
			}
		}

		bool iSubstringEquals(const std::string& a_fullString, const std::string& a_substring, size_t a_begin = 0) {
			size_t end = a_begin + a_substring.size();
			if (a_fullString.size() < end) {
				return false;
			}
			return std::equal(a_fullString.begin() + a_begin, a_fullString.begin() + end, a_substring.begin(), [](char l, char r) {return std::tolower(l) == std::tolower(r); });
		}

		// The chunks of a chunked body, joined; nullopt when the framing is malformed or the final chunk never arrived.
		std::optional<std::string> decodeChunkedBody(const std::string& a_raw) {
			std::string result;
			size_t position = 0;
			while (true) {
				auto lineEnd = a_raw.find("\r\n", position);
				if (lineEnd == std::string::npos) {
					return std::nullopt;
				}
				auto sizeText = a_raw.substr(position, lineEnd - position);
				sizeText = sizeText.substr(0, sizeText.find(';'));
				size_t size = 0;
				try {
					size = static_cast<size_t>(std::stoull(sizeText, nullptr, 16));
				} catch (...) {
					return std::nullopt;
				}
				position = lineEnd + 2;
				if (size == 0) {
					return result;
				}
				if (size > a_raw.size() - position || a_raw.size() - position - size < 2) {
					return std::nullopt;
				}
				result.append(a_raw, position, size);
				position += size;
				if (a_raw.compare(position, 2, "\r\n") != 0) {
					return std::nullopt;
				}
				position += 2;
			}
		}

		bool endOfStream(const std::error_code& a_err) {
			// A server that closes TLS without close_notify still ends the body when the length came from the close
			return a_err == asio::error::eof || a_err == asio::ssl::error::stream_truncated;
		}
	}

	struct DownloadRequest::AsioDownload {
		struct TlsSocket {
			TlsSocket(asio::io_context& a_ioContext, const std::string& a_trustedCaFile) :
				context(makePublicTlsClientContext(a_trustedCaFile)),
				stream(a_ioContext, *context) {
			}

			std::shared_ptr<asio::ssl::context> context;
			TlsStream stream;
		};

		explicit AsioDownload(std::shared_ptr<NetworkContext> a_context) :
			sharedContext(std::move(a_context)),
			ioContext(sharedContext->asio().ioContext),
			deadline(ioContext) {
		}

		AsioDownload() :
			ownedContext(std::in_place),
			ioContext(*ownedContext),
			deadline(ioContext) {
		}

		template <typename Handler>
		void read(size_t a_minimum, Handler&& a_handler) {
			if (tls) {
				asio::async_read(tls->stream, *response, asio::transfer_at_least(a_minimum), std::forward<Handler>(a_handler));
			} else {
				asio::async_read(*socket, *response, asio::transfer_at_least(a_minimum), std::forward<Handler>(a_handler));
			}
		}

		std::shared_ptr<NetworkContext> sharedContext;
		std::optional<asio::io_context> ownedContext;
		asio::io_context& ioContext;
		std::unique_ptr<tcp::resolver> resolver;
		std::unique_ptr<tcp::socket> socket;
		std::unique_ptr<TlsSocket> tls;
		std::unique_ptr<asio::streambuf> request;
		std::unique_ptr<asio::streambuf> response;
		asio::steady_timer deadline;
	};

	void HttpResponse::readHeader(std::istream& a_stream) {
		headerValues.clear();

		a_stream >> version;
		std::string statusCode;
		a_stream >> statusCode;
		try {
			status = std::stoi(statusCode);
		} catch (...) {
			status = 0;
		}

		getline_platform_agnostic(a_stream, message);
		if (!message.empty() && message[0] == ' ') {
			message = message.substr(1);
		}

		std::string header;
		while (getline_platform_agnostic(a_stream, header) && !header.empty()) {
			auto index = header.find_first_of(':');
			if (index != std::string::npos && index > 0) {
				headerValues[MV::toLower(header.substr(0, index))] = trimmedHeaderValue(header, index);
			}
		}
	}

	void HttpResponse::writeResponse(std::ostream& a_stream) const {
		a_stream << version << " " << status << " " << message << "\r\n";
		if (!body.empty()) {
			a_stream << "content-length: " << body.size() << "\r\n";
		}
		for (auto&& kvp : headerValues) {
			if (body.empty() || MV::toLower(kvp.first) != "content-length") {
				a_stream << kvp.first << ": " << kvp.second << "\r\n";
			}
		}
		a_stream << "\r\n";
		a_stream << body;
	}

	std::string HttpResponse::to_string() const {
		std::ostringstream result(std::ios::out | std::ios::binary);
		writeResponse(result);
		return result.str();
	}

	std::ostream& operator<<(std::ostream& a_os, const HttpResponse& a_response) {
		a_response.writeResponse(a_os);
		return a_os;
	}

	std::istream& operator>>(std::istream& a_is, HttpResponse& a_response) {
		a_response.readHeader(a_is);
		return a_is;
	}

	void HttpRequest::writeRequest(std::ostream& a_os, const MV::Url& a_url) const {
		a_os << (type == HttpRequest::Type::POST ? "POST" : "GET") << " " << a_url.pathAndQuery() << " " << version << "\r\n";
		a_os << "Host: " << a_url.host() << "\r\n";
		if (type == HttpRequest::Type::POST) {
			a_os << "Content-Type: " << (postContentType.empty() ? "application/x-www-form-urlencoded" : postContentType) << "\r\n";
			a_os << "Content-Length: " << postBody.size() << "\r\n";
		}
		a_os << "Accept: */*\r\n";
		a_os << "Connection: close\r\n\r\n";
		if (type == HttpRequest::Type::POST) {
			a_os << postBody;
		}
	}

	void HttpRequest::readHeaderFromStream(std::istream& a_is) {
		std::string line;
		if (!getline_platform_agnostic(a_is, line)) {
			return;
		}
		std::stringstream lineStream;
		lineStream << line;
		{
			std::string method;
			lineStream >> method;
			type = iSubstringEquals(method, "post") ? Type::POST : Type::GET;
		}
		std::string pathAndQuery;
		lineStream >> pathAndQuery >> version;

		while (getline_platform_agnostic(a_is, line) && !line.empty()) {
			auto index = line.find_first_of(':');
			if (index != std::string::npos && index > 0) {
				auto key = MV::toLower(line.substr(0, index));
				auto value = trimmedHeaderValue(line, index);
				headerValues[key] = value;
				if (key == "host") {
					url = value + pathAndQuery;
				} else if (key == "content-length") {
					expectedBodyLength = parsedContentLength(value);
				} else if (key == "content-type") {
					postContentType = value;
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

	DownloadResult downloadBlocking(const HttpRequest& a_request, DownloadOptions a_options) {
		auto download = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_request, std::move(a_options)));
		download->asioDownload = std::make_unique<DownloadRequest::AsioDownload>();
		download->start();
		download->asioDownload->ioContext.run();
		return download->ourResult;
	}

	std::optional<std::string> downloadStringBlocking(const Url& a_url) {
		auto result = downloadBlocking(a_url);
		if (!result.ok()) {
			return std::nullopt;
		}
		return std::move(result.response.body);
	}

	std::string localIpV4() {
		std::string result = "0.0.0.0";
		try {
			asio::io_context ioContext;
			tcp::resolver resolver(ioContext);
			for (auto&& entry : resolver.resolve(asio::ip::host_name(), "")) {
				auto address = entry.endpoint().address();
				if (address.is_v4()) {
					result = address.to_string();
				}
			}
		} catch(...) {
		}
		return result;
	}

	DownloadRequest::DownloadRequest(const HttpRequest& a_request, DownloadOptions a_options) :
		ourRequest(a_request),
		options(std::move(a_options)) {
	}

	DownloadRequest::~DownloadRequest() = default;

	std::shared_ptr<DownloadRequest> DownloadRequest::make(const std::shared_ptr<NetworkContext>& a_context, const HttpRequest& a_request, CompletionCallback a_onComplete, DownloadOptions a_options) {
		auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_request, std::move(a_options)));
		result->onComplete = std::move(a_onComplete);
		result->asioDownload = std::make_unique<AsioDownload>(a_context);
		result->start();
		return result;
	}

	bool DownloadRequest::complete() const {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		return onCompleteCalled;
	}

	void DownloadRequest::cancel() {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (onCompleteCalled || cancelled) {
			return;
		}
		cancelled = true;
		// The failure runs on the network thread, which owns the sockets
		asio::post(asioDownload->ioContext, [self = shared_from_this()] {
			std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
			self->handleCancellation();
		});
	}

	bool DownloadRequest::handleCancellation() {
		if (cancelled) {
			fail(DownloadError::CANCELLED, "Download cancelled.");
			return true;
		}
		return false;
	}

	void DownloadRequest::fail(DownloadError a_error, const std::error_code& a_err, const std::string& a_what) {
		fail(a_error, a_what + ": " + a_err.message());
	}

	void DownloadRequest::fail(DownloadError a_error, const std::string& a_message) {
		if (onCompleteCalled) {
			return;
		}
		ourResult.error = a_error;
		ourResult.errorMessage = a_message;
		if (a_error != DownloadError::CANCELLED) {
			MV::error(a_message);
		}
		closeSockets();
		callOnComplete();
	}

	void DownloadRequest::deliver(std::function<void()> a_callback) {
		if (options.callbackQueue) {
			options.callbackQueue->then(std::move(a_callback));
		} else {
			a_callback();
		}
	}

	void DownloadRequest::callOnComplete() {
		if (onCompleteCalled) {
			return;
		}
		onCompleteCalled = true;
		asioDownload->deadline.cancel();
		auto self = shared_from_this();
		if (releaseFromPool) {
			releaseFromPool(self);
		}
		if (onComplete) {
			deliver([self, callback = onComplete] { callback(self); });
		}
	}

	// Closing cancels a pending operation, whose handler still runs against the stream, so the streams stay alive until
	// initializeSocket replaces them (no operation is pending then) or the request dies (no handler holds it then).
	void DownloadRequest::closeSockets() {
		asio::error_code ignored;
		if (asioDownload->socket) {
			asioDownload->socket->shutdown(tcp::socket::shutdown_both, ignored);
			asioDownload->socket->close(ignored);
		}
		if (asioDownload->tls) {
			asioDownload->tls->stream.lowest_layer().shutdown(tcp::socket::shutdown_both, ignored);
			asioDownload->tls->stream.lowest_layer().close(ignored);
		}
	}

	void DownloadRequest::handleReadContent(const std::error_code& a_err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!a_err) {
			continueReadingContent(false);
		} else if (endOfStream(a_err)) {
			continueReadingContent(true);
		} else {
			fail(DownloadError::READ, a_err, "Download Read Content Failure");
		}
	}

	void DownloadRequest::initiateReadingMoreContent(size_t a_minimumTransferAmount) {
		asioDownload->read(a_minimumTransferAmount, [self = shared_from_this()](const asio::error_code& a_err, size_t) { self->handleReadContent(a_err); });
	}

	void DownloadRequest::continueReadingContent(bool a_endOfStream) {
		auto& response = *asioDownload->response;
		auto data = response.data();
		bodyBytes.append(asio::buffers_begin(data), asio::buffers_end(data));
		response.consume(response.size());

		if (options.maxBodyBytes && bodyBytes.size() > options.maxBodyBytes) {
			fail(DownloadError::BODY_TOO_LARGE, "Download exceeded the " + std::to_string(options.maxBodyBytes) + " byte limit.");
			return;
		}
		const bool sizeKnown = expectedBodyBytes && !chunkedBody;
		if (sizeKnown && options.onProgress) {
			auto fraction = std::min(1.0f, bodyBytes.size() / static_cast<float>(expectedBodyBytes));
			deliver([callback = options.onProgress, total = expectedBodyBytes, fraction] { callback(total, fraction); });
		}
		if (a_endOfStream || (sizeKnown && bodyBytes.size() >= expectedBodyBytes)) {
			finishBody();
		} else {
			initiateReadingMoreContent(sizeKnown ? std::min(ChunkSize, expectedBodyBytes - bodyBytes.size()) : 1);
		}
	}

	void DownloadRequest::finishBody() {
		if (chunkedBody) {
			auto decoded = decodeChunkedBody(bodyBytes);
			if (!decoded) {
				fail(DownloadError::MALFORMED_BODY, "Download ended inside a malformed or unfinished chunked body.");
				return;
			}
			bodyBytes = std::move(*decoded);
		} else if (expectedBodyBytes) {
			if (bodyBytes.size() < expectedBodyBytes) {
				fail(DownloadError::TRUNCATED_BODY, "Download closed after " + std::to_string(bodyBytes.size()) + " of " + std::to_string(expectedBodyBytes) + " body bytes.");
				return;
			}
			bodyBytes.resize(expectedBodyBytes);
		}
		ourResult.response.body = std::move(bodyBytes);
		bodyBytes.clear();
		closeSockets();
		callOnComplete();
	}

	void DownloadRequest::handleReadHeaders(const std::error_code& a_err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (a_err) {
			fail(DownloadError::READ, a_err, "Download Read Header Failure");
			return;
		}
		auto& response = ourResult.response;
		std::istream responseStream(asioDownload->response.get());
		response.readHeader(responseStream);
		auto contentLength = response.headerValues.find("content-length");
		expectedBodyBytes = contentLength != response.headerValues.end() ? parsedContentLength(contentLength->second) : 0;
		auto transferEncoding = response.headerValues.find("transfer-encoding");
		chunkedBody = transferEncoding != response.headerValues.end() && MV::toLower(transferEncoding->second).find("chunked") != std::string::npos;
		auto location = response.headerValues.find("location");
		if (response.status >= 300 && response.status < 400 && location != response.headerValues.end()) {
			if (ourResult.redirects.size() >= MAX_REDIRECTS) {
				fail(DownloadError::TOO_MANY_REDIRECTS, "Download stopped after " + std::to_string(MAX_REDIRECTS) + " redirects.");
				return;
			}
			ourResult.redirects.push_back(ourResult.finalUrl);
			initiateRequest(MV::Url(ourResult.finalUrl, location->second));
		} else {
			continueReadingContent(false);
		}
	}

	void DownloadRequest::handleWriteRequest(const std::error_code& a_err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (a_err) {
			fail(DownloadError::WRITE, a_err, "Download Write Failure");
			return;
		}
		auto onHeaders = [self = shared_from_this()](const asio::error_code& a_readErr, size_t) { self->handleReadHeaders(a_readErr); };
		if (asioDownload->tls) {
			asio::async_read_until(asioDownload->tls->stream, *asioDownload->response, "\r\n\r\n", onHeaders);
		} else {
			asio::async_read_until(*asioDownload->socket, *asioDownload->response, "\r\n\r\n", onHeaders);
		}
	}

	void DownloadRequest::initiateRequest(const MV::Url& a_url) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		ourResult.finalUrl = a_url;
		bodyBytes.clear();
		try {
			initializeSocket();
		} catch (std::exception& e) {
			fail(DownloadError::TLS, e.what());
			return;
		}
		asioDownload->request = std::make_unique<asio::streambuf>();
		asioDownload->response = std::make_unique<asio::streambuf>();

		std::ostream requestStream(asioDownload->request.get());
		ourRequest.writeRequest(requestStream, a_url);

		auto self = shared_from_this();
		asioDownload->resolver->async_resolve(a_url.host(), a_url.port() ? std::to_string(a_url.port()) : a_url.scheme(), [self](const asio::error_code& a_err, tcp::resolver::results_type a_endpoints) {
			std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
			if (self->handleCancellation()) {
				return;
			}
			if (a_err) {
				self->fail(DownloadError::RESOLVE, a_err, "Download Resolve Failure");
				return;
			}
			// async_connect tries each endpoint in turn
			auto onConnected = [self](const asio::error_code& a_connectErr, const tcp::endpoint&) {
				std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
				if (self->handleCancellation()) {
					return;
				}
				if (a_connectErr) {
					self->fail(DownloadError::CONNECT, a_connectErr, "Download Connection Failure");
					return;
				}
				auto onWritten = [self](const asio::error_code& a_writeErr, size_t) { self->handleWriteRequest(a_writeErr); };
				if (self->asioDownload->tls) {
					auto host = self->ourResult.finalUrl.host();
					try {
						verifyTlsPeerAs(self->asioDownload->tls->stream, host);
					} catch (std::exception& e) {
						self->fail(DownloadError::TLS, e.what());
						return;
					}
					self->asioDownload->tls->stream.async_handshake(asio::ssl::stream_base::client, [self, onWritten, host](const asio::error_code& a_handshakeErr) {
						std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
						if (self->handleCancellation()) {
							return;
						}
						if (a_handshakeErr) {
							self->fail(DownloadError::TLS, "Download Connection Handshake Failure [" + host + "]: " + describeTlsFailure(a_handshakeErr, self->asioDownload->tls->stream));
						} else {
							asio::async_write(self->asioDownload->tls->stream, *self->asioDownload->request, onWritten);
						}
					});
				} else {
					asio::async_write(*self->asioDownload->socket, *self->asioDownload->request, onWritten);
				}
			};
			if (self->asioDownload->tls) {
				asio::async_connect(self->asioDownload->tls->stream.lowest_layer(), a_endpoints, onConnected);
			} else {
				asio::async_connect(*self->asioDownload->socket, a_endpoints, onConnected);
			}
		});
	}

	void DownloadRequest::initializeSocket() {
		if (!asioDownload->resolver) {
			asioDownload->resolver = std::make_unique<tcp::resolver>(asioDownload->ioContext);
		}
		closeSockets();
		asioDownload->socket.reset();
		asioDownload->tls.reset();
		if (ourResult.finalUrl.scheme() == "https") {
			asioDownload->tls = std::make_unique<AsioDownload::TlsSocket>(asioDownload->ioContext, options.trustedCaFile);
		} else {
			asioDownload->socket = std::make_unique<tcp::socket>(asioDownload->ioContext);
		}
	}

	void DownloadRequest::start() {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		asioDownload->deadline.expires_after(options.timeout);
		asioDownload->deadline.async_wait([weak = std::weak_ptr<DownloadRequest>(shared_from_this())](const asio::error_code& a_err) {
			if (a_err) {
				return;
			}
			if (auto self = weak.lock()) {
				std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
				self->fail(DownloadError::TIMED_OUT, "Download timed out after " + std::to_string(self->options.timeout.count()) + " ms.");
			}
		});
		try {
			initiateRequest(ourRequest.url);
		} catch (std::exception& e) {
			fail(DownloadError::START, std::string("Download failed to start: ") + e.what());
		} catch (...) {
			fail(DownloadError::START, "Download failed to start.");
		}
	}

	std::shared_ptr<DownloadRequest> DownloadPool::download(const HttpRequest& a_request, DownloadRequest::CompletionCallback a_onComplete, DownloadOptions a_options) {
		auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_request, std::move(a_options)));
		result->onComplete = std::move(a_onComplete);
		result->releaseFromPool = [pool = std::weak_ptr<DownloadPool>(shared_from_this())](const std::shared_ptr<DownloadRequest>& a_download) {
			if (auto self = pool.lock()) {
				std::scoped_lock<std::mutex> lock(self->activeRequestsMutex);
				self->activeRequests.erase(a_download);
			}
		};
		result->asioDownload = std::make_unique<DownloadRequest::AsioDownload>(ourContext);
		{
			std::scoped_lock<std::mutex> lock(activeRequestsMutex);
			activeRequests.insert(result);
		}
		// Registered before it starts, so a download that finishes at once is still forgotten
		result->start();
		return result;
	}

	void DownloadPool::cancelAll() {
		std::set<std::shared_ptr<DownloadRequest>> cancelling;
		{
			std::scoped_lock<std::mutex> lock(activeRequestsMutex);
			cancelling.swap(activeRequests);
		}
		// Outside the lock: a finishing download holds its own lock while it takes this one
		for (auto&& download : cancelling) {
			download->cancel();
		}
	}
}
