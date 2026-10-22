// Blocking peers for the network tests: MV sockets and OpenSSL driven directly, so a test can do what MV's own
// clients and servers never would (send anything, present any certificate, stall, speak plaintext to a TLS port).
#pragma once

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "MV/Network/socket.h"
#include "MV/Network/tls.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace mv_tests {

// How a blocking read ended.
enum class PeerRead { OK, CLOSED, TIMED_OUT, FAILED };

inline const char* describe(PeerRead a_read) {
	switch (a_read) {
		case PeerRead::OK:
			return "ok";
		case PeerRead::CLOSED:
			return "closed by the peer";
		case PeerRead::TIMED_OUT:
			return "timed out";
		case PeerRead::FAILED:
			return "failed";
	}
	return "unknown";
}

inline bool lastSocketCallTimedOut() {
#ifdef _WIN32
	return WSAGetLastError() == WSAETIMEDOUT;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

inline MV::Socket connectLoopback(uint16_t a_port, std::chrono::milliseconds a_receiveTimeout = std::chrono::seconds(10)) {
	auto address = *MV::SocketAddress::parse("127.0.0.1", a_port);
	std::error_code error;
	auto result = MV::Socket::tcp(address, error);
	if (!error) {
		error = result.connect(address);
	}
	if (error) {
		throw std::system_error(error, "connecting to 127.0.0.1:" + std::to_string(a_port));
	}
	result.receiveTimeout(a_receiveTimeout);
	return result;
}

// Listens on loopback; accept() blocks until a peer connects or the listener closes.
class LoopbackListener {
public:
	LoopbackListener() {
		auto address = *MV::SocketAddress::parse("127.0.0.1", 0);
		std::error_code error;
		socket = MV::Socket::tcp(address, error);
		if (!error) {
			error = socket.bind(address);
		}
		if (!error) {
			error = socket.listen();
		}
		if (error) {
			throw std::system_error(error, "listening on loopback");
		}
		ourPort = socket.localAddress()->port();
	}

	uint16_t port() const {
		return ourPort;
	}

	MV::Socket accept(std::chrono::milliseconds a_receiveTimeout = std::chrono::seconds(5)) {
		std::error_code error;
		auto result = socket.accept(error);
		if (result.valid()) {
			result.receiveTimeout(a_receiveTimeout);
		}
		return result;
	}

	void close() {
		socket.close();
	}

private:
	MV::Socket socket;
	uint16_t ourPort = 0;
};

inline bool writeAll(MV::Socket &a_socket, std::string_view a_bytes) {
	return !a_socket.sendAll(a_bytes.data(), a_bytes.size());
}

// Reads until a_buffer holds a_delimiter, then moves everything through it into a_line.
inline PeerRead readUntil(MV::Socket &a_socket, std::string &a_buffer, const std::string &a_delimiter, std::string &a_line) {
	while (a_buffer.find(a_delimiter) == std::string::npos) {
		char chunk[4096];
		std::error_code error;
		auto received = a_socket.receive(chunk, sizeof(chunk), error);
		if (error) {
			return lastSocketCallTimedOut() ? PeerRead::TIMED_OUT : PeerRead::CLOSED;
		}
		if (received == 0) {
			return PeerRead::CLOSED;
		}
		a_buffer.append(chunk, received);
	}
	auto end = a_buffer.find(a_delimiter) + a_delimiter.size();
	a_line = a_buffer.substr(0, end);
	a_buffer.erase(0, end);
	return PeerRead::OK;
}

// Blocks until the peer closes the socket (or the receive timeout passes).
inline PeerRead waitUntilClosed(MV::Socket &a_socket) {
	char chunk[256];
	while (true) {
		std::error_code error;
		auto received = a_socket.receive(chunk, sizeof(chunk), error);
		if (error) {
			return lastSocketCallTimedOut() ? PeerRead::TIMED_OUT : PeerRead::CLOSED;
		}
		if (received == 0) {
			return PeerRead::CLOSED;
		}
	}
}

// Blocking TLS over a connected socket, client or server by the context.
class BlockingTls {
public:
	BlockingTls(MV::Socket a_socket, std::shared_ptr<MV::TlsContext> a_context) :
		ourSocket(std::move(a_socket)),
		context(std::move(a_context)),
		session(SSL_new(context->native())) {

		SSL_set_fd(session, static_cast<int>(ourSocket.handle()));
	}

	~BlockingTls() {
		SSL_free(session);
	}

	BlockingTls(const BlockingTls&) = delete;
	BlockingTls& operator=(const BlockingTls&) = delete;

	ssl_st* ssl() const {
		return session;
	}

	MV::Socket& socket() {
		return ourSocket;
	}

	// The OpenSSL reason a failed handshake gives.
	const std::string& error() const {
		return ourError;
	}

	bool handshake() {
		ERR_clear_error();
		auto result = context->server() ? SSL_accept(session) : SSL_connect(session);
		if (result != 1) {
			auto packed = ERR_get_error();
			ourError = packed ? std::error_code(static_cast<int>(packed), MV::tlsCategory()).message() : std::string("the peer closed the connection");
			ERR_clear_error();
			return false;
		}
		return true;
	}

	bool write(std::string_view a_bytes) {
		while (!a_bytes.empty()) {
			ERR_clear_error();
			auto written = SSL_write(session, a_bytes.data(), static_cast<int>(a_bytes.size()));
			if (written <= 0) {
				ERR_clear_error();
				return false;
			}
			a_bytes.remove_prefix(static_cast<size_t>(written));
		}
		return true;
	}

	PeerRead read(char *a_bytes, size_t a_size, size_t &a_received) {
		ERR_clear_error();
		auto result = SSL_read(session, a_bytes, static_cast<int>(a_size));
		if (result > 0) {
			a_received = static_cast<size_t>(result);
			return PeerRead::OK;
		}
		a_received = 0;
		auto code = SSL_get_error(session, result);
		auto timedOut = code == SSL_ERROR_SYSCALL && lastSocketCallTimedOut();
		auto reason = ERR_GET_REASON(ERR_peek_error());
		ERR_clear_error();
		if (timedOut) {
			return PeerRead::TIMED_OUT;
		}
		if (code == SSL_ERROR_ZERO_RETURN || code == SSL_ERROR_SYSCALL || (code == SSL_ERROR_SSL && reason == SSL_R_UNEXPECTED_EOF_WHILE_READING)) {
			return PeerRead::CLOSED;
		}
		return PeerRead::FAILED;
	}

	PeerRead readExactly(char *a_bytes, size_t a_size) {
		while (a_size > 0) {
			size_t received = 0;
			auto result = read(a_bytes, a_size, received);
			if (result != PeerRead::OK) {
				return result;
			}
			a_bytes += received;
			a_size -= received;
		}
		return PeerRead::OK;
	}

	PeerRead readUntil(std::string &a_buffer, const std::string &a_delimiter, std::string &a_line) {
		while (a_buffer.find(a_delimiter) == std::string::npos) {
			char chunk[4096];
			size_t received = 0;
			auto result = read(chunk, sizeof(chunk), received);
			if (result != PeerRead::OK) {
				return result;
			}
			a_buffer.append(chunk, received);
		}
		auto end = a_buffer.find(a_delimiter) + a_delimiter.size();
		a_line = a_buffer.substr(0, end);
		a_buffer.erase(0, end);
		return PeerRead::OK;
	}

	// Sends close_notify and waits for the peer's.
	void shutdown() {
		ERR_clear_error();
		if (SSL_shutdown(session) == 0) {
			SSL_shutdown(session);
		}
		ERR_clear_error();
	}

private:
	MV::Socket ourSocket;
	std::shared_ptr<MV::TlsContext> context;
	ssl_st *session;
	std::string ourError;
};

}
