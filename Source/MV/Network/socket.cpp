#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "MV/Network/socket.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace MV {

	namespace {
#ifdef _WIN32
		using NativeLength = int;

		struct WinsockSession {
			WinsockSession() {
				WSADATA data;
				if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
					throw std::runtime_error("Winsock failed to start");
				}
			}
			~WinsockSession() {
				WSACleanup();
			}
		};

		void startSockets() {
			static const WinsockSession session;
		}

		std::error_code lastSocketError() {
			return {WSAGetLastError(), std::system_category()};
		}

		SOCKET native(SocketHandle a_handle) {
			return static_cast<SOCKET>(a_handle);
		}

		const int SEND_FLAGS = 0;
#else
		using NativeLength = socklen_t;

		void startSockets() {
		}

		std::error_code lastSocketError() {
			return {errno, std::system_category()};
		}

		int native(SocketHandle a_handle) {
			return a_handle;
		}

#ifdef MSG_NOSIGNAL
		const int SEND_FLAGS = MSG_NOSIGNAL;
#else
		const int SEND_FLAGS = 0;
#endif

		class ResolverCategory : public std::error_category {
		public:
			const char* name() const noexcept override {
				return "resolver";
			}
			std::string message(int a_value) const override {
				return gai_strerror(a_value);
			}
		};

		const std::error_category& resolverCategory() {
			static const ResolverCategory category;
			return category;
		}
#endif

		class NetworkCategory : public std::error_category {
		public:
			const char* name() const noexcept override {
				return "MV network";
			}
			std::string message(int a_value) const override {
				switch (static_cast<NetworkError>(a_value)) {
					case NetworkError::END_OF_STREAM:
						return "End of file";
					case NetworkError::STREAM_TRUNCATED:
						return "stream truncated";
					case NetworkError::TLS_FAILURE:
						return "TLS failure";
					case NetworkError::INVALID_ADDRESS:
						return "Invalid address";
				}
				return "network error";
			}
		};

		const sockaddr* asSockaddr(const SocketAddress &a_address) {
			return static_cast<const sockaddr*>(a_address.data());
		}
	}

	const std::error_category& networkCategory() {
		static const NetworkCategory category;
		return category;
	}

	std::error_code make_error_code(NetworkError a_error) {
		return {static_cast<int>(a_error), networkCategory()};
	}

	bool socketWouldBlock(const std::error_code &a_error) {
		if (a_error.category() != std::system_category()) {
			return false;
		}
#ifdef _WIN32
		return a_error.value() == WSAEWOULDBLOCK || a_error.value() == WSAEINPROGRESS;
#else
		return a_error.value() == EAGAIN || a_error.value() == EWOULDBLOCK || a_error.value() == EINPROGRESS;
#endif
	}

	static_assert(sizeof(sockaddr_storage) <= 128, "SocketAddress holds any sockaddr");

	std::optional<SocketAddress> SocketAddress::parse(const std::string &a_address, uint16_t a_port) {
		startSockets();
		auto text = a_address;
		if (text.size() > 2 && text.front() == '[' && text.back() == ']') {
			text = text.substr(1, text.size() - 2);
		}
		SocketAddress result;
		auto* ipv4 = static_cast<sockaddr_in*>(result.data());
		if (inet_pton(AF_INET, text.c_str(), &ipv4->sin_addr) == 1) {
			ipv4->sin_family = AF_INET;
			ipv4->sin_port = htons(a_port);
			result.length = sizeof(sockaddr_in);
			return result;
		}
		result = {};
		auto* ipv6 = static_cast<sockaddr_in6*>(result.data());
		if (inet_pton(AF_INET6, text.c_str(), &ipv6->sin6_addr) == 1) {
			ipv6->sin6_family = AF_INET6;
			ipv6->sin6_port = htons(a_port);
			result.length = sizeof(sockaddr_in6);
			return result;
		}
		return std::nullopt;
	}

	std::optional<SocketAddress> SocketAddress::fromBytes(const unsigned char *a_bytes, size_t a_length, uint16_t a_port) {
		SocketAddress result;
		if (a_length == 4) {
			auto* ipv4 = static_cast<sockaddr_in*>(result.data());
			ipv4->sin_family = AF_INET;
			ipv4->sin_port = htons(a_port);
			std::memcpy(&ipv4->sin_addr, a_bytes, 4);
			result.length = sizeof(sockaddr_in);
			return result;
		} else if (a_length == 16) {
			auto* ipv6 = static_cast<sockaddr_in6*>(result.data());
			ipv6->sin6_family = AF_INET6;
			ipv6->sin6_port = htons(a_port);
			std::memcpy(&ipv6->sin6_addr, a_bytes, 16);
			result.length = sizeof(sockaddr_in6);
			return result;
		}
		return std::nullopt;
	}

	uint16_t SocketAddress::port() const {
		if (ipv6()) {
			return ntohs(static_cast<const sockaddr_in6*>(data())->sin6_port);
		}
		return length ? ntohs(static_cast<const sockaddr_in*>(data())->sin_port) : uint16_t {0};
	}

	bool SocketAddress::ipv6() const {
		return length && asSockaddr(*this)->sa_family == AF_INET6;
	}

	bool SocketAddress::loopback() const {
		if (!length) {
			return false;
		}
		if (ipv6()) {
			auto& address = static_cast<const sockaddr_in6*>(data())->sin6_addr;
			static const unsigned char LOOPBACK_V6[16] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
			static const unsigned char MAPPED_V4_PREFIX[12] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
			auto* bytes = reinterpret_cast<const unsigned char*>(&address);
			return std::memcmp(bytes, LOOPBACK_V6, 16) == 0 || (std::memcmp(bytes, MAPPED_V4_PREFIX, 12) == 0 && bytes[12] == 127);
		}
		auto* bytes = reinterpret_cast<const unsigned char*>(&static_cast<const sockaddr_in*>(data())->sin_addr);
		return bytes[0] == 127;
	}

	std::string SocketAddress::host() const {
		char text[INET6_ADDRSTRLEN] {};
		if (ipv6()) {
			inet_ntop(AF_INET6, &static_cast<const sockaddr_in6*>(data())->sin6_addr, text, sizeof(text));
		} else if (length) {
			inet_ntop(AF_INET, &static_cast<const sockaddr_in*>(data())->sin_addr, text, sizeof(text));
		}
		return text;
	}

	std::string SocketAddress::to_string() const {
		return (ipv6() ? "[" + host() + "]" : host()) + ":" + std::to_string(port());
	}

	bool SocketAddress::operator==(const SocketAddress &a_other) const {
		return length == a_other.length && std::memcmp(storage.data(), a_other.storage.data(), static_cast<size_t>(length)) == 0;
	}

	std::vector<SocketAddress> resolveAddresses(const std::string &a_host, const std::string &a_service, std::error_code &a_error) {
		startSockets();
		a_error.clear();
		std::vector<SocketAddress> result;
		addrinfo hints {};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		addrinfo* found = nullptr;
		auto status = getaddrinfo(a_host.c_str(), a_service.empty() ? nullptr : a_service.c_str(), &hints, &found);
		if (status != 0) {
#ifdef _WIN32
			a_error = {status, std::system_category()};
#else
			a_error = status == EAI_SYSTEM ? lastSocketError() : std::error_code {status, resolverCategory()};
#endif
			return result;
		}
		for (auto* entry = found; entry; entry = entry->ai_next) {
			if ((entry->ai_family == AF_INET || entry->ai_family == AF_INET6) && entry->ai_addrlen <= static_cast<size_t>(SocketAddress::capacity())) {
				SocketAddress address;
				std::memcpy(address.data(), entry->ai_addr, entry->ai_addrlen);
				address.size(static_cast<int>(entry->ai_addrlen));
				result.push_back(address);
			}
		}
		freeaddrinfo(found);
		return result;
	}

	std::string localHostName() {
		startSockets();
		char name[256] {};
		if (gethostname(name, sizeof(name) - 1) != 0) {
			return {};
		}
		return name;
	}

	Socket& Socket::operator=(Socket &&a_other) noexcept {
		if (this != &a_other) {
			close();
			ourHandle = a_other.release();
		}
		return *this;
	}

	namespace {
		Socket openSocket(const SocketAddress &a_for, bool a_datagram, std::error_code &a_error) {
			startSockets();
			a_error.clear();
			auto handle = ::socket(a_for.ipv6() ? AF_INET6 : AF_INET, a_datagram ? SOCK_DGRAM : SOCK_STREAM, a_datagram ? IPPROTO_UDP : IPPROTO_TCP);
#ifdef _WIN32
			if (handle == INVALID_SOCKET) {
#else
			if (handle < 0) {
#endif
				a_error = lastSocketError();
				return {};
			}
#ifdef SO_NOSIGPIPE
			int noSigPipe = 1;
			setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
			return Socket(static_cast<SocketHandle>(handle));
		}
	}

	Socket Socket::tcp(const SocketAddress &a_for, std::error_code &a_error) {
		return openSocket(a_for, false, a_error);
	}

	Socket Socket::udp(const SocketAddress &a_for, std::error_code &a_error) {
		return openSocket(a_for, true, a_error);
	}

	void Socket::close() {
		if (valid()) {
#ifdef _WIN32
			::shutdown(native(ourHandle), SD_BOTH);
			::closesocket(native(ourHandle));
#else
			::shutdown(native(ourHandle), SHUT_RDWR);
			::close(native(ourHandle));
#endif
			ourHandle = INVALID_SOCKET_HANDLE;
		}
	}

	std::error_code Socket::nonBlocking(bool a_nonBlocking) {
#ifdef _WIN32
		u_long mode = a_nonBlocking ? 1 : 0;
		if (ioctlsocket(native(ourHandle), FIONBIO, &mode) != 0) {
			return lastSocketError();
		}
#else
		auto flags = fcntl(native(ourHandle), F_GETFL, 0);
		if (flags < 0 || fcntl(native(ourHandle), F_SETFL, a_nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) < 0) {
			return lastSocketError();
		}
#endif
		return {};
	}

	std::error_code Socket::receiveTimeout(std::chrono::milliseconds a_timeout) {
#ifdef _WIN32
		DWORD timeout = static_cast<DWORD>(a_timeout.count());
#else
		timeval timeout {static_cast<time_t>(a_timeout.count() / 1000), static_cast<suseconds_t>((a_timeout.count() % 1000) * 1000)};
#endif
		if (setsockopt(native(ourHandle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
			return lastSocketError();
		}
		return {};
	}

	std::error_code Socket::connect(const SocketAddress &a_address) {
		if (::connect(native(ourHandle), asSockaddr(a_address), static_cast<NativeLength>(a_address.size())) != 0) {
			return lastSocketError();
		}
		return {};
	}

	std::error_code Socket::bind(const SocketAddress &a_address) {
		int enabled = 1;
#ifdef _WIN32
		setsockopt(native(ourHandle), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#else
		setsockopt(native(ourHandle), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
		if (::bind(native(ourHandle), asSockaddr(a_address), static_cast<NativeLength>(a_address.size())) != 0) {
			return lastSocketError();
		}
		return {};
	}

	std::error_code Socket::listen() {
		if (::listen(native(ourHandle), SOMAXCONN) != 0) {
			return lastSocketError();
		}
		return {};
	}

	Socket Socket::accept(std::error_code &a_error) {
		a_error.clear();
		auto handle = ::accept(native(ourHandle), nullptr, nullptr);
#ifdef _WIN32
		if (handle == INVALID_SOCKET) {
#else
		if (handle < 0) {
#endif
			a_error = lastSocketError();
			return {};
		}
		return Socket(static_cast<SocketHandle>(handle));
	}

	size_t Socket::send(const char *a_bytes, size_t a_size, std::error_code &a_error) {
		a_error.clear();
		auto chunk = static_cast<int>(std::min<size_t>(a_size, 1 << 30));
		auto sent = ::send(native(ourHandle), a_bytes, chunk, SEND_FLAGS);
		if (sent < 0) {
			a_error = lastSocketError();
			return 0;
		}
		return static_cast<size_t>(sent);
	}

	size_t Socket::receive(char *a_bytes, size_t a_size, std::error_code &a_error) {
		a_error.clear();
		auto chunk = static_cast<int>(std::min<size_t>(a_size, 1 << 30));
		auto received = ::recv(native(ourHandle), a_bytes, chunk, 0);
		if (received < 0) {
			a_error = lastSocketError();
			return 0;
		}
		return static_cast<size_t>(received);
	}

	std::error_code Socket::sendAll(const char *a_bytes, size_t a_size) {
		std::error_code result;
		while (a_size > 0) {
			auto sent = send(a_bytes, a_size, result);
			if (result) {
				return result;
			}
			a_bytes += sent;
			a_size -= sent;
		}
		return result;
	}

	void Socket::shutdownSend() {
		if (valid()) {
#ifdef _WIN32
			::shutdown(native(ourHandle), SD_SEND);
#else
			::shutdown(native(ourHandle), SHUT_WR);
#endif
		}
	}

	size_t Socket::available() const {
#ifdef _WIN32
		u_long result = 0;
		ioctlsocket(native(ourHandle), FIONREAD, &result);
#else
		int result = 0;
		ioctl(native(ourHandle), FIONREAD, &result);
#endif
		return static_cast<size_t>(result);
	}

	std::error_code Socket::pendingError() const {
		int error = 0;
		NativeLength length = sizeof(error);
		if (getsockopt(native(ourHandle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) {
			return lastSocketError();
		}
		return error ? std::error_code {error, std::system_category()} : std::error_code {};
	}

	std::optional<SocketAddress> Socket::localAddress() const {
		SocketAddress result;
		NativeLength length = SocketAddress::capacity();
		if (getsockname(native(ourHandle), static_cast<sockaddr*>(result.data()), &length) != 0) {
			return std::nullopt;
		}
		result.size(static_cast<int>(length));
		return result;
	}

	std::optional<SocketAddress> Socket::peerAddress() const {
		SocketAddress result;
		NativeLength length = SocketAddress::capacity();
		if (getpeername(native(ourHandle), static_cast<sockaddr*>(result.data()), &length) != 0) {
			return std::nullopt;
		}
		result.size(static_cast<int>(length));
		return result;
	}
}
