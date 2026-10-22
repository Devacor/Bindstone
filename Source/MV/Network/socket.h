#ifndef _MV_SOCKET_H_
#define _MV_SOCKET_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// A thin layer over the platform's sockets (Winsock, BSD sockets); no platform header leaks out of socket.cpp.
namespace MV {
#ifdef _WIN32
	using SocketHandle = std::uintptr_t;
	inline constexpr SocketHandle INVALID_SOCKET_HANDLE = ~SocketHandle {0};
#else
	using SocketHandle = int;
	inline constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

	// An IPv4 or IPv6 address and port.
	class SocketAddress {
	public:
		// a_address is a literal ("0.0.0.0", "127.0.0.1", "::1", "[::1]"); names go through resolveAddresses.
		static std::optional<SocketAddress> parse(const std::string &a_address, uint16_t a_port = 0);
		// 4 bytes for IPv4, 16 for IPv6, in network order (as a certificate's IP address entry stores them).
		static std::optional<SocketAddress> fromBytes(const unsigned char *a_bytes, size_t a_length, uint16_t a_port = 0);

		uint16_t port() const;
		bool ipv6() const;
		bool loopback() const;
		// "127.0.0.1", "::1"
		std::string host() const;
		// "127.0.0.1:80", "[::1]:80"
		std::string to_string() const;

		const void* data() const {
			return storage.data();
		}
		void* data() {
			return storage.data();
		}
		int size() const {
			return length;
		}
		void size(int a_length) {
			length = a_length;
		}
		static constexpr int capacity() {
			return static_cast<int>(sizeof(storage));
		}

		bool operator==(const SocketAddress &a_other) const;
		bool operator!=(const SocketAddress &a_other) const {
			return !(*this == a_other);
		}

	private:
		alignas(8) std::array<unsigned char, 128> storage {};
		int length = 0;
	};

	// Blocks while the platform resolves a_host; a_service is a port number or a service name ("http", "https").
	std::vector<SocketAddress> resolveAddresses(const std::string &a_host, const std::string &a_service, std::error_code &a_error);

	std::string localHostName();

	// What a non-blocking call reports when it would have to wait (including a connect still in progress).
	bool socketWouldBlock(const std::error_code &a_error);

	enum class NetworkError { END_OF_STREAM = 1, STREAM_TRUNCATED, TLS_FAILURE, INVALID_ADDRESS };
	const std::error_category& networkCategory();
	std::error_code make_error_code(NetworkError a_error);

	// Owns one TCP socket and closes it on destruction.
	class Socket {
	public:
		Socket() = default;
		explicit Socket(SocketHandle a_handle) :
			ourHandle(a_handle) {
		}
		Socket(Socket &&a_other) noexcept :
			ourHandle(a_other.release()) {
		}
		Socket& operator=(Socket &&a_other) noexcept;
		Socket(const Socket&) = delete;
		Socket& operator=(const Socket&) = delete;
		~Socket() {
			close();
		}

		// A TCP (or UDP) socket of a_for's family.
		static Socket tcp(const SocketAddress &a_for, std::error_code &a_error);
		static Socket udp(const SocketAddress &a_for, std::error_code &a_error);

		bool valid() const {
			return ourHandle != INVALID_SOCKET_HANDLE;
		}
		SocketHandle handle() const {
			return ourHandle;
		}
		SocketHandle release() {
			auto result = ourHandle;
			ourHandle = INVALID_SOCKET_HANDLE;
			return result;
		}
		// Shuts both directions, then closes.
		void close();

		std::error_code nonBlocking(bool a_nonBlocking);
		// SO_RCVTIMEO: a blocking receive fails after this long.
		std::error_code receiveTimeout(std::chrono::milliseconds a_timeout);

		// A non-blocking socket reports socketWouldBlock until the connection completes; pendingError() then says how it went.
		std::error_code connect(const SocketAddress &a_address);
		// Exclusive on Windows (SO_EXCLUSIVEADDRUSE: no other process binds a specific address over this port), reusable elsewhere.
		std::error_code bind(const SocketAddress &a_address);
		std::error_code listen();
		Socket accept(std::error_code &a_error);

		// Partial results are normal; 0 bytes with no error from receive means the peer closed.
		size_t send(const char *a_bytes, size_t a_size, std::error_code &a_error);
		size_t receive(char *a_bytes, size_t a_size, std::error_code &a_error);
		// Loops until every byte is sent or an error (blocking sockets).
		std::error_code sendAll(const char *a_bytes, size_t a_size);

		void shutdownSend();
		size_t available() const;
		std::error_code pendingError() const;
		std::optional<SocketAddress> localAddress() const;
		std::optional<SocketAddress> peerAddress() const;

	private:
		SocketHandle ourHandle = INVALID_SOCKET_HANDLE;
	};
}

namespace std {
	template <>
	struct is_error_code_enum<MV::NetworkError> : true_type {};
}

#endif
