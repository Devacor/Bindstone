#include "download.h"
#include "MV/Network/networkReactor.h"
#include "MV/Network/networkStream.h"
#include "MV/Utility/log.h"
#include "MV/Utility/stringUtility.h"

#include <algorithm>
#include <optional>
#include <sstream>

namespace MV {

	namespace {
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
			return a_err == NetworkError::END_OF_STREAM || a_err == NetworkError::STREAM_TRUNCATED;
		}
	}

	struct DownloadRequest::Transport {
		explicit Transport(std::shared_ptr<NetworkContext> a_context) :
			sharedContext(std::move(a_context)),
			reactor(sharedContext->reactor()) {
		}

		// A blocking download's reactor runs on the calling thread until the download ends.
		explicit Transport(NetworkReactor& a_reactor) :
			reactor(a_reactor),
			stopWhenComplete(true) {
		}

		std::shared_ptr<NetworkContext> sharedContext;
		NetworkReactor& reactor;
		bool stopWhenComplete = false;
		std::shared_ptr<TlsContext> tlsContext;
		std::shared_ptr<NetworkStream> stream;
		NetworkReactor::TimerId deadline = 0;
	};

	void HttpResponse::readHeader(std::istream& a_stream) {
		headerValues.clear();
		headerLines.clear();

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
				headerLines.emplace_back(header.substr(0, index), trimmedHeaderValue(header, index));
				headerValues[MV::toLower(header.substr(0, index))] = headerLines.back().second;
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

	std::string HttpRequest::verb() const {
		return !method.empty() ? method : (type == HttpRequest::Type::POST ? "POST" : "GET");
	}

	void HttpRequest::writeRequest(std::ostream& a_os, const MV::Url& a_url) const {
		const bool hasBody = type == HttpRequest::Type::POST || !postBody.empty();
		a_os << verb() << " " << a_url.pathAndQuery() << " " << version << "\r\n";
		a_os << "Host: " << a_url.host();
		if (a_url.portString() != "0") {
			a_os << ":" << a_url.port();
		}
		a_os << "\r\n";
		bool typed = false;
		bool accepting = false;
		for (auto&& [name, value] : headers) {
			auto key = MV::toLower(name);
			if (key == "host" || key == "connection" || key == "content-length" || key == "transfer-encoding") {
				continue;
			}
			typed = typed || key == "content-type";
			accepting = accepting || key == "accept";
			a_os << name << ": " << value << "\r\n";
		}
		if (hasBody) {
			if (!typed) {
				a_os << "Content-Type: " << (postContentType.empty() ? "application/x-www-form-urlencoded" : postContentType) << "\r\n";
			}
			a_os << "Content-Length: " << postBody.size() << "\r\n";
		}
		if (!accepting) {
			a_os << "Accept: */*\r\n";
		}
		a_os << "Connection: close\r\n\r\n";
		if (hasBody) {
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
		NetworkReactor reactor(1);
		download->transport = std::make_unique<DownloadRequest::Transport>(reactor);
		download->start();
		reactor.run();
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
		std::error_code error;
		for (auto&& address : resolveAddresses(localHostName(), "", error)) {
			if (!address.ipv6()) {
				result = address.host();
			}
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
		result->transport = std::make_unique<Transport>(a_context);
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
		transport->reactor.post([self = shared_from_this()] {
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
		transport->reactor.cancel(transport->deadline);
		transport->deadline = 0;
		auto self = shared_from_this();
		if (releaseFromPool) {
			releaseFromPool(self);
		}
		if (onComplete) {
			deliver([self, callback = onComplete] { callback(self); });
		}
		if (transport->stopWhenComplete) {
			transport->reactor.stop();
		}
	}

	void DownloadRequest::closeSockets() {
		if (transport->stream) {
			transport->stream->close();
		}
	}

	void DownloadRequest::handleInput() {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (headerParsed) {
			continueReadingContent(false);
		} else {
			readHeader();
		}
	}

	void DownloadRequest::handleClosed(const std::error_code& a_err) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		if (!headerParsed) {
			if (transport->stream->writing()) {
				fail(DownloadError::WRITE, a_err, "Download Write Failure");
			} else {
				fail(DownloadError::READ, a_err, "Download Read Header Failure");
			}
		} else if (endOfStream(a_err)) {
			continueReadingContent(true);
		} else {
			fail(DownloadError::READ, a_err, "Download Read Content Failure");
		}
	}

	void DownloadRequest::continueReadingContent(bool a_endOfStream) {
		auto& stream = *transport->stream;
		bodyBytes.append(stream.input());
		stream.consume(stream.input().size());

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

	void DownloadRequest::readHeader() {
		auto& stream = *transport->stream;
		auto end = stream.input().find("\r\n\r\n");
		if (end == std::string_view::npos) {
			return;
		}
		std::istringstream header(std::string(stream.input().substr(0, end + 4)), std::ios::in | std::ios::binary);
		stream.consume(end + 4);
		headerParsed = true;
		auto& response = ourResult.response;
		response.readHeader(header);
		auto contentLength = response.headerValues.find("content-length");
		expectedBodyBytes = contentLength != response.headerValues.end() ? parsedContentLength(contentLength->second) : 0;
		auto transferEncoding = response.headerValues.find("transfer-encoding");
		chunkedBody = transferEncoding != response.headerValues.end() && MV::toLower(transferEncoding->second).find("chunked") != std::string::npos;
		auto location = response.headerValues.find("location");
		if (response.status >= 300 && response.status < 400 && location != response.headerValues.end() && options.maxRedirects > 0) {
			if (ourResult.redirects.size() >= options.maxRedirects) {
				fail(DownloadError::TOO_MANY_REDIRECTS, "Download stopped after " + std::to_string(options.maxRedirects) + " redirects.");
				return;
			}
			ourResult.redirects.push_back(ourResult.finalUrl);
			initiateRequest(MV::Url(ourResult.finalUrl, location->second));
		} else if (ourRequest.verb() == "HEAD" || response.status == 204 || response.status == 304 || response.status < 200) {
			expectedBodyBytes = 0;
			chunkedBody = false;
			stream.consume(stream.input().size());
			finishBody();
		} else {
			continueReadingContent(false);
		}
	}

	void DownloadRequest::sendRequest() {
		auto self = shared_from_this();
		auto& stream = *transport->stream;
		stream.onInput([self] { self->handleInput(); });
		stream.onClosed([self](const std::error_code& a_err) { self->handleClosed(a_err); });
		stream.write(requestBytes);
		stream.reading(true);
	}

	void DownloadRequest::initiateRequest(const MV::Url& a_url) {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		if (handleCancellation()) {
			return;
		}
		ourResult.finalUrl = a_url;
		headerParsed = false;
		bodyBytes.clear();
		expectedBodyBytes = 0;
		chunkedBody = false;
		try {
			initializeSocket();
		} catch (std::exception& e) {
			fail(DownloadError::TLS, e.what());
			return;
		}
		std::ostringstream requestStream(std::ios::out | std::ios::binary);
		ourRequest.writeRequest(requestStream, a_url);
		requestBytes = requestStream.str();

		auto stream = transport->stream;
		transport->reactor.resolve(a_url.host(), a_url.port() ? std::to_string(a_url.port()) : a_url.scheme(), [self = shared_from_this(), stream](const std::error_code& a_err, std::vector<SocketAddress> a_addresses) {
			std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
			if (stream != self->transport->stream || self->handleCancellation()) {
				return;
			}
			if (a_err) {
				self->fail(DownloadError::RESOLVE, a_err, "Download Resolve Failure");
				return;
			}
			stream->connect(std::move(a_addresses), [self, stream](const std::error_code& a_connectErr) {
				std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
				if (self->handleCancellation()) {
					return;
				}
				if (a_connectErr) {
					self->fail(DownloadError::CONNECT, a_connectErr, "Download Connection Failure");
					return;
				}
				if (!self->transport->tlsContext) {
					self->sendRequest();
					return;
				}
				auto host = self->ourResult.finalUrl.host();
				ssl_st* ssl = nullptr;
				try {
					ssl = stream->beginTls(*self->transport->tlsContext);
					verifyTlsPeerAs(ssl, host);
				} catch (std::exception& e) {
					self->fail(DownloadError::TLS, e.what());
					return;
				}
				stream->handshake([self, host, ssl](const std::error_code& a_handshakeErr) {
					std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
					if (self->handleCancellation()) {
						return;
					}
					if (a_handshakeErr) {
						self->fail(DownloadError::TLS, "Download Connection Handshake Failure [" + host + "]: " + describeTlsFailure(a_handshakeErr, ssl));
					} else {
						self->sendRequest();
					}
				});
			});
		});
	}

	void DownloadRequest::initializeSocket() {
		closeSockets();
		transport->stream.reset();
		transport->tlsContext = ourResult.finalUrl.scheme() == "https" ? makePublicTlsClientContext(options.trustedCaFile) : nullptr;
		transport->stream = NetworkStream::make(transport->reactor);
	}

	void DownloadRequest::start() {
		transport->reactor.post([self = shared_from_this()] { self->begin(); });
	}

	void DownloadRequest::begin() {
		std::scoped_lock<std::recursive_mutex> lock(criticalPath);
		transport->deadline = transport->reactor.scheduleAfter(options.timeout, [weak = std::weak_ptr<DownloadRequest>(shared_from_this())] {
			if (auto self = weak.lock()) {
				std::scoped_lock<std::recursive_mutex> lock(self->criticalPath);
				self->transport->deadline = 0;
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
		result->transport = std::make_unique<DownloadRequest::Transport>(ourContext);
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
