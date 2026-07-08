// Host-side smoke for the default-on script debugger: makeScriptEngine stands up the DAP
// listener on 127.0.0.1 and a raw client completes the initialize handshake. The full
// breakpoint/step flow is covered by JaiScript's own "Debugger Connector" suite; this
// guards the MV wiring (default-on, port scheme, lastScriptDebugPort reporting).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include "MV/Script/script.h"
#include "MV/Utility/services.hpp"

#include <chrono>
#include <string>
#include <thread>

using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

SOCKET connectClient(int a_port) {
	WSADATA wsa;
	::WSAStartup(MAKEWORD(2, 2), &wsa);
	for (int attempt = 0; attempt < 50; ++attempt) {
		SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s != INVALID_SOCKET) {
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_port = htons(static_cast<u_short>(a_port));
			::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
			if (::connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
				DWORD recvTimeoutMs = 100;
				::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs), sizeof(recvTimeoutMs));
				return s;
			}
			::closesocket(s);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return INVALID_SOCKET;
}

void sendDap(SOCKET s, const std::string& a_json) {
	std::string frame = "Content-Length: " + std::to_string(a_json.size()) + "\r\n\r\n" + a_json;
	size_t sent = 0;
	while (sent < frame.size()) {
		int n = ::send(s, frame.data() + sent, static_cast<int>(frame.size() - sent), 0);
		if (n <= 0) { return; }
		sent += static_cast<size_t>(n);
	}
}

bool waitFor(SOCKET s, std::string& a_buffer, const std::string& a_needle, int a_timeoutMs = 5000) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(a_timeoutMs);
	if (a_buffer.find(a_needle) != std::string::npos) { return true; }
	char chunk[4096];
	while (std::chrono::steady_clock::now() < deadline) {
		int n = ::recv(s, chunk, sizeof(chunk), 0);
		if (n > 0) {
			a_buffer.append(chunk, chunk + n);
			if (a_buffer.find(a_needle) != std::string::npos) { return true; }
		}
	}
	return false;
}

} // namespace

class script_debug_tests : public suite {
public:
	script_debug_tests() : suite("Script Debug") {}

	void forge_tests() override {
		test("engine_listener_attach_handshake", [this]() {
			auto engine = MV::makeScriptEngine(MV::Services::instance());
			int port = MV::lastScriptDebugPort();
			check_gt(port, 0);

			SOCKET client = connectClient(port);
			check_true(client != INVALID_SOCKET, "connect to the debug listener failed");
			if (client == INVALID_SOCKET) { return; }

			std::string buffer;
			sendDap(client, R"({"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"jaiscript"}})");
			check_true(waitFor(client, buffer, "\"command\":\"initialize\""), "no initialize response");

			sendDap(client, R"({"seq":2,"type":"request","command":"attach","arguments":{}})");
			check_true(waitFor(client, buffer, "\"command\":\"attach\""), "no attach response");
			check_true(waitFor(client, buffer, "\"event\":\"initialized\""), "no initialized event");

			// Fire-and-forget like VS Code: the connector drops the client before the
			// disconnect reply flushes (connector.cpp run loop), so don't await it.
			sendDap(client, R"({"seq":3,"type":"request","command":"disconnect"})");
			::closesocket(client);
		});
	}
};

} // namespace jai::foundry::tests

using script_debug_tests = jai::foundry::tests::script_debug_tests;
FOUNDRY_REGISTER(script_debug_tests)
