// MV TLS: what every MV::Server / MV::Client link verifies, and the HTTPS and SMTP clients' certificate
// checks, against loopback servers using certificates made at test time (tls_test_pki.hpp).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include "MV/Network/network.h"
#include "MV/Network/download.h"
#include "MV/Network/email.h"
#include "MV/Network/scriptHttp.h"
#include "socket_test_peers.hpp"
#include "tls_test_pki.hpp"

#include <openssl/crypto.h>

#include <chrono>
#include <mutex>
#include <thread>

using namespace jai::foundry;

namespace mv_tests {

namespace tls_detail {

struct Record {
	std::mutex lock;
	std::vector<std::string> messages;
	int states = 0;
};

class EchoState : public MV::ConnectionStateBase {
public:
	EchoState(const std::shared_ptr<MV::Connection>& a_connection, std::shared_ptr<Record> a_record) :
		MV::ConnectionStateBase(a_connection),
		record(std::move(a_record)) {

		std::lock_guard<std::mutex> guard(record->lock);
		++record->states;
	}

	void message(const std::string& a_message) override {
		{
			std::lock_guard<std::mutex> guard(record->lock);
			record->messages.push_back(a_message);
		}
		connection()->send("echo:" + a_message);
	}

private:
	std::shared_ptr<Record> record;
};

std::shared_ptr<MV::Server> makeServer(const MV::TlsConfig& a_tls, std::shared_ptr<Record> a_record, const MV::MessageLimits& a_limits = {}) {
	return std::make_shared<MV::Server>("127.0.0.1", uint16_t{0}, a_tls, [a_record](const std::shared_ptr<MV::Connection>& a_connection) {
		return std::make_unique<EchoState>(a_connection, a_record);
	}, a_limits);
}

size_t messageCount(Record& a_record) {
	std::lock_guard<std::mutex> guard(a_record.lock);
	return a_record.messages.size();
}

int stateCount(Record& a_record) {
	std::lock_guard<std::mutex> guard(a_record.lock);
	return a_record.states;
}

struct ClientRecord {
	std::vector<std::string> received;
	std::vector<std::string> failures;
	bool initialized = false;
};

std::shared_ptr<MV::Client> makeClient(const std::string& a_host, uint16_t a_port, const MV::TlsConfig& a_tls, std::shared_ptr<ClientRecord> a_record, const MV::MessageLimits& a_limits = {}) {
	return MV::Client::make(MV::Url{"http://" + a_host + ":" + std::to_string(a_port)}, a_tls, [a_record](const std::string& a_message) {
		a_record->received.push_back(a_message);
	}, [a_record](const std::string& a_reason) {
		a_record->failures.push_back(a_reason);
	}, [a_record] {
		a_record->initialized = true;
	}, a_limits);
}

template<typename Condition>
bool pumpUntil(const std::function<void()>& a_pump, Condition a_condition, double a_seconds = 5.0) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(a_seconds);
	while (std::chrono::steady_clock::now() < deadline) {
		a_pump();
		if (a_condition()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return false;
}

bool contains(const std::string& a_text, const std::string& a_part) {
	return a_text.find(a_part) != std::string::npos;
}

// The client's connection fails with a reason naming a_expected, and the server never adopts it.
void expectClientRefuses(const std::string& a_host, const MV::TlsConfig& a_serverTls, const MV::TlsConfig& a_clientTls, const std::string& a_expected) {
	auto record = std::make_shared<Record>();
	auto server = makeServer(a_serverTls, record);
	auto clientRecord = std::make_shared<ClientRecord>();
	auto client = makeClient(a_host, server->port(), a_clientTls, clientRecord);
	auto pump = [&] { server->update(0.01); client->update(); };
	check_true(pumpUntil(pump, [&] { return !clientRecord->failures.empty(); }), "the client refuses the connection");
	pumpUntil(pump, [] { return false; }, 0.2);
	check_true(!clientRecord->initialized && clientRecord->received.empty(), "nothing reaches the client");
	check_true(contains(clientRecord->failures[0], a_expected), "the failure says why: " + clientRecord->failures[0]);
	check_eq(0, stateCount(*record), "the server never builds a connection state for it");
}

// A TLS client driven by hand: it can present any certificate (or none) and send after its handshake.
struct RawTlsPeer {
	RawTlsPeer(uint16_t a_port, const MV::TlsConfig& a_tls) :
		tls(connectLoopback(a_port, std::chrono::milliseconds(5000)), MV::makeTlsClientContext(a_tls)) {

		MV::verifyTlsPeerAs(tls.ssl(), "127.0.0.1");
		handshaken = tls.handshake();
	}

	bool send(const std::string& a_content) {
		return tls.write(MV::NetworkMessage::make(a_content)->wire());
	}

	PeerRead readSomething() {
		std::array<char, 64> buffer;
		size_t received = 0;
		return tls.read(buffer.data(), buffer.size(), received);
	}

	BlockingTls tls;
	bool handshaken = false;
};

struct PlainPeer {
	explicit PlainPeer(uint16_t a_port) :
		socket(connectLoopback(a_port, std::chrono::milliseconds(5000))) {
	}

	// Blocks until the server closes the socket.
	PeerRead waitUntilClosed() {
		return mv_tests::waitUntilClosed(socket);
	}

	MV::Socket socket;
};

// Accepts one connection on loopback and runs a_session on it from its own thread.
struct OneShotServer {
	explicit OneShotServer(std::function<void(MV::Socket&)> a_session) :
		port(listener.port()) {

		thread = std::thread([this, a_session] {
			auto socket = listener.accept();
			if (socket.valid()) {
				a_session(socket);
			}
		});
	}

	~OneShotServer() {
		finish();
	}

	// Closing the listener ends an accept that is still waiting; a session already running finishes first.
	void finish() {
		listener.close();
		if (thread.joinable()) {
			thread.join();
		}
	}

	LoopbackListener listener;
	uint16_t port;
	std::thread thread;
};

std::string readLine(MV::Socket& a_socket, std::string& a_buffer) {
	std::string result;
	return mv_tests::readUntil(a_socket, a_buffer, "\r\n", result) == PeerRead::OK ? result : std::string();
}

std::string readLine(BlockingTls& a_stream, std::string& a_buffer, const std::string& a_delimiter = "\r\n") {
	std::string result;
	return a_stream.readUntil(a_buffer, a_delimiter, result) == PeerRead::OK ? result : std::string();
}

struct SmtpLog {
	std::mutex lock;
	bool tlsEstablished = false;
	std::vector<std::string> secureCommands;
	std::string handshakeError;
};

// Plaintext greeting, EHLO and STARTTLS, then the TLS handshake with a_context and the rest of a send.
std::function<void(MV::Socket&)> fakeSmtp(std::shared_ptr<MV::TlsContext> a_context, std::shared_ptr<SmtpLog> a_log) {
	return [a_context, a_log](MV::Socket& a_socket) {
		std::string buffer;
		writeAll(a_socket, "220 fake smtp\r\n");
		readLine(a_socket, buffer);
		writeAll(a_socket, "250 hello\r\n");
		if (!contains(readLine(a_socket, buffer), "STARTTLS")) {
			return;
		}
		writeAll(a_socket, "220 go ahead\r\n");
		BlockingTls stream(std::move(a_socket), a_context);
		if (!stream.handshake()) {
			std::lock_guard<std::mutex> guard(a_log->lock);
			a_log->handshakeError = stream.error();
			return;
		}
		{
			std::lock_guard<std::mutex> guard(a_log->lock);
			a_log->tlsEstablished = true;
		}
		const std::vector<std::string> replies {"250 hello again\r\n", "334 VXNlcm5hbWU6\r\n", "334 UGFzc3dvcmQ6\r\n", "235 ok\r\n", "250 ok\r\n", "250 ok\r\n", "354 go\r\n"};
		for (auto&& reply : replies) {
			auto command = readLine(stream, buffer);
			if (command.empty()) {
				return;
			}
			{
				std::lock_guard<std::mutex> guard(a_log->lock);
				a_log->secureCommands.push_back(command);
			}
			stream.write(reply);
		}
		readLine(stream, buffer, "\r\n.\r\n");
		stream.write("250 queued\r\n");
		readLine(stream, buffer);
		stream.write("221 bye\r\n");
		stream.shutdown();
	};
}

struct HttpsLog {
	std::mutex lock;
	std::string request;
	std::string handshakeError;
};

std::function<void(MV::Socket&)> fakeHttps(std::shared_ptr<MV::TlsContext> a_context, std::shared_ptr<HttpsLog> a_log) {
	return [a_context, a_log](MV::Socket& a_socket) {
		BlockingTls stream(std::move(a_socket), a_context);
		if (!stream.handshake()) {
			std::lock_guard<std::mutex> guard(a_log->lock);
			a_log->handshakeError = stream.error();
			return;
		}
		std::string buffer;
		auto request = readLine(stream, buffer, "\r\n\r\n");
		{
			std::lock_guard<std::mutex> guard(a_log->lock);
			a_log->request = request;
		}
		stream.write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
		stream.shutdown();
	};
}

int recordServerName(SSL* a_ssl, int*, void* a_name) {
	if (auto* name = SSL_get_servername(a_ssl, TLSEXT_NAMETYPE_host_name)) {
		*static_cast<std::string*>(a_name) = name;
	}
	return SSL_TLSEXT_ERR_OK;
}

bool linkedOpenSslHasTls13() {
	return OpenSSL_version_num() >= 0x10101000L;
}

}

class tls_suite : public suite {
public:
	tls_suite() : suite("MV TLS") {}

	void forge_tests() override {
		using namespace tls_detail;

		test("a TLS client and server exchange messages both ways over TLS 1.3 when the linked OpenSSL has it (1.2 otherwise) with an AEAD cipher", [this]() {
			MV::logOpenSslVersion();
			TestPki pki;
			auto record = std::make_shared<Record>();
			auto server = makeServer(pki.serverConfig(pki.localServer()), record);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("127.0.0.1", server->port(), pki.clientConfig(), clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };

			check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
			client->send("ping");
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == 1; }), "the echo arrives");
			check_eq(std::string("echo:ping"), clientRecord->received[0]);
			check_eq(static_cast<size_t>(1), messageCount(*record));
			auto session = server->connections()[0]->tlsSession();
			check_eq(session, client->tlsSession(), "both ends agree on the session");
			auto expectedVersion = linkedOpenSslHasTls13() ? "TLSv1.3" : "TLSv1.2";
			check_true(session.rfind(expectedVersion, 0) == 0, "negotiated " + session + " with " + MV::openSslVersion());
			check_true(contains(session, "GCM") || contains(session, "CHACHA20"), "an AEAD cipher: " + session);
			check_true(!server->connections()[0]->peerCertificate().has_value(), "a server that does not ask for client certificates has none");
		});

		test("a client refuses a server certificate from another CA, even one with the same name", [this]() {
			TestPki trusted("trusted");
			TestPki other("other");
			TestPki impostor("trusted");
			expectClientRefuses("127.0.0.1", other.serverConfig(other.localServer()), trusted.clientConfig(), "certificate verify failed");
			expectClientRefuses("127.0.0.1", impostor.serverConfig(impostor.localServer()), trusted.clientConfig(), "certificate signature failure");
		});

		test("a client refuses a certificate that does not name the host it connects to, by DNS name and by IP address", [this]() {
			TestPki pki;
			expectClientRefuses("localhost", pki.serverConfig(pki.issue("elsewhere", "DNS:other.example,IP:10.1.2.3")), pki.clientConfig(), "ostname mismatch");
			expectClientRefuses("127.0.0.1", pki.serverConfig(pki.issue("name only", "DNS:localhost")), pki.clientConfig(), "IP address mismatch");
			auto record = std::make_shared<Record>();
			auto server = makeServer(pki.serverConfig(pki.localServer()), record);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("localhost", server->port(), pki.clientConfig(), clientRecord);
			check_true(pumpUntil([&] { server->update(0.01); client->update(); }, [&] { return client->connected(); }), "the same CA's certificate for localhost connects to localhost");
		});

		test("a client refuses an expired server certificate and one not yet valid", [this]() {
			TestPki pki;
			expectClientRefuses("127.0.0.1", pki.serverConfig(pki.issue("expired", "DNS:localhost,IP:127.0.0.1", "serverAuth", -10 * 24 * 3600, -24 * 3600)), pki.clientConfig(), "certificate has expired");
			expectClientRefuses("127.0.0.1", pki.serverConfig(pki.issue("future", "DNS:localhost,IP:127.0.0.1", "serverAuth", 24 * 3600, 10 * 24 * 3600)), pki.clientConfig(), "certificate is not yet valid");
		});

		test("a client refuses a server presenting a client certificate", [this]() {
			TestPki pki;
			expectClientRefuses("127.0.0.1", pki.serverConfig(pki.issue("client only", "DNS:localhost,IP:127.0.0.1", "clientAuth")), pki.clientConfig(), "certificate purpose");
		});

		test("a server requiring client certificates refuses a peer with none, one from another CA, and a server certificate, and reads nothing any of them sends", [this]() {
			TestPki pki;
			TestPki other("other");
			auto record = std::make_shared<Record>();
			auto server = makeServer(pki.serverConfig(pki.localServer(), true), record);
			auto pump = [&] { server->update(0.01); };
			std::vector<std::pair<std::string, MV::TlsConfig>> refused {
				{"no certificate", pki.clientConfig()},
				{"another CA", other.clientConfig(other.issue("gameserver:rogue", "DNS:localhost", "clientAuth"))},
				{"a server certificate", pki.clientConfig(pki.localServer("not a client"))},
			};
			for (auto&& [what, tls] : refused) {
				RawTlsPeer peer(server->port(), tls);
				peer.send("RegisterMe");
				auto read = peer.readSomething();
				check_true(read != PeerRead::OK, what + ": the server ends the connection (" + describe(read) + ")");
				pumpUntil(pump, [] { return false; }, 0.2);
				check_eq(0, stateCount(*record), what + ": the server never builds a connection state");
				check_eq(static_cast<size_t>(0), messageCount(*record), what + ": nothing it sent is read");
			}

			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("127.0.0.1", server->port(), pki.clientConfig(), clientRecord);
			check_true(pumpUntil([&] { pump(); client->update(); }, [&] { return !clientRecord->failures.empty(); }), "an MV::Client without a certificate fails");
			check_true(!clientRecord->initialized, "and never reports a connection");

			auto identity = pki.issue("gameserver:alpha", "DNS:alpha.example,IP:127.0.0.1", "clientAuth");
			auto trustedRecord = std::make_shared<ClientRecord>();
			auto trusted = makeClient("127.0.0.1", server->port(), pki.clientConfig(identity), trustedRecord);
			check_true(pumpUntil([&] { pump(); trusted->update(); }, [&] { return trusted->connected() && server->connections().size() == 1; }), "a client certificate from the CA connects");
			auto& certificate = server->connections()[0]->peerCertificate();
			check_true(certificate.has_value(), "the verified certificate is available to the game layer");
			check_eq(std::string("gameserver:alpha"), certificate->commonName);
			check_true(certificate->names("alpha.example") && certificate->names("ALPHA.example") && certificate->names("127.0.0.1"), "it names its DNS name and address");
			check_true(!certificate->names("beta.example") && !certificate->names("127.0.0.2") && !certificate->names("example"), "and nothing else: " + certificate->describe());
		});

		test("a peer speaking the old plaintext protocol to a TLS port is dropped before any game code sees it", [this]() {
			TestPki pki;
			auto record = std::make_shared<Record>();
			auto server = makeServer(pki.serverConfig(pki.localServer()), record);
			std::atomic<bool> pumping {true};
			std::thread pumper([&] {
				while (pumping) {
					server->update(0.01);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			});
			PlainPeer peer(server->port());
			auto plaintext = MV::NetworkMessage::make("LoginRequest password=hunter2");
			writeAll(peer.socket, plaintext->wire());
			auto start = std::chrono::steady_clock::now();
			peer.waitUntilClosed();
			auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			pumping = false;
			pumper.join();
			check_true(seconds < 2.0, "the server closes it at once (" + std::to_string(seconds) + " s)");
			check_eq(0, stateCount(*record));
			check_eq(static_cast<size_t>(0), messageCount(*record));
		});

		test("a peer that connects and never handshakes is dropped at the handshake deadline", [this]() {
			TestPki pki;
			MV::MessageLimits limits;
			limits.handshakeTimeout = std::chrono::milliseconds(200);
			auto record = std::make_shared<Record>();
			auto server = makeServer(pki.serverConfig(pki.localServer()), record, limits);
			auto start = std::chrono::steady_clock::now();
			std::vector<std::unique_ptr<PlainPeer>> silent;
			for (int i = 0; i < 8; ++i) {
				silent.push_back(std::make_unique<PlainPeer>(server->port()));
			}
			std::vector<double> closedAfter;
			for (auto&& peer : silent) {
				peer->waitUntilClosed();
				closedAfter.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
			}
			for (auto&& seconds : closedAfter) {
				check_true(seconds > 0.1 && seconds < 1.5, "each silent peer is closed at its deadline (" + std::to_string(seconds) + " s)");
			}
			server->update(0.01);
			check_eq(0, stateCount(*record), "none became a connection");

			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("127.0.0.1", server->port(), pki.clientConfig(), clientRecord);
			check_true(pumpUntil([&] { server->update(0.01); client->update(); }, [&] { return client->connected(); }), "the server still serves a peer that does handshake");
		});

		test("a client gives up on a server that accepts and never handshakes, at its handshake deadline", [this]() {
			TestPki pki;
			LoopbackListener silent;
			MV::MessageLimits limits;
			limits.handshakeTimeout = std::chrono::milliseconds(200);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("127.0.0.1", silent.port(), pki.clientConfig(), clientRecord, limits);
			auto start = std::chrono::steady_clock::now();
			check_true(pumpUntil([&] { client->update(); }, [&] { return !clientRecord->failures.empty(); }, 3.0), "the client fails");
			auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			check_true(seconds < 1.5, "at its deadline (" + std::to_string(seconds) + " s)");
			check_true(contains(clientRecord->failures[0], "No TLS handshake"), "saying why: " + clientRecord->failures[0]);
		});

		test("a client whose CA file is missing fails each attempt with the reason and connects once the file exists", [this]() {
			TestPki pki;
			auto record = std::make_shared<Record>();
			auto server = makeServer(pki.serverConfig(pki.localServer()), record);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("127.0.0.1", server->port(), MV::TlsConfig {pki.caFile + ".missing"}, clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };
			check_true(pumpUntil(pump, [&] { return !clientRecord->failures.empty(); }), "the attempt fails");
			check_true(contains(clientRecord->failures[0], "make_certs.py"), "pointing at the certificate tool: " + clientRecord->failures[0]);
			std::filesystem::copy_file(pki.caFile, pki.caFile + ".missing");
			client->reconnect();
			check_true(pumpUntil(pump, [&] { return client->connected(); }), "a later attempt loads the file and connects");
		});

		test("the HTTPS download client verifies the certificate names the host and sends SNI", [this]() {
			TestPki pki;
			auto good = MV::makeTlsServerContext(pki.serverConfig(pki.localServer()));
			{
				auto log = std::make_shared<HttpsLog>();
				OneShotServer server(fakeHttps(good, log));
				MV::DownloadOptions options;
				options.trustedCaFile = pki.caFile;
				auto result = MV::downloadBlocking(MV::Url("https://localhost:" + std::to_string(server.port) + "/file.txt"), options);
				check_true(result.ok(), "a certificate for localhost downloads from localhost: " + result.errorMessage);
				check_eq(std::string("hello"), result.response.body);
			}
			{
				auto log = std::make_shared<HttpsLog>();
				std::string serverName;
				auto sni = MV::makeTlsServerContext(pki.serverConfig(pki.localServer()));
				SSL_CTX_set_tlsext_servername_callback(sni->native(), recordServerName);
				SSL_CTX_set_tlsext_servername_arg(sni->native(), &serverName);
				OneShotServer server(fakeHttps(sni, log));
				MV::DownloadOptions options;
				options.trustedCaFile = pki.caFile;
				MV::downloadBlocking(MV::Url("https://localhost:" + std::to_string(server.port) + "/"), options);
				server.finish();
				check_eq(std::string("localhost"), serverName, "the client names the host it wants (SNI)");
			}
			for (auto&& [host, names] : std::vector<std::pair<std::string, std::string>> {{"localhost", "DNS:other.example"}, {"127.0.0.1", "DNS:localhost"}}) {
				auto log = std::make_shared<HttpsLog>();
				OneShotServer server(fakeHttps(MV::makeTlsServerContext(pki.serverConfig(pki.issue("wrong host", names))), log));
				MV::DownloadOptions options;
				options.trustedCaFile = pki.caFile;
				auto result = MV::downloadBlocking(MV::Url("https://" + host + ":" + std::to_string(server.port) + "/secret"), options);
				check_true(result.error == MV::DownloadError::TLS, host + " with a certificate for " + names + " fails its TLS");
				check_true(contains(result.errorMessage, "mismatch"), "on the name: " + result.errorMessage);
				server.finish();
				std::lock_guard<std::mutex> guard(log->lock);
				check_true(log->request.empty(), "and sends no request");
			}
		});

		test("spider:: on the script transport sends any method with its headers and body, keeps every Set-Cookie, follows no redirect itself and trusts only the platform's roots", [this]() {
			auto engine = jai::engine::make();
			jai::stdlib::register_all(*engine);
			jai::stdlib::register_spider(*engine);
			engine->enable(jai::network);
			engine->http_transport(MV::makeScriptHttpTransport());
			auto text = [&](const std::string& a_source) { return engine->execute(a_source).as<std::string>(); };
			{
				std::string request;
				OneShotServer server([&](MV::Socket& a_socket) {
					std::string buffer;
					readUntil(a_socket, buffer, "payload", request);
					writeAll(a_socket, "HTTP/1.1 201 Created\r\nSet-Cookie: a=1\r\nSet-Cookie: b=2\r\nContent-Length: 4\r\nConnection: close\r\n\r\nmade");
				});
				auto answer = text("auto r = spider::post(\"http://127.0.0.1:" + std::to_string(server.port) + "/things?x=1\", \"payload\", {\"method\": \"PUT\", \"headers\": {\"X-Spider\": \"silk\"}}); "
					"`${r.status} ${r.body} ${r.cookies.size()} ${r.cookies[1]}`;");
				server.finish();
				check_eq(std::string("201 made 2 b=2"), answer);
				check_true(contains(request, "PUT /things?x=1 HTTP/1.1\r\n") && contains(request, "X-Spider: silk\r\n") && contains(request, "Content-Type: text/plain; charset=utf-8\r\n")
					&& contains(request, "Content-Length: 7\r\n") && contains(request, "\r\n\r\npayload"), "the request went out as the script spelled it: " + request);
			}
			{
				OneShotServer server([&](MV::Socket& a_socket) {
					std::string buffer;
					std::string head;
					readUntil(a_socket, buffer, "\r\n\r\n", head);
					writeAll(a_socket, "HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
				});
				auto answer = text("auto r = spider::get(\"http://127.0.0.1:" + std::to_string(server.port) + "/\", {\"redirects\": 0}); `${r.status} ${r.headers[\"location\"]}`;");
				server.finish();
				check_eq(std::string("302 /elsewhere"), answer, "the transport answers a redirect; the library's policy follows it");
			}
			{
				TestPki pki;
				OneShotServer server(fakeHttps(MV::makeTlsServerContext(pki.serverConfig(pki.localServer())), std::make_shared<HttpsLog>()));
				auto refused = text("string m = \"\"; try { spider::get(\"https://localhost:" + std::to_string(server.port) + "/\"); } catch (e) { m = e; } m;");
				server.finish();
				check_true(contains(refused, "Handshake Failure") && contains(refused, "certificate verify failed"), "a test CA is not among the platform's roots: " + refused);
			}
		});

		test("the SMTP client verifies the server's certificate after STARTTLS and sends credentials only to the right host", [this]() {
			TestPki pki;
			TestPki other("other");
			MV::Email::Addresses addresses("noreply@bindstone.test", "player@bindstone.test");
			{
				auto log = std::make_shared<SmtpLog>();
				OneShotServer server(fakeSmtp(MV::makeTlsServerContext(pki.serverConfig(pki.localServer())), log));
				auto email = MV::Email::make("localhost", std::to_string(server.port), {"test-user", "test-password"});
				auto result = email->trustedCaFile(pki.caFile)->sendBlocking(addresses, "Welcome", "Hello.");
				server.finish();
				check_true(result.ok(), "a certificate for the SMTP host sends: " + result.errorMessage);
				std::lock_guard<std::mutex> guard(log->lock);
				check_true(log->tlsEstablished && log->secureCommands.size() == 7, "every command after STARTTLS went over TLS");
				check_true(contains(log->secureCommands[1], "AUTH LOGIN"), "including the login");
			}
			std::vector<std::pair<std::string, std::shared_ptr<MV::TlsContext>>> impostors {
				{"a certificate for another host", MV::makeTlsServerContext(pki.serverConfig(pki.issue("wrong host", "DNS:smtp.elsewhere.example")))},
				{"a certificate from another CA", MV::makeTlsServerContext(other.serverConfig(other.localServer()))},
			};
			for (auto&& [what, context] : impostors) {
				auto log = std::make_shared<SmtpLog>();
				OneShotServer server(fakeSmtp(context, log));
				auto email = MV::Email::make("localhost", std::to_string(server.port), {"test-user", "test-password"});
				auto result = email->trustedCaFile(pki.caFile)->sendBlocking(addresses, "Welcome", "Hello.");
				server.finish();
				check_true(result.error == MV::EmailError::TLS, what + ": the send fails its TLS");
				check_true(contains(result.errorMessage, "certificate verify failed"), what + ": saying why: " + result.errorMessage);
				std::lock_guard<std::mutex> guard(log->lock);
				check_true(log->secureCommands.empty(), what + ": no credentials or mail reach it");
			}
		});

		test("PeerCertificate::names matches DNS names case-insensitively and IP addresses by value", [this]() {
			MV::PeerCertificate certificate {"gameserver:local", {"localhost", "Play.Example"}, {"127.0.0.1", "::1"}};
			check_true(certificate.names("LOCALHOST") && certificate.names("play.example"));
			check_true(certificate.names("::1") && certificate.names("[::1]") && certificate.names("0:0:0:0:0:0:0:1") && certificate.names("127.0.0.1"));
			check_true(!certificate.names("127.0.0.2") && !certificate.names("example") && !certificate.names("evil.localhost") && !certificate.names(""));
		});
	}
};

}

using tls_suite = mv_tests::tls_suite;
FOUNDRY_REGISTER(tls_suite)
