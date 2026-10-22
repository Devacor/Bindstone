#include "MV/Network/networkStream.h"
#include "MV/Utility/log.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <stdexcept>

namespace MV {

	namespace {
		const size_t READ_CHUNK_BYTES = 64 * 1024;
		const int READS_PER_WAKE = 16;
		const size_t COMPACT_AFTER_BYTES = 256 * 1024;
	}

	NetworkStream::NetworkStream(NetworkReactor &a_reactor) :
		reactor(a_reactor) {
		scratch.resize(READ_CHUNK_BYTES);
	}

	std::shared_ptr<NetworkStream> NetworkStream::make(NetworkReactor &a_reactor) {
		auto result = std::shared_ptr<NetworkStream>(new NetworkStream(a_reactor));
		a_reactor.watch(result);
		return result;
	}

	std::shared_ptr<NetworkStream> NetworkStream::make(NetworkReactor &a_reactor, Socket a_connected) {
		auto result = std::shared_ptr<NetworkStream>(new NetworkStream(a_reactor));
		result->socket = std::move(a_connected);
		result->socket.nonBlocking(true);
		result->ourPeer = result->socket.peerAddress();
		a_reactor.watch(result);
		return result;
	}

	NetworkStream::~NetworkStream() {
		if (ssl) {
			SSL_free(ssl);
		}
	}

	void NetworkStream::connect(std::vector<SocketAddress> a_addresses, ErrorCallback a_onConnected) {
		addresses = std::move(a_addresses);
		nextAddress = 0;
		connectError = make_error_code(NetworkError::INVALID_ADDRESS);
		onConnectedCallback = std::move(a_onConnected);
		connectNext();
	}

	void NetworkStream::connectNext() {
		socket.close();
		while (nextAddress < addresses.size()) {
			auto& address = addresses[nextAddress++];
			std::error_code error;
			auto attempt = Socket::tcp(address, error);
			if (!error) {
				error = attempt.nonBlocking(true);
			}
			if (!error) {
				error = attempt.connect(address);
			}
			if (!error || socketWouldBlock(error)) {
				socket = std::move(attempt);
				connecting = true;
				return;
			}
			connectError = error;
		}
		connecting = false;
		reactor.post([self = shared_from_this(), error = connectError] {
			if (auto callback = std::move(self->onConnectedCallback)) {
				callback(error);
			}
		});
	}

	void NetworkStream::connectionSettled() {
		if (auto error = socket.pendingError()) {
			connectError = error;
			connectNext();
			return;
		}
		connecting = false;
		ourPeer = socket.peerAddress();
		if (auto callback = std::move(onConnectedCallback)) {
			callback({});
		}
	}

	ssl_st* NetworkStream::beginTls(const TlsContext &a_context) {
		ssl = SSL_new(a_context.native());
		if (!ssl) {
			throw std::runtime_error("TLS: OpenSSL could not make a session");
		}
		networkIn = BIO_new(BIO_s_mem());
		networkOut = BIO_new(BIO_s_mem());
		BIO_set_mem_eof_return(networkIn, -1);
		BIO_set_mem_eof_return(networkOut, -1);
		SSL_set_bio(ssl, networkIn, networkOut);
		SSL_set_mode(ssl, SSL_MODE_RELEASE_BUFFERS);
		if (a_context.server()) {
			SSL_set_accept_state(ssl);
		} else {
			SSL_set_connect_state(ssl);
		}
		auto unread = input();
		if (!unread.empty()) {
			BIO_write(networkIn, unread.data(), static_cast<int>(unread.size()));
		}
		inputBuffer.clear();
		inputConsumed = 0;
		return ssl;
	}

	void NetworkStream::handshake(ErrorCallback a_onComplete) {
		onHandshakeCallback = std::move(a_onComplete);
		tlsHandshaking = true;
		reactor.post([self = shared_from_this()] {
			if (self->open() && self->tlsHandshaking) {
				self->runTls();
				self->deliverInput();
			}
		});
	}

	void NetworkStream::reading(bool a_reading) {
		readEnabled = a_reading;
		if (readEnabled && !deliveryPosted && (!input().empty() || peerFinished)) {
			deliveryPosted = true;
			reactor.post([self = shared_from_this()] {
				self->deliveryPosted = false;
				if (self->open()) {
					self->deliverInput();
				}
			});
		}
	}

	void NetworkStream::consume(size_t a_bytes) {
		inputConsumed = std::min(inputBuffer.size(), inputConsumed + a_bytes);
		if (inputConsumed == inputBuffer.size()) {
			inputBuffer.clear();
			inputConsumed = 0;
		} else if (inputConsumed > COMPACT_AFTER_BYTES && inputConsumed * 2 > inputBuffer.size()) {
			inputBuffer.erase(0, inputConsumed);
			inputConsumed = 0;
		}
	}

	void NetworkStream::write(std::string_view a_bytes) {
		if (!open() || broken || finishing) {
			return;
		}
		if (ssl && tlsHandshaking) {
			pendingPlaintext.append(a_bytes);
			return;
		}
		if (outputSent > COMPACT_AFTER_BYTES && outputSent * 2 > outputBuffer.size()) {
			outputBuffer.erase(0, outputSent);
			outputSent = 0;
		}
		if (!ssl) {
			outputBuffer.append(a_bytes);
			return;
		}
		size_t offset = 0;
		while (offset < a_bytes.size()) {
			ERR_clear_error();
			auto chunk = static_cast<int>(std::min<size_t>(a_bytes.size() - offset, 1 << 30));
			auto written = SSL_write(ssl, a_bytes.data() + offset, chunk);
			if (written <= 0) {
				fail(tlsError(SSL_get_error(ssl, written)));
				return;
			}
			offset += static_cast<size_t>(written);
		}
		moveTlsOutput();
	}

	void NetworkStream::finish(std::function<void()> a_onFinished) {
		onFinishedCallback = std::move(a_onFinished);
		finishing = true;
		discarding = true;
		inputBuffer.clear();
		inputConsumed = 0;
		if (!open() || broken) {
			reactor.post([self = shared_from_this()] { self->finished(); });
			return;
		}
		if (ssl && !tlsHandshaking) {
			ERR_clear_error();
			SSL_shutdown(ssl);
			ERR_clear_error();
			moveTlsOutput();
		}
		if (!writing()) {
			shutSend();
		}
	}

	void NetworkStream::close() {
		auto self = shared_from_this();
		reactor.unwatch(this);
		abandon();
	}

	void NetworkStream::abandon() {
		socket.close();
		connecting = false;
		onInputCallback = nullptr;
		onClosedCallback = nullptr;
		onFinishedCallback = nullptr;
		onConnectedCallback = nullptr;
		onHandshakeCallback = nullptr;
	}

	bool NetworkStream::wantsRead() const {
		if (!open() || connecting || broken || socketEnded) {
			return false;
		}
		return discarding || ((readEnabled || tlsHandshaking) && !peerFinished);
	}

	bool NetworkStream::wantsWrite() const {
		return open() && !broken && (connecting || outputSent < outputBuffer.size());
	}

	void NetworkStream::ready(bool a_readable, bool a_writable) {
		auto self = shared_from_this();
		if (connecting) {
			connectionSettled();
			return;
		}
		if (a_writable && wantsWrite()) {
			flushOutput();
		}
		if (a_readable && wantsRead()) {
			readSocket();
		}
	}

	void NetworkStream::readSocket() {
		for (int i = 0; i < READS_PER_WAKE && wantsRead(); ++i) {
			std::error_code error;
			auto received = socket.receive(scratch.data(), scratch.size(), error);
			if (error) {
				if (!socketWouldBlock(error)) {
					fail(error);
				}
				return;
			}
			if (received == 0) {
				socketClosedByPeer();
				return;
			}
			if (discarding) {
				continue;
			}
			if (ssl) {
				BIO_write(networkIn, scratch.data(), static_cast<int>(received));
				runTls();
			} else {
				inputBuffer.append(scratch.data(), received);
			}
			if (!open()) {
				return;
			}
			deliverInput();
			if (!open()) {
				return;
			}
		}
	}

	void NetworkStream::runTls() {
		if (tlsHandshaking) {
			advanceHandshake();
			if (tlsHandshaking || !open() || broken) {
				return;
			}
		}
		decrypt();
	}

	void NetworkStream::advanceHandshake() {
		ERR_clear_error();
		auto result = SSL_do_handshake(ssl);
		moveTlsOutput();
		if (result == 1) {
			tlsHandshaking = false;
			if (!pendingPlaintext.empty()) {
				auto plaintext = std::move(pendingPlaintext);
				pendingPlaintext.clear();
				write(plaintext);
			}
			if (auto callback = std::move(onHandshakeCallback)) {
				callback({});
			}
			return;
		}
		auto code = SSL_get_error(ssl, result);
		if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE) {
			return;
		}
		auto error = tlsError(code);
		flushOutput();
		tlsHandshaking = false;
		broken = true;
		if (auto callback = std::move(onHandshakeCallback)) {
			callback(error);
		}
	}

	void NetworkStream::decrypt() {
		while (!peerFinished) {
			ERR_clear_error();
			auto result = SSL_read(ssl, scratch.data(), static_cast<int>(scratch.size()));
			if (result > 0) {
				inputBuffer.append(scratch.data(), static_cast<size_t>(result));
				continue;
			}
			auto code = SSL_get_error(ssl, result);
			if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE) {
				break;
			}
			if (code == SSL_ERROR_ZERO_RETURN) {
				peerFinished = true;
				finishCode = make_error_code(NetworkError::END_OF_STREAM);
				break;
			}
			moveTlsOutput();
			fail(tlsError(code));
			return;
		}
		moveTlsOutput();
	}

	void NetworkStream::moveTlsOutput() {
		while (auto pending = BIO_ctrl_pending(networkOut)) {
			auto offset = outputBuffer.size();
			outputBuffer.resize(offset + pending);
			auto read = BIO_read(networkOut, outputBuffer.data() + offset, static_cast<int>(pending));
			outputBuffer.resize(offset + static_cast<size_t>(std::max(read, 0)));
			if (read <= 0) {
				return;
			}
		}
	}

	void NetworkStream::flushOutput() {
		while (outputSent < outputBuffer.size()) {
			std::error_code error;
			auto sent = socket.send(outputBuffer.data() + outputSent, outputBuffer.size() - outputSent, error);
			if (error) {
				if (!socketWouldBlock(error)) {
					fail(error);
				}
				return;
			}
			outputSent += sent;
		}
		outputBuffer.clear();
		outputSent = 0;
		if (finishing) {
			shutSend();
		}
	}

	void NetworkStream::shutSend() {
		if (!sendShut) {
			sendShut = true;
			socket.shutdownSend();
		}
		if (socketEnded) {
			finished();
		}
	}

	void NetworkStream::finished() {
		if (auto callback = std::move(onFinishedCallback)) {
			callback();
		}
	}

	void NetworkStream::deliverInput() {
		if (!readEnabled || discarding) {
			return;
		}
		if (!input().empty() && onInputCallback) {
			auto callback = onInputCallback;
			callback();
		}
		if (readEnabled && peerFinished && open()) {
			reportClosed(finishCode);
		}
	}

	void NetworkStream::socketClosedByPeer() {
		socketEnded = true;
		if (discarding) {
			if (!writing()) {
				finished();
			}
			return;
		}
		if (tlsHandshaking) {
			fail(make_error_code(NetworkError::STREAM_TRUNCATED));
			return;
		}
		if (!peerFinished) {
			peerFinished = true;
			finishCode = make_error_code(ssl ? NetworkError::STREAM_TRUNCATED : NetworkError::END_OF_STREAM);
		}
		deliverInput();
	}

	void NetworkStream::fail(const std::error_code &a_error) {
		broken = true;
		if (tlsHandshaking) {
			tlsHandshaking = false;
			if (auto callback = std::move(onHandshakeCallback)) {
				callback(a_error);
			}
		} else if (finishing) {
			finished();
		} else {
			reportClosed(a_error);
		}
	}

	void NetworkStream::reportClosed(const std::error_code &a_error) {
		if (!closeReported) {
			closeReported = true;
			if (auto callback = std::move(onClosedCallback)) {
				callback(a_error);
			}
		}
	}

	std::error_code NetworkStream::tlsError(int a_code) {
		if (auto queued = ERR_get_error()) {
			ERR_clear_error();
			return {static_cast<int>(queued), tlsCategory()};
		}
		if (a_code == SSL_ERROR_SYSCALL || a_code == SSL_ERROR_ZERO_RETURN) {
			return make_error_code(NetworkError::STREAM_TRUNCATED);
		}
		return make_error_code(NetworkError::TLS_FAILURE);
	}

	std::shared_ptr<NetworkListener> NetworkListener::make(NetworkReactor &a_reactor, const SocketAddress &a_address, std::function<void(Socket)> a_onAccepted) {
		auto result = std::shared_ptr<NetworkListener>(new NetworkListener());
		std::error_code error;
		result->socket = Socket::tcp(a_address, error);
		if (!error) {
			error = result->socket.bind(a_address);
		}
		if (!error) {
			error = result->socket.listen();
		}
		if (!error) {
			error = result->socket.nonBlocking(true);
		}
		if (error) {
			throw std::system_error(error, "Listening on " + a_address.to_string());
		}
		auto bound = result->socket.localAddress();
		result->ourPort = bound ? bound->port() : a_address.port();
		result->onAccepted = std::move(a_onAccepted);
		a_reactor.post([&a_reactor, result] { a_reactor.watch(result); });
		return result;
	}

	void NetworkListener::ready(bool a_readable, bool) {
		for (int i = 0; a_readable && i < 64 && socket.valid(); ++i) {
			std::error_code error;
			auto accepted = socket.accept(error);
			if (error) {
				if (!socketWouldBlock(error)) {
					warning("Accept failed: ", error.message());
				}
				return;
			}
			if (onAccepted) {
				auto callback = onAccepted;
				callback(std::move(accepted));
			}
		}
	}

	void NetworkListener::abandon() {
		socket.close();
		onAccepted = nullptr;
	}
}
