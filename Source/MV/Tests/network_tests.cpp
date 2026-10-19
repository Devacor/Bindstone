// MV::Server / MV::Client threading and message-limit regression tests, Foundry framework, over TLS.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Network/network.h"
#include "MV/Network/tlsAsio.h"
#include "tls_test_pki.hpp"

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <mutex>
#include <numeric>
#include <thread>

using namespace jai::foundry;

namespace mv_tests {

namespace network_detail {

struct StateRecord {
	std::mutex lock;
	std::thread::id constructedOn;
	std::thread::id messagedOn;
	std::thread::id disconnectedOn;
	std::thread::id destroyedOn;
	int disconnects = 0;
	bool destroyed = false;
	std::vector<std::string> messages;
};

class RecordingState : public MV::ConnectionStateBase {
public:
	RecordingState(const std::shared_ptr<MV::Connection>& a_connection, std::shared_ptr<StateRecord> a_record, bool a_echo) :
		MV::ConnectionStateBase(a_connection),
		record(std::move(a_record)),
		echo(a_echo) {

		std::lock_guard<std::mutex> guard(record->lock);
		record->constructedOn = std::this_thread::get_id();
	}

	~RecordingState() {
		std::lock_guard<std::mutex> guard(record->lock);
		record->destroyedOn = std::this_thread::get_id();
		record->destroyed = true;
	}

	void message(const std::string& a_message) override {
		{
			std::lock_guard<std::mutex> guard(record->lock);
			record->messagedOn = std::this_thread::get_id();
			record->messages.push_back(a_message);
		}
		if (echo) {
			connection()->send(a_message);
		}
	}

protected:
	void disconnectImplementation() override {
		std::lock_guard<std::mutex> guard(record->lock);
		record->disconnectedOn = std::this_thread::get_id();
		++record->disconnects;
	}

private:
	std::shared_ptr<StateRecord> record;
	bool echo;
};

TestPki& networkPki() {
	static TestPki pki("network");
	return pki;
}

const TestPki::Issued& networkServerIdentity() {
	static TestPki::Issued identity = networkPki().localServer();
	return identity;
}

std::shared_ptr<MV::Server> makeServer(std::shared_ptr<StateRecord> a_record, bool a_echo = false, const MV::MessageLimits& a_limits = {}) {
	return std::make_shared<MV::Server>("127.0.0.1", uint16_t{0}, networkPki().serverConfig(networkServerIdentity()), [a_record, a_echo](const std::shared_ptr<MV::Connection>& a_connection) {
		return std::make_unique<RecordingState>(a_connection, a_record, a_echo);
	}, a_limits);
}

size_t peakCommittedBytes() {
	PROCESS_MEMORY_COUNTERS counters {};
	GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
	return counters.PeakPagefileUsage;
}

size_t recordedBytes(StateRecord& a_record) {
	std::lock_guard<std::mutex> guard(a_record.lock);
	return std::accumulate(a_record.messages.begin(), a_record.messages.end(), size_t {0}, [](size_t a_total, const std::string& a_message) {
		return a_total + a_message.size();
	});
}

size_t recordedCount(StateRecord& a_record) {
	std::lock_guard<std::mutex> guard(a_record.lock);
	return a_record.messages.size();
}

const size_t KIB = 1024;
const size_t MIB = 1024 * 1024;

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

// A hand-driven TLS client: speaks the framing directly, so a test can send what MV::Client never would.
struct RawPeer {
	explicit RawPeer(uint16_t a_port, const MV::TlsConfig& a_tls = networkPki().clientConfig()) :
		context(MV::makeTlsClientContext(a_tls)),
		socket(io, *context) {

		socket.lowest_layer().connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), a_port));
		MV::verifyTlsPeerAs(socket, "127.0.0.1");
		socket.handshake(asio::ssl::stream_base::client, handshakeError);
	}

	asio::error_code send(const std::string& a_content) {
		asio::error_code result;
		asio::write(socket, asio::buffer(MV::NetworkMessage::make(a_content)->wire()), result);
		return result;
	}

	asio::error_code sendHeader(uint32_t a_contentSize) {
		asio::error_code result;
		std::array<uint8_t, 4> header {static_cast<uint8_t>(a_contentSize >> 24), static_cast<uint8_t>(a_contentSize >> 16), static_cast<uint8_t>(a_contentSize >> 8), static_cast<uint8_t>(a_contentSize)};
		asio::write(socket, asio::buffer(header), result);
		return result;
	}

	asio::error_code sendRaw(const std::string& a_bytes) {
		asio::error_code result;
		asio::write(socket, asio::buffer(a_bytes), result);
		return result;
	}

	std::string receive(asio::error_code& a_error) {
		MV::NetworkMessage message;
		asio::read(socket, asio::buffer(message.headerBuffer), a_error);
		if (a_error) {
			return {};
		}
		auto header = message.headerValue();
		protocolFrame = (header & MV::PROTOCOL_FRAME_BIT) != 0;
		message.content.resize(header & ~MV::PROTOCOL_FRAME_BIT);
		asio::read(socket, asio::buffer(message.content), a_error);
		return a_error ? std::string() : message.content;
	}

	size_t available() {
		return socket.lowest_layer().available();
	}

	void close() {
		asio::error_code ignored;
		socket.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		socket.lowest_layer().close(ignored);
	}

	asio::io_context io;
	std::shared_ptr<asio::ssl::context> context;
	MV::TlsStream socket;
	asio::error_code handshakeError;
	bool protocolFrame = false;
};

bool closedByPeer(const asio::error_code& a_error) {
	return a_error == asio::error::eof || a_error == asio::ssl::error::stream_truncated || a_error == asio::error::connection_reset || a_error == asio::error::connection_aborted;
}

std::optional<MV::ProtocolFrame> receiveFrame(RawPeer& a_peer) {
	asio::error_code error;
	auto content = a_peer.receive(error);
	if (error || !a_peer.protocolFrame) {
		return std::nullopt;
	}
	return MV::ProtocolFrame::decode(content);
}

void expectRejection(RawPeer& a_peer, MV::ProtocolCode a_code, uint32_t a_value) {
	auto frame = receiveFrame(a_peer);
	check_true(frame.has_value(), "the peer is told why before the close");
	check_eq(static_cast<int>(a_code), static_cast<int>(frame->code), "the rejection's reason code");
	check_eq(a_value, frame->value, "the rejection's limit");
	check_true(!frame->reason.empty(), "the rejection carries a reason");
	asio::error_code error;
	a_peer.receive(error);
	check_true(closedByPeer(error), "then the server closes the connection (" + error.message() + ")");
}

struct ClientRecord {
	std::vector<std::string> received;
	std::vector<std::string> failures;
	std::thread::id messagedOn;
	std::thread::id failedOn;
	size_t receivedAtFailure = 0;
	int initialized = 0;
};

std::shared_ptr<MV::Client> makeClient(uint16_t a_port, std::shared_ptr<ClientRecord> a_record, const MV::MessageLimits& a_limits = {}, const MV::TlsConfig& a_tls = networkPki().clientConfig(), const std::string& a_host = "127.0.0.1") {
	return MV::Client::make(MV::Url{"http://" + a_host + ":" + std::to_string(a_port)}, a_tls, [a_record](const std::string& a_message) {
		a_record->messagedOn = std::this_thread::get_id();
		a_record->received.push_back(a_message);
	}, [a_record](const std::string& a_reason) {
		a_record->failedOn = std::this_thread::get_id();
		a_record->receivedAtFailure = a_record->received.size();
		a_record->failures.push_back(a_reason);
	}, [a_record] {
		++a_record->initialized;
	}, a_limits);
}

// Floods through TLS from its own thread. TLS records cannot be written non-blocking and resumed, so a
// flood the server never drains is ended by closing the socket once the test is done with it.
struct Flooder {
	Flooder(RawPeer& a_peer, size_t a_messageSize, size_t a_count) :
		peer(a_peer) {

		thread = std::thread([this, a_messageSize, a_count] {
			for (size_t i = 0; i < a_count && !stopping; ++i) {
				std::string bytes {static_cast<char>(a_messageSize >> 24), static_cast<char>(a_messageSize >> 16), static_cast<char>(a_messageSize >> 8), static_cast<char>(a_messageSize)};
				bytes.append(a_messageSize, static_cast<char>('a' + i % 26));
				asio::error_code error;
				asio::write(peer.socket, asio::buffer(bytes), error);
				if (error) {
					failed = true;
					break;
				}
				++sent;
			}
			finished = true;
		});
	}

	~Flooder() {
		stopping = true;
		for (int i = 0; i < 3000 && !finished; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (!finished) {
			asio::error_code ignored;
			peer.socket.lowest_layer().close(ignored);
		}
		thread.join();
	}

	RawPeer& peer;
	std::atomic<size_t> sent {0};
	std::atomic<bool> failed {false};
	std::atomic<bool> stopping {false};
	std::atomic<bool> finished {false};
	std::thread thread;
};

}

class network_suite : public suite {
public:
	network_suite() : suite("MV Network") {}

	void forge_tests() override {
		using namespace network_detail;

		test("a connection's state is built, fed and disconnected on the thread that updates the server", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto pump = [&] { server->update(0.01); };
			auto mainThread = std::this_thread::get_id();

			RawPeer peer(server->port());
			peer.send("hello");
			check_true(pumpUntil(pump, [&] { std::lock_guard<std::mutex> guard(record->lock); return record->messages.size() == 1; }), "the message arrives");
			peer.close();
			check_true(pumpUntil(pump, [&] { return server->connections().empty(); }), "the closed connection is dropped");

			std::lock_guard<std::mutex> guard(record->lock);
			check_true(record->constructedOn == mainThread, "the connection state factory runs on the updating thread");
			check_true(record->messagedOn == mainThread, "messages are handled on the updating thread");
			check_true(record->disconnectedOn == mainThread, "a peer's close disconnects the state on the updating thread");
			check_eq(1, record->disconnects);
		});

		test("the update that drops a disconnected connection destroys its state on the updating thread, once", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto pump = [&] { server->update(0.01); };
			auto mainThread = std::this_thread::get_id();

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			server->connections()[0]->disconnect();
			server->update(0.01);
			check_true(server->connections().empty(), "the disconnected connection is dropped");
			bool destroyedByThatUpdate = false;
			{
				std::lock_guard<std::mutex> guard(record->lock);
				destroyedByThatUpdate = record->destroyed;
			}
			check_true(destroyedByThatUpdate, "no network handler keeps the connection (and its game state) alive");
			pumpUntil([] {}, [&] { std::lock_guard<std::mutex> guard(record->lock); return record->destroyed; }, 1.0);

			std::lock_guard<std::mutex> guard(record->lock);
			check_true(record->destroyedOn == mainThread, "the state is destroyed on the updating thread");
			check_eq(1, record->disconnects);
		});

		test("messages sent just before a server-side disconnect reach the peer before the close", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			auto connection = server->connections()[0];
			std::string farewell(64 * 1024, 'k');
			connection->send("Logged in elsewhere.");
			connection->send(farewell);
			connection->disconnect();
			connection.reset();
			server->update(0.01);

			asio::error_code error;
			check_eq(std::string("Logged in elsewhere."), peer.receive(error));
			check_true(peer.receive(error) == farewell, "a large final message arrives whole");
			peer.receive(error);
			check_true(closedByPeer(error), "then the server closes the connection (" + error.message() + ")");
		});

		test("a client whose last owner lets go is destroyed at once, not kept alive by its pending read", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient(server->port(), clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };

			check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
			std::weak_ptr<MV::Client> watcher = client;
			client.reset();
			check_true(watcher.expired(), "dropping the last owner destroys the client");
		});

		test("client callbacks run on the updating thread and a server-side disconnect reaches the client", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, true);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient(server->port(), clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };
			auto mainThread = std::this_thread::get_id();

			check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
			check_eq(1, clientRecord->initialized);
			client->send("ping");
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == 1; }), "the echo arrives");
			check_eq(std::string("ping"), clientRecord->received[0]);
			check_true(clientRecord->messagedOn == mainThread, "the message callback runs on the updating thread");

			server->connections()[0]->disconnect();
			check_true(pumpUntil(pump, [&] { return !clientRecord->failures.empty(); }), "the client learns the server closed it");
			check_true(clientRecord->failedOn == mainThread, "the failure callback runs on the updating thread");
			check_true(client->disconnected());

			client->reconnect();
			check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client reconnects");
			client->send("again");
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == 2; }), "the reconnected client is echoed");
			check_eq(std::string("again"), clientRecord->received[1]);
		});

		test("messages a server sends just before closing reach the client ahead of its failure callback", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			for (int round = 0; round < 10; ++round) {
				auto clientRecord = std::make_shared<ClientRecord>();
				auto client = makeClient(server->port(), clientRecord);
				auto pump = [&] { server->update(0.01); client->update(); };

				check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
				server->connections()[0]->send("Logged in elsewhere.");
				server->connections()[0]->disconnect();
				server->update(0.01);
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				check_true(pumpUntil(pump, [&] { return !clientRecord->failures.empty(); }), "the client learns the server closed it");
				check_eq(static_cast<size_t>(1), clientRecord->receivedAtFailure);
				check_eq(std::string("Logged in elsewhere."), clientRecord->received[0]);
			}
		});

		test("a client that disconnects and reconnects at once keeps the new connection", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, true);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient(server->port(), clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };

			for (int round = 0; round < 20; ++round) {
				check_true(pumpUntil(pump, [&] { return client->connected(); }), "the client connects");
				client->disconnect();
				client->update();
				client->reconnect();
			}
			check_true(pumpUntil(pump, [&] { return client->connected(); }), "the last reconnect connects");
			auto failuresBefore = clientRecord->failures.size();
			client->send("still here");
			check_true(pumpUntil(pump, [&] { return !clientRecord->received.empty(); }), "the new connection carries messages");
			pumpUntil(pump, [] { return false; }, 0.2);
			check_true(client->connected(), "no handler of an earlier socket disconnects the new one");
			check_eq(failuresBefore, clientRecord->failures.size());
		});

		test("messages arrive whole and in order both ways while both ends are busy", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, true);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient(server->port(), clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };

			check_true(pumpUntil(pump, [&] { return client->connected(); }), "the client connects");
			const int total = 2000;
			std::vector<std::string> sent;
			for (int i = 0; i < total; ++i) {
				sent.push_back(std::to_string(i) + ":" + std::string(static_cast<size_t>((i * 37) % 1024), static_cast<char>('a' + i % 26)));
				client->send(sent.back());
				if (i % 50 == 0) {
					pump();
				}
			}
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == static_cast<size_t>(total); }, 20.0), "every echo arrives");
			check_true(clientRecord->received == sent, "echoes arrive whole and in send order");
			std::lock_guard<std::mutex> guard(record->lock);
			check_true(record->messages == sent, "the server receives whole messages in send order");
		});

		test("a peer announcing a 4 GiB message is told MESSAGE_TOO_LARGE and dropped at its header, before anything near that size is allocated", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			auto peakBefore = peakCommittedBytes();
			peer.sendHeader(0xFFFFFFFF);
			check_true(pumpUntil(pump, [&] { return server->connections().empty(); }, 2.0), "the connection is dropped at the header");
			check_true(peakCommittedBytes() - peakBefore < 64 * MIB, "the announced size is never allocated");
			{
				std::lock_guard<std::mutex> guard(record->lock);
				check_eq(1, record->disconnects);
			}
			expectRejection(peer, MV::ProtocolCode::MESSAGE_TOO_LARGE, static_cast<uint32_t>(32 * KIB));
		});

		test("before authentication a message at 32 KiB arrives and one over it is told MESSAGE_TOO_LARGE and dropped", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			check_true(!server->connections()[0]->authenticated());
			check_eq(static_cast<uint32_t>(32 * KIB), server->connections()[0]->maxMessageBytes());
			peer.send(std::string(32 * KIB, 'a'));
			check_true(pumpUntil(pump, [&] { return recordedCount(*record) == 1; }), "a message at the limit arrives");
			peer.send(std::string(32 * KIB + 1, 'b'));
			check_true(pumpUntil(pump, [&] { return server->connections().empty(); }, 2.0), "a message over the limit drops the connection");
			check_eq(static_cast<size_t>(1), recordedCount(*record));
			expectRejection(peer, MV::ProtocolCode::MESSAGE_TOO_LARGE, static_cast<uint32_t>(32 * KIB));
		});

		test("an authenticated connection announces its limit once, accepts a 1 MiB message and rejects one over 2 MiB", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			auto connection = server->connections()[0];
			connection->authenticate();
			connection->authenticate();
			check_true(connection->authenticated());
			check_eq(static_cast<uint32_t>(2 * MIB), connection->maxMessageBytes());
			connection.reset();
			auto announcement = receiveFrame(peer);
			check_true(announcement.has_value(), "the first thing the peer reads is a protocol frame");
			check_true(announcement->code == MV::ProtocolCode::MESSAGE_LIMIT, "announcing the message limit");
			check_eq(static_cast<uint32_t>(2 * MIB), announcement->value);

			std::string oneMebibyte(MIB, 'm');
			peer.send(oneMebibyte);
			check_true(pumpUntil(pump, [&] { return recordedCount(*record) == 1; }), "a 1 MiB message arrives");
			{
				std::lock_guard<std::mutex> guard(record->lock);
				check_true(record->messages[0] == oneMebibyte, "the 1 MiB message arrives whole");
			}
			peer.send(std::string(2 * MIB + 1, 'x'));
			check_true(pumpUntil(pump, [&] { return server->connections().empty(); }, 2.0), "a message over 2 MiB drops the connection");
			check_eq(static_cast<size_t>(1), recordedCount(*record));
			expectRejection(peer, MV::ProtocolCode::MESSAGE_TOO_LARGE, static_cast<uint32_t>(2 * MIB));
		});

		test("an unauthenticated flood is not read past the 512 KiB unread cap until the inbox drains, then is read again, and the pause tells the peer nothing", [this]() {
			MV::MessageLimits limits;
			limits.unreadTimeout = std::chrono::seconds(10);
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, false, limits);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			const size_t messageSize = 16 * KIB;
			const size_t total = 512;
			Flooder flood(peer, messageSize, total);
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			server->update(0.01);
			auto firstDrain = recordedBytes(*record);
			check_true(firstDrain <= limits.unauthenticatedMaxUnreadBytes + messageSize, "the inbox stops growing at the cap (held " + std::to_string(firstDrain) + " bytes)");
			check_true(firstDrain >= limits.unauthenticatedMaxUnreadBytes / 2, "the inbox filled toward the cap (held " + std::to_string(firstDrain) + " bytes)");
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			server->update(0.01);
			auto secondDrain = recordedBytes(*record) - firstDrain;
			check_true(secondDrain <= limits.unauthenticatedMaxUnreadBytes + messageSize, "reading resumed after the drain and paused at the cap again (held " + std::to_string(secondDrain) + " bytes)");
			check_true(secondDrain > 0, "reading resumed after the drain");
			check_true(pumpUntil(pump, [&] { return recordedCount(*record) == total; }, 20.0), "every message arrives once the inbox keeps draining");
			check_true(server->connections().size() == 1, "the paused connection was never dropped");
			check_eq(static_cast<size_t>(0), peer.available(), "backpressure sends the peer nothing");
			std::lock_guard<std::mutex> guard(record->lock);
			for (size_t i = 0; i < total; ++i) {
				check_true(record->messages[i] == std::string(messageSize, static_cast<char>('a' + i % 26)), "messages arrive whole and in order");
			}
		});

		test("an authenticated flood is not read past the 8 MiB unread cap", [this]() {
			MV::MessageLimits limits;
			limits.unreadTimeout = std::chrono::seconds(10);
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, false, limits);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			server->connections()[0]->authenticate();
			check_true(receiveFrame(peer).has_value(), "the limit is announced");
			const size_t messageSize = 64 * KIB;
			const size_t total = 384;
			Flooder flood(peer, messageSize, total);
			std::this_thread::sleep_for(std::chrono::milliseconds(800));
			server->update(0.01);
			auto firstDrain = recordedBytes(*record);
			check_true(firstDrain <= limits.authenticatedMaxUnreadBytes + messageSize, "the inbox stops growing at the cap (held " + std::to_string(firstDrain) + " bytes)");
			check_true(firstDrain > limits.unauthenticatedMaxUnreadBytes + messageSize, "authentication raised the cap (held " + std::to_string(firstDrain) + " bytes)");
			check_true(pumpUntil(pump, [&] { return recordedCount(*record) == total; }, 20.0), "every message arrives once the inbox keeps draining");
			check_eq(static_cast<size_t>(0), peer.available(), "backpressure sends the peer nothing");
		});

		test("a message that trickles in slower than the unread timeout is told MESSAGE_TIMEOUT and dropped, while idle time between messages is not", [this]() {
			MV::MessageLimits limits;
			limits.unreadTimeout = std::chrono::milliseconds(200);
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, false, limits);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			for (size_t i = 1; i <= 3; ++i) {
				peer.send("ping");
				check_true(pumpUntil(pump, [&] { return recordedCount(*record) == i; }), "a whole message arrives");
				pumpUntil(pump, [] { return false; }, 0.3);
			}
			check_eq(static_cast<size_t>(1), server->connections().size());

			peer.sendHeader(100);
			auto start = std::chrono::steady_clock::now();
			bool dropped = false;
			for (int i = 0; i < 100 && !dropped; ++i) {
				peer.sendRaw("x");
				dropped = pumpUntil(pump, [&] { return server->connections().empty(); }, 0.05);
			}
			auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			check_true(dropped, "the trickling message drops the connection");
			check_true(elapsed < 1.5, "at its deadline (" + std::to_string(elapsed) + " s)");
			check_eq(static_cast<size_t>(3), recordedCount(*record));
			expectRejection(peer, MV::ProtocolCode::MESSAGE_TIMEOUT, 200);
		});

		test("a paused inbox that is not drained within the unread timeout is told INBOX_TIMEOUT and dropped", [this]() {
			MV::MessageLimits limits;
			limits.unreadTimeout = std::chrono::milliseconds(200);
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, false, limits);
			auto pump = [&] { server->update(0.01); };

			RawPeer peer(server->port());
			check_true(pumpUntil(pump, [&] { return server->connections().size() == 1; }), "the connection is adopted");
			{
				Flooder flood(peer, 16 * KIB, 512);
				std::this_thread::sleep_for(std::chrono::milliseconds(800));
				check_true(pumpUntil(pump, [&] { return server->connections().empty(); }, 0.5), "the stalled connection is dropped");
				check_true(recordedBytes(*record) <= limits.unauthenticatedMaxUnreadBytes + 16 * KIB, "nothing past the cap was read");
			}
			expectRejection(peer, MV::ProtocolCode::INBOX_TIMEOUT, 200);
		});

		test("a client sends at most the server's announced limit, assuming the unauthenticated one until it hears it, and an oversized send throws", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, true);
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient(server->port(), clientRecord);
			auto pump = [&] { server->update(0.01); client->update(); };
			auto refused = [&](size_t a_size) {
				try {
					client->send(std::string(a_size, 'r'));
				} catch (const MV::MessageTooLargeException& e) {
					return e.size() == a_size && e.limit() == client->serverMaxMessageBytes();
				}
				return false;
			};

			check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
			check_eq(static_cast<uint32_t>(32 * KIB), client->serverMaxMessageBytes());
			check_true(refused(32 * KIB + 1), "a send over the assumed limit throws MessageTooLargeException");
			client->send("small");
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == 1; }), "the small message is echoed");
			check_eq(std::string("small"), clientRecord->received[0]);

			server->connections()[0]->authenticate();
			check_true(pumpUntil(pump, [&] { return client->serverMaxMessageBytes() == 2 * MIB; }), "the client applies the announced limit");
			std::string oneMebibyte(MIB, 'm');
			client->send(oneMebibyte);
			check_true(refused(2 * MIB + 1), "a send over the announced limit throws MessageTooLargeException");
			client->send("after");
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == 3; }), "the messages within the limit are echoed");
			check_true(clientRecord->received[1] == oneMebibyte, "the 1 MiB message is echoed whole");
			check_eq(std::string("after"), clientRecord->received[2]);
			check_true(client->connected() && server->connections().size() == 1 && clientRecord->failures.empty(), "refusing a send keeps the connection");
			{
				std::lock_guard<std::mutex> guard(record->lock);
				check_eq(static_cast<size_t>(3), record->messages.size());
			}

			client->disconnect();
			client->update();
			client->reconnect();
			check_true(pumpUntil(pump, [&] { return client->connected(); }), "the client reconnects");
			check_eq(static_cast<uint32_t>(32 * KIB), client->serverMaxMessageBytes());
		});

		test("a rejected client hears the reason code before its failure callback, which carries the reason", [this]() {
			MV::MessageLimits limits;
			limits.unreadTimeout = std::chrono::milliseconds(200);
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record, false, limits);
			for (auto expected : {MV::ProtocolCode::MESSAGE_TOO_LARGE, MV::ProtocolCode::INBOX_TIMEOUT}) {
				auto clientRecord = std::make_shared<ClientRecord>();
				auto client = makeClient(server->port(), clientRecord);
				std::vector<std::string> events;
				client->onRejected([&](const MV::ProtocolFrame& a_frame) {
					events.push_back("rejected " + std::to_string(static_cast<int>(a_frame.code)) + " " + std::to_string(a_frame.value) + (clientRecord->failures.empty() ? "" : " after the failure"));
				});
				auto pump = [&] { server->update(0.01); client->update(); };

				check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
				if (expected == MV::ProtocolCode::MESSAGE_TOO_LARGE) {
					client->serverMaxMessageBytes(static_cast<uint32_t>(MIB));
					client->send(std::string(64 * KIB, 'x'));
				} else {
					for (int i = 0; i < 64; ++i) {
						client->send(std::string(16 * KIB, 'f'));
					}
				}
				check_true(pumpUntil([&] { client->update(); }, [&] { return !clientRecord->failures.empty(); }, 3.0), "the client fails while the server is not updated");
				auto value = expected == MV::ProtocolCode::MESSAGE_TOO_LARGE ? static_cast<uint32_t>(32 * KIB) : 200u;
				check_eq(static_cast<size_t>(1), events.size());
				check_eq("rejected " + std::to_string(static_cast<int>(expected)) + " " + std::to_string(value), events[0]);
				check_true(client->rejection().has_value() && client->rejection()->code == expected, "the rejection stays readable");
				check_eq(client->rejection()->reason, clientRecord->failures[0]);
				check_true(pumpUntil(pump, [&] { return server->connections().empty(); }), "the server drops the rejected connection");
			}
		});

		test("a client fails its connection on a server message over its own receive limit", [this]() {
			auto record = std::make_shared<StateRecord>();
			auto server = makeServer(record);
			MV::MessageLimits clientLimits;
			clientLimits.authenticatedMaxMessageBytes = 64 * KIB;
			auto clientRecord = std::make_shared<ClientRecord>();
			auto client = makeClient(server->port(), clientRecord, clientLimits);
			auto pump = [&] { server->update(0.01); client->update(); };

			check_true(pumpUntil(pump, [&] { return client->connected() && server->connections().size() == 1; }), "the client connects");
			server->connections()[0]->send(std::string(64 * KIB, 'a'));
			check_true(pumpUntil(pump, [&] { return clientRecord->received.size() == 1; }), "a message at the limit arrives");
			server->connections()[0]->send(std::string(64 * KIB + 1, 'b'));
			check_true(pumpUntil(pump, [&] { return !clientRecord->failures.empty(); }, 2.0), "a message over the limit fails the connection");
			check_true(clientRecord->failures[0].find("byte limit") != std::string::npos, "the failure says why: " + clientRecord->failures[0]);
			check_eq(static_cast<size_t>(1), clientRecord->received.size());
		});
	}
};

}

using network_suite = mv_tests::network_suite;
FOUNDRY_REGISTER(network_suite)
