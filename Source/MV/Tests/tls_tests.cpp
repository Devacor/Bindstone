// MV TLS: what every MV::Server / MV::Client link verifies, against loopback servers using certificates
// made at test time (tls_test_pki.hpp).

#include <jaiscript/testing/foundry.hpp>
#include "MV/Network/network.h"
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
	return std::make_shared<MV::Server>(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0), a_tls, [a_record](const std::shared_ptr<MV::Connection>& a_connection) {
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
		context(MV::makeTlsClientContext(a_tls)),
		stream(io, *context) {

		stream.lowest_layer().connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), a_port));
		stream.lowest_layer().set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>(5000));
		MV::verifyTlsPeerAs(stream, "127.0.0.1");
		stream.handshake(asio::ssl::stream_base::client, handshakeError);
	}

	asio::error_code send(const std::string& a_content) {
		asio::error_code result;
		asio::write(stream, MV::NetworkMessage::make(a_content)->wire(), result);
		return result;
	}

	asio::error_code readSomething() {
		asio::error_code result;
		std::array<char, 64> buffer;
		stream.read_some(asio::buffer(buffer), result);
		return result;
	}

	asio::io_context io;
	std::shared_ptr<asio::ssl::context> context;
	MV::TlsStream stream;
	asio::error_code handshakeError;
};

struct PlainPeer {
	explicit PlainPeer(uint16_t a_port) {
		socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), a_port));
		socket.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>(5000));
	}

	// Blocks until the server closes the socket.
	void waitUntilClosed() {
		std::array<char, 256> buffer;
		asio::error_code error;
		while (!error) {
			socket.read_some(asio::buffer(buffer), error);
		}
	}

	asio::io_context io;
	asio::ip::tcp::socket socket {io};
};

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
				auto error = peer.readSomething();
				check_true(!!error, what + ": the server ends the connection (" + error.message() + ")");
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
			asio::write(peer.socket, plaintext->wire());
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
			asio::io_context io;
			asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
			asio::ip::tcp::socket held(io);
			acceptor.async_accept(held, [](const asio::error_code&) {});
			MV::MessageLimits limits;
			limits.handshakeTimeout = std::chrono::milliseconds(200);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient("127.0.0.1", acceptor.local_endpoint().port(), pki.clientConfig(), clientRecord, limits);
			auto start = std::chrono::steady_clock::now();
			check_true(pumpUntil([&] { io.poll(); client->update(); }, [&] { return !clientRecord->failures.empty(); }, 3.0), "the client fails");
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
