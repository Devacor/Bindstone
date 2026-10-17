// MV::Server / MV::Client threading regression tests, Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Network/network.h"

#include <chrono>
#include <mutex>
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

std::shared_ptr<MV::Server> makeServer(std::shared_ptr<StateRecord> a_record, bool a_echo = false) {
	return std::make_shared<MV::Server>(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0), [a_record, a_echo](const std::shared_ptr<MV::Connection>& a_connection) {
		return std::make_unique<RecordingState>(a_connection, a_record, a_echo);
	});
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

struct RawPeer {
	explicit RawPeer(uint16_t a_port) {
		socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), a_port));
	}

	void send(const std::string& a_content) {
		MV::NetworkMessage message(a_content);
		asio::write(socket, asio::buffer(message.wire()));
	}

	std::string receive(asio::error_code& a_error) {
		MV::NetworkMessage message;
		asio::read(socket, asio::buffer(message.headerBuffer), a_error);
		if (a_error) {
			return {};
		}
		message.readHeaderFromBuffer();
		asio::read(socket, asio::buffer(message.content), a_error);
		return a_error ? std::string() : message.content;
	}

	void close() {
		asio::error_code ignored;
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		socket.close(ignored);
	}

	asio::io_context io;
	asio::ip::tcp::socket socket {io};
};

struct ClientRecord {
	std::vector<std::string> received;
	std::vector<std::string> failures;
	std::thread::id messagedOn;
	std::thread::id failedOn;
	size_t receivedAtFailure = 0;
	int initialized = 0;
};

std::shared_ptr<MV::Client> makeClient(uint16_t a_port, std::shared_ptr<ClientRecord> a_record) {
	return MV::Client::make(MV::Url{"http://127.0.0.1:" + std::to_string(a_port)}, [a_record](const std::string& a_message) {
		a_record->messagedOn = std::this_thread::get_id();
		a_record->received.push_back(a_message);
	}, [a_record](const std::string& a_reason) {
		a_record->failedOn = std::this_thread::get_id();
		a_record->receivedAtFailure = a_record->received.size();
		a_record->failures.push_back(a_reason);
	}, [a_record] {
		++a_record->initialized;
	});
}

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
			check_true(error == asio::error::eof || error == asio::error::connection_reset, "then the server closes the socket");
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
	}
};

}

using network_suite = mv_tests::network_suite;
FOUNDRY_REGISTER(network_suite)
