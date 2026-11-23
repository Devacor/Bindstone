#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/net/smtp_socket_transport.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/select.h>
#endif

namespace jai::foundry::tests {

namespace {

	using native_socket = net::socket_stream::native_type;

	void close_socket(native_socket a_socket) {
#ifdef _WIN32
		::closesocket(a_socket);
#else
		::close(a_socket);
#endif
	}

	// An SMTP server on a loopback port for one conversation: it greets, answers each line by its first word from answers (a
	// missing word gets 502, "" closes without an answer) and keeps the conversation: "S: " for each answer, "C: " for each line
	// the client sent and "D: " for the DATA section before its end mark. An empty greeting is a server that never speaks.
	// split sends each answer in two writes 30 ms apart, so the client reads it in pieces.
	class fake_smtp_server {
	public:
		std::string greeting = "220 relay.test ESMTP";
		std::map<std::string, std::string> answers = {
			{"EHLO", "250-relay.test\r\n250-PIPELINING\r\n250 8BITMIME"}, {"STARTTLS", "220 2.0.0 ready"}, {"AUTH", "235 2.7.0 accepted"},
			{"MAIL", "250 2.1.0 ok"}, {"RCPT", "250 2.1.5 ok"}, {"DATA", "354 go ahead"}, {".", "250 2.0.0 queued as 4F3B2"}, {"QUIT", ""}};
		bool split = false;

		fake_smtp_server() {
#ifdef _WIN32
			WSADATA data;
			::WSAStartup(MAKEWORD(2, 2), &data);
#endif
			listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			socklen_t length = sizeof(address);
			if (::bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(listener_, 1) != 0 ||
				::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
				throw std::runtime_error("the fake SMTP server cannot listen on loopback");
			}
			port_ = ntohs(address.sin_port);
		}
		fake_smtp_server(const fake_smtp_server&) = delete;
		fake_smtp_server& operator=(const fake_smtp_server&) = delete;
		~fake_smtp_server() {
			if (thread_.joinable()) { thread_.join(); }
			close_socket(listener_);
#ifdef _WIN32
			::WSACleanup();
#endif
		}

		int port() const { return port_; }
		void start() { thread_ = std::thread([this] { serve(); }); }

		std::vector<std::string> finish() {
			if (thread_.joinable()) { thread_.join(); }
			return transcript_;
		}

	private:
		void serve() {
			fd_set ready;
			FD_ZERO(&ready);
			FD_SET(listener_, &ready);
			timeval wait {5, 0};
			if (::select(static_cast<int>(listener_) + 1, &ready, nullptr, nullptr, &wait) <= 0) { return; }
			client_ = ::accept(listener_, nullptr, nullptr);
			if (client_ == net::socket_stream::bad_socket) { return; }
#ifdef _WIN32
			DWORD idle = 5000;
#else
			timeval idle {5, 0};
#endif
			::setsockopt(client_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&idle), sizeof(idle));
#ifdef SO_NOSIGPIPE
			int no_sigpipe = 1;
			::setsockopt(client_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
			if (!greeting.empty()) { say(greeting); }
			std::string line;
			while (read_until("\r\n", line)) {
				transcript_.push_back("C: " + line);
				const std::string word = line.substr(0, line.find(' '));
				const auto found = answers.find(word);
				const std::string answer = found == answers.end() ? "502 5.5.1 unrecognized" : found->second;
				if (answer.empty()) { break; }
				say(answer);
				if (word != "DATA" || answer.rfind("354", 0) != 0) { continue; }
				std::string data;
				if (!read_until("\r\n.\r\n", data)) { break; }
				transcript_.push_back("D: " + data + "\r\n");
				if (answers["."].empty()) { break; }
				say(answers["."]);
			}
			close_socket(client_);
		}

		void say(const std::string& a_answer) {
			transcript_.push_back("S: " + a_answer);
			const std::string bytes = a_answer + "\r\n";
			const size_t first = split ? bytes.size() / 2 : bytes.size();
			send_bytes(bytes.substr(0, first));
			if (first < bytes.size()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
				send_bytes(bytes.substr(first));
			}
		}

		void send_bytes(const std::string& a_bytes) {
#ifdef MSG_NOSIGNAL
			::send(client_, a_bytes.data(), static_cast<int>(a_bytes.size()), MSG_NOSIGNAL);
#else
			::send(client_, a_bytes.data(), static_cast<int>(a_bytes.size()), 0);
#endif
		}

		// false when the client closed (or went quiet for 5 s) first
		bool read_until(const std::string& a_mark, std::string& a_out) {
			while (true) {
				const size_t end = buffer_.find(a_mark);
				if (end != std::string::npos) {
					a_out = buffer_.substr(0, end);
					buffer_.erase(0, end + a_mark.size());
					return true;
				}
				char chunk[4096];
				const auto got = ::recv(client_, chunk, sizeof(chunk), 0);
				if (got <= 0) { return false; }
				buffer_.append(chunk, static_cast<size_t>(got));
			}
		}

		native_socket listener_ = net::socket_stream::bad_socket;
		native_socket client_ = net::socket_stream::bad_socket;
		int port_ = 0;
		std::string buffer_;
		std::vector<std::string> transcript_;
		std::thread thread_;
	};

	// TLS that changes nothing: the conversation's STARTTLS and implicit-TLS steps run without OpenSSL, the host each was for kept
	class passthrough_tls : public net::tls_layer {
	public:
		std::vector<std::string> secured;

		std::unique_ptr<net::byte_stream> secure(net::socket_stream& a_socket, const std::string& a_host, net::clock::time_point, std::string&) override {
			secured.push_back(a_host);
			return std::make_unique<net::plain_stream>(a_socket);
		}
	};

	// Keeps every request and answers each with reply
	class recording_transport : public smtp_transport {
	public:
		std::vector<smtp_request> seen;
		smtp_reply reply {"250 2.0.0 ok", ""};

		smtp_reply send(const smtp_request& a_request) override {
			seen.push_back(a_request);
			return reply;
		}
	};

	// The DATA section with what each message draws anew replaced: the Date, the Message-ID's random part and the boundary
	std::string steady(std::string a_data) {
		auto replace_between = [&](const std::string& a_start, const std::string& a_end, const std::string& a_with) {
			const size_t start = a_data.find(a_start);
			if (start == std::string::npos) { return std::string(); }
			const size_t end = a_data.find(a_end, start + a_start.size());
			const std::string drawn = a_data.substr(start + a_start.size(), end - start - a_start.size());
			a_data.replace(start + a_start.size(), end - start - a_start.size(), a_with);
			return drawn;
		};
		const std::string date = replace_between("Date: ", "\r\n", "<date>");
		const std::string id = replace_between("Message-ID: <", "@", "<id>");
		const std::string boundary = replace_between("boundary=\"", "\"", "BOUNDARY");
		for (size_t at = boundary.empty() ? std::string::npos : a_data.find(boundary); at != std::string::npos; at = a_data.find(boundary, at)) { a_data.replace(at, boundary.size(), "BOUNDARY"); }
		const bool date_shaped = date.size() == 31 && date[3] == ',' && date.substr(25) == " +0000";
		const bool id_shaped = id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
		if (!date_shaped || !id_shaped) { return "malformed Date '" + date + "' or Message-ID '" + id + "' in\n" + a_data; }
		return a_data;
	}

	std::string joined(const std::vector<std::string>& a_lines) {
		std::string result;
		for (const std::string& line : a_lines) { result += line + "\n"; }
		return result;
	}

	std::string relay_options(int a_port, const std::string& a_more = "") {
		return "mail::smtp relay = mail::smtp({\"host\": \"127.0.0.1\", \"port\": " + std::to_string(a_port) + ", \"security\": \"none\", \"from\": \"shop@snapjaw.example\"" + a_more + "});\n";
	}

	const std::string k_headers_tail = "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n";

}

class mail_tests : public suite {
public:
	mail_tests() : suite("Mail Client") {}

	std::shared_ptr<engine> mail_engine(std::shared_ptr<smtp_transport> a_transport) {
		auto result = make_engine();
		stdlib::register_all(*result);
		stdlib::register_mail(*result, std::move(a_transport));
		result->enable(jai::permissions::network);
		return result;
	}

	void forge_tests() override {
		auto text = [](const std::shared_ptr<engine>& a_engine, const std::string& a_source) { return a_engine->execute(a_source).as<std::string>(); };
		auto caught = [text](const std::shared_ptr<engine>& a_engine, const std::string& a_source) {
			return text(a_engine, "string m = \"\"; try { " + a_source + " } catch (e) { m = e; } m;");
		};
		auto sockets = [] { return std::make_shared<net::smtp_socket_transport>(); };

		test("a_message_is_the_conversation_a_relay_expects", [this, text, sockets]() {
			fake_smtp_server server;
			server.start();
			auto e = mail_engine(sockets());
			check_eq(std::string("250 2.0.0 queued as 4F3B2"), text(e, relay_options(server.port()) + R"(relay.send({"to": "pup@example.test", "to_name": "Pup", "subject": "Welcome", "text": "Hello.\nBye."});)"));
			const std::vector<std::string> got = server.finish();
			const std::vector<std::string> expected = {"S: 220 relay.test ESMTP", "C: EHLO snapjaw.example", "S: 250-relay.test\r\n250-PIPELINING\r\n250 8BITMIME",
				"C: MAIL FROM:<shop@snapjaw.example>", "S: 250 2.1.0 ok", "C: RCPT TO:<pup@example.test>", "S: 250 2.1.5 ok", "C: DATA", "S: 354 go ahead",
				"D: Date: <date>\r\nFrom: shop@snapjaw.example\r\nTo: \"Pup\" <pup@example.test>\r\nSubject: Welcome\r\nMessage-ID: <<id>@snapjaw.example>\r\n" + k_headers_tail + "Hello.\r\nBye.\r\n",
				"S: 250 2.0.0 queued as 4F3B2", "C: QUIT"};
			std::vector<std::string> shown = got;
			for (std::string& line : shown) {
				if (line.rfind("D: ", 0) == 0) { line = steady(line); }
			}
			check_eq(joined(expected), joined(shown));
		});

		test("a_line_that_starts_with_a_dot_is_stuffed_and_line_breaks_are_crlf", [this, text, sockets]() {
			fake_smtp_server server;
			server.start();
			auto e = mail_engine(sockets());
			text(e, relay_options(server.port()) + R"(relay.send({"to": "pup@example.test", "text": ".hidden\n.\r\nend\rlast"}); "";)");
			const std::vector<std::string> got = server.finish();
			check_eq(size_t {12}, got.size());
			check(got.size() > 9 && got[9].find("\r\n\r\n..hidden\r\n..\r\nend\r\nlast\r\n") != std::string::npos, "dot-stuffed CRLF lines: " + (got.size() > 9 ? got[9] : std::string()));
		});

		test("a_multi_line_reply_split_across_reads_is_one_reply", [this, text, sockets]() {
			fake_smtp_server server;
			server.split = true;
			server.answers["."] = "250-2.0.0 queued\r\n250 2.0.0 as 4F3B2";
			server.start();
			auto e = mail_engine(sockets());
			check_eq(std::string("250 2.0.0 queued 2.0.0 as 4F3B2"), text(e, relay_options(server.port()) + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
			check_eq(size_t {12}, server.finish().size());
		});

		test("a_reply_outside_the_expected_codes_fails_the_send_naming_the_step", [this, caught, sockets]() {
			const std::vector<std::pair<std::string, std::string>> cases = {{"greeting", "554 5.3.2 not now"}, {"EHLO", "421 4.3.2 shutting down"},
				{"MAIL", "553 5.1.8 sender refused"}, {"RCPT", "550 5.1.1 no such user"}, {"DATA", "451 4.3.0 try later"}, {".", "552 5.3.4 too big"}};
			const std::vector<std::string> steps = {"the connection", "EHLO", "MAIL FROM", "RCPT TO", "DATA", "the message"};
			for (size_t i = 0; i < cases.size(); ++i) {
				fake_smtp_server server;
				if (cases[i].first == "greeting") {
					server.greeting = cases[i].second;
				} else {
					server.answers[cases[i].first] = cases[i].second;
				}
				server.start();
				auto e = mail_engine(sockets());
				check_eq("mail::smtp.send: 127.0.0.1:" + std::to_string(server.port()) + ": the server answered " + steps[i] + " with " + cases[i].second,
					caught(e, relay_options(server.port()) + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
			}
		});

		test("a_malformed_reply_and_a_closed_connection_fail_the_send", [this, caught, sockets]() {
			const std::vector<std::pair<std::string, std::string>> cases = {{"hello there", "the server's reply is malformed"},
				{"250-relay.test\r\n251 PIPELINING", "the server's reply is malformed"}, {"", "the connection closed before the server answered"}};
			for (const auto& [answer, reason] : cases) {
				fake_smtp_server server;
				server.answers["EHLO"] = answer;
				server.start();
				auto e = mail_engine(sockets());
				check_eq("mail::smtp.send: 127.0.0.1:" + std::to_string(server.port()) + ": " + reason, caught(e, relay_options(server.port()) + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
			}
		});

		test("a_silent_server_fails_the_send_at_the_timeout", [this, caught, sockets]() {
			fake_smtp_server server;
			server.greeting = "";
			server.start();
			auto e = mail_engine(sockets());
			const auto began = std::chrono::steady_clock::now();
			check_eq("mail::smtp.send: 127.0.0.1:" + std::to_string(server.port()) + ": timed out after 0.25 s",
				caught(e, relay_options(server.port(), ", \"timeout\": 0.25") + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
			check(std::chrono::steady_clock::now() - began < std::chrono::seconds(3), "the timeout bounds the conversation");
			check_eq(size_t {0}, server.finish().size());
		});

		test("security_none_reaches_loopback_only", [this, caught, sockets]() {
			auto e = mail_engine(sockets());
			for (const std::string host : {"192.0.2.1", "2001:db8::1"}) {
				const std::string shown = host.find(':') != std::string::npos ? "[" + host + "]" : host;
				check_eq("mail::smtp.send: " + shown + ":25: security \"none\" sends in the clear, so only to a loopback relay, and '" + host + "' resolves beyond loopback: use \"starttls\" or \"tls\"",
					caught(e, "mail::smtp relay = mail::smtp({\"host\": \"" + host + "\", \"security\": \"none\", \"from\": \"shop@snapjaw.example\"}); relay.send({\"to\": \"pup@example.test\", \"text\": \"hi\"});"));
			}
		});

		test("tls_without_a_tls_layer_names_what_the_host_installs", [this, caught, sockets]() {
			auto e = mail_engine(sockets());
			for (const std::string security : {"starttls", "tls"}) {
				check_eq("mail::smtp.send: 127.0.0.1:" + std::string(security == "tls" ? "465" : "587") + ": security \"" + security +
					"\" needs TLS, and this transport has none: the host gives jai::net::smtp_socket_transport a jai::net::tls_layer (openssl_tls.hpp is one)",
					caught(e, "mail::smtp relay = mail::smtp({\"host\": \"127.0.0.1\", \"security\": \"" + security + "\", \"from\": \"shop@snapjaw.example\"}); relay.send({\"to\": \"pup@example.test\", \"text\": \"hi\"});"));
			}
		});

		test("starttls_is_refused_when_the_server_does_not_offer_it_and_the_login_never_goes_out", [this, caught]() {
			fake_smtp_server server;
			server.answers["EHLO"] = "250-relay.test\r\n250 AUTH PLAIN LOGIN";
			server.start();
			auto tls = std::make_shared<passthrough_tls>();
			auto e = mail_engine(std::make_shared<net::smtp_socket_transport>(tls));
			check_eq("mail::smtp.send: 127.0.0.1:" + std::to_string(server.port()) + ": the server does not offer STARTTLS: use security \"tls\" if it takes TLS from the first byte (usually port 465)",
				caught(e, "mail::smtp relay = mail::smtp({\"host\": \"127.0.0.1\", \"port\": " + std::to_string(server.port()) +
					", \"username\": \"shop\", \"password\": \"hunter2\", \"from\": \"shop@snapjaw.example\"}); relay.send({\"to\": \"pup@example.test\", \"text\": \"hi\"});"));
			check_eq(joined({"S: 220 relay.test ESMTP", "C: EHLO snapjaw.example", "S: 250-relay.test\r\n250 AUTH PLAIN LOGIN"}), joined(server.finish()));
			check_eq(size_t {0}, tls->secured.size());
		});

		test("starttls_secures_the_connection_says_ehlo_again_and_logs_in_with_auth_plain", [this, text]() {
			fake_smtp_server server;
			server.answers["EHLO"] = "250-relay.test\r\n250-STARTTLS\r\n250 AUTH LOGIN PLAIN";
			server.start();
			auto tls = std::make_shared<passthrough_tls>();
			auto e = mail_engine(std::make_shared<net::smtp_socket_transport>(tls));
			text(e, "mail::smtp relay = mail::smtp({\"host\": \"127.0.0.1\", \"port\": " + std::to_string(server.port()) +
				", \"username\": \"shop\", \"password\": \"hunter2\", \"from\": \"shop@snapjaw.example\"}); relay.send({\"to\": \"pup@example.test\", \"text\": \"hi\"}); \"\";");
			std::vector<std::string> got = server.finish();
			got.resize(std::min<size_t>(got.size(), 11));
			check_eq(joined({"S: 220 relay.test ESMTP", "C: EHLO snapjaw.example", "S: 250-relay.test\r\n250-STARTTLS\r\n250 AUTH LOGIN PLAIN", "C: STARTTLS",
				"S: 220 2.0.0 ready", "C: EHLO snapjaw.example", "S: 250-relay.test\r\n250-STARTTLS\r\n250 AUTH LOGIN PLAIN", "C: AUTH PLAIN AHNob3AAaHVudGVyMg==",
				"S: 235 2.7.0 accepted", "C: MAIL FROM:<shop@snapjaw.example>", "S: 250 2.1.0 ok"}), joined(got));
			check_eq(joined({"127.0.0.1"}), joined(tls->secured));
		});

		test("security_tls_secures_before_the_greeting", [this, text]() {
			fake_smtp_server server;
			server.start();
			auto tls = std::make_shared<passthrough_tls>();
			auto e = mail_engine(std::make_shared<net::smtp_socket_transport>(tls));
			check_eq(std::string("250 2.0.0 queued as 4F3B2"), text(e, "mail::smtp relay = mail::smtp({\"host\": \"127.0.0.1\", \"port\": " + std::to_string(server.port()) +
				", \"security\": \"tls\", \"from\": \"shop@snapjaw.example\"}); relay.send({\"to\": \"pup@example.test\", \"text\": \"hi\"});"));
			check_eq(size_t {12}, server.finish().size());
			check_eq(joined({"127.0.0.1"}), joined(tls->secured));
		});

		test("auth_login_when_the_server_offers_no_plain", [this, text, sockets]() {
			for (const std::string offer : {"250 AUTH LOGIN", "250 AUTH=LOGIN"}) {
				fake_smtp_server server;
				server.answers["EHLO"] = "250-relay.test\r\n" + offer;
				server.answers["AUTH"] = "334 VXNlcm5hbWU6";
				server.answers["c2hvcA=="] = "334 UGFzc3dvcmQ6";
				server.answers["aHVudGVyMg=="] = "235 2.7.0 accepted";
				server.start();
				auto e = mail_engine(sockets());
				text(e, relay_options(server.port(), ", \"username\": \"shop\", \"password\": \"hunter2\"") + R"(relay.send({"to": "pup@example.test", "text": "hi"}); "";)");
				std::vector<std::string> got = server.finish();
				got.resize(std::min<size_t>(got.size(), 10));
				check_eq(joined({"S: 220 relay.test ESMTP", "C: EHLO snapjaw.example", "S: 250-relay.test\r\n" + offer, "C: AUTH LOGIN", "S: 334 VXNlcm5hbWU6",
					"C: c2hvcA==", "S: 334 UGFzc3dvcmQ6", "C: aHVudGVyMg==", "S: 235 2.7.0 accepted", "C: MAIL FROM:<shop@snapjaw.example>"}), joined(got));
			}
		});

		test("a_refused_login_never_shows_the_credentials", [this, caught, sockets]() {
			for (const std::string mechanism : {"PLAIN", "LOGIN"}) {
				fake_smtp_server server;
				server.answers["EHLO"] = "250-relay.test\r\n250 AUTH " + mechanism;
				server.answers["AUTH"] = mechanism == "PLAIN" ? "535 5.7.8 authentication failed" : "334 VXNlcm5hbWU6";
				server.answers["c2hvcA=="] = "334 UGFzc3dvcmQ6";
				server.answers["aHVudGVyMg=="] = "535 5.7.8 authentication failed";
				server.start();
				auto e = mail_engine(sockets());
				const std::string error = caught(e, relay_options(server.port(), ", \"username\": \"shop\", \"password\": \"hunter2\"") + R"(relay.send({"to": "pup@example.test", "text": "hi"});)");
				check_eq("mail::smtp.send: 127.0.0.1:" + std::to_string(server.port()) + ": the server answered AUTH with 535 5.7.8 authentication failed", error);
				for (const std::string secret : {"hunter2", "aHVudGVyMg==", "AHNob3AAaHVudGVyMg==", "c2hvcA=="}) { check(error.find(secret) == std::string::npos, "the error holds " + secret); }
			}
			fake_smtp_server server;
			server.start();
			auto e = mail_engine(sockets());
			check_eq("mail::smtp.send: 127.0.0.1:" + std::to_string(server.port()) + ": the server offers no login this client speaks (AUTH PLAIN or LOGIN)",
				caught(e, relay_options(server.port(), ", \"username\": \"shop\", \"password\": \"hunter2\"") + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
		});

		test("every_recipient_gets_a_rcpt_and_bcc_never_reaches_the_headers", [this, text, sockets]() {
			fake_smtp_server server;
			server.start();
			auto e = mail_engine(sockets());
			text(e, relay_options(server.port()) + R"(relay.send({"to": ["a@example.test", "b@example.test"], "cc": "c@example.test", "bcc": ["d@example.test", "e@example.test"], "reply_to": "help@snapjaw.example", "text": "hi"}); "";)");
			const std::vector<std::string> got = server.finish();
			std::vector<std::string> recipients;
			std::string data;
			for (const std::string& line : got) {
				if (line.rfind("C: RCPT TO:", 0) == 0) { recipients.push_back(line.substr(11)); }
				if (line.rfind("D: ", 0) == 0) { data = steady(line); }
			}
			check_eq(joined({"<a@example.test>", "<b@example.test>", "<c@example.test>", "<d@example.test>", "<e@example.test>"}), joined(recipients));
			check_eq("D: Date: <date>\r\nFrom: shop@snapjaw.example\r\nReply-To: help@snapjaw.example\r\nTo: a@example.test,\r\n b@example.test\r\nCc: c@example.test\r\nMessage-ID: <<id>@snapjaw.example>\r\n" +
				k_headers_tail + "hi\r\n", data);
		});

		test("text_and_html_go_as_multipart_alternative_and_html_alone_as_text_html", [this, text]() {
			auto transport = std::make_shared<recording_transport>();
			auto e = mail_engine(transport);
			text(e, relay_options(25) + R"(relay.send({"to": "pup@example.test", "text": "Hi pup", "html": "<p>Hi <b>pup</b></p>"}); relay.send({"to": "pup@example.test", "html": "<p>x=1</p>"}); "";)");
			check_eq(size_t {2}, transport->seen.size());
			const std::string head = "Date: <date>\r\nFrom: shop@snapjaw.example\r\nTo: pup@example.test\r\nMessage-ID: <<id>@snapjaw.example>\r\nMIME-Version: 1.0\r\n";
			check_eq(head + "Content-Type: multipart/alternative; boundary=\"BOUNDARY\"\r\n\r\n--BOUNDARY\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nHi pup\r\n"
				"--BOUNDARY\r\nContent-Type: text/html; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n<p>Hi <b>pup</b></p>\r\n--BOUNDARY--\r\n", steady(transport->seen[0].message));
			check(transport->seen[0].message.find("boundary=\"=_") != std::string::npos, "the boundary starts =_, which quoted-printable never writes");
			check_eq(head + "Content-Type: text/html; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n<p>x=3D1</p>\r\n", steady(transport->seen[1].message));
		});

		test("non_ascii_headers_are_encoded_words_and_utf8_text_is_quoted_printable", [this, text]() {
			auto transport = std::make_shared<recording_transport>();
			auto e = mail_engine(transport);
			text(e, relay_options(25, ", \"from_name\": \"Snap \\\"Jaw\\\" \\\\ Co\"") + R"(relay.send({"to": "pup@example.test", "to_name": "Pupper Ñ", "subject": "Wëlcome to the pack 🐾", "text": "Grüße, pup \nline two"});
				relay.send({"to": "pup@example.test", "subject": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaüb", "text": "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"}); "";)");
			check_eq(size_t {2}, transport->seen.size());
			check_eq("Date: <date>\r\nFrom: \"Snap \\\"Jaw\\\" \\\\ Co\" <shop@snapjaw.example>\r\nTo: =?utf-8?B?UHVwcGVyIMOR?= <pup@example.test>\r\nSubject: =?utf-8?B?V8OrbGNvbWUgdG8gdGhlIHBhY2sg8J+Qvg==?=\r\n"
				"Message-ID: <<id>@snapjaw.example>\r\n" + k_headers_tail + "Gr=C3=BC=C3=9Fe, pup=20\r\nline two\r\n", steady(transport->seen[0].message));
			const std::string second = steady(transport->seen[1].message);
			check(second.find("Subject: =?utf-8?B?YWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWE=?=\r\n =?utf-8?B?w7xi?=\r\n") != std::string::npos, "a long subject folds without splitting a character: " + second);
			check(second.find("\r\n\r\n012345678901234567890123456789012345678901234567890123456789012345678901234=\r\n5678901234567890123456789\r\n") != std::string::npos, "a long line takes a soft break at 76: " + second);
		});

		test("a_line_break_or_nul_in_any_header_field_is_refused_before_anything_is_sent", [this, caught]() {
			auto transport = std::make_shared<recording_transport>();
			auto e = mail_engine(transport);
			const std::string not_address = " holds a value that is not an email address (name@domain in ASCII, without spaces, quotes, brackets, commas or line breaks)";
			const std::string to = R"("to": "pup@example.test", )";
			const std::vector<std::pair<std::string, std::string>> message_cases = {
				{R"("to": "pup@example.test\r\nBcc: x@evil.test")", "'to'" + not_address}, {R"("to": ["pup@example.test", "x@evil.test\n"])", "'to'" + not_address},
				{to + R"("cc": "c@example.test\r\nX: y")", "'cc'" + not_address}, {to + R"("bcc": "d@example.test\n")", "'bcc'" + not_address},
				{to + R"("from": "shop@snapjaw.example\r\nBcc: x@evil.test")", "'from'" + not_address}, {to + R"("reply_to": "help@snapjaw.example\r\n")", "'reply_to'" + not_address},
				{R"("to": "Pup <pup@example.test>")", "'to'" + not_address}, {R"("to": "pup@exämple.test")", "'to'" + not_address},
				{to + R"("to_name": "Pup\r\nBcc: x@evil.test")", "'to_name' holds a line break or NUL"}, {to + R"("from_name": "Shop\nX: y")", "'from_name' holds a line break or NUL"},
				{to + R"("subject": "Hi\r\nBcc: x@evil.test")", "'subject' holds a line break or NUL"}, {to + R"("subject": "Hi\rX: y")", "'subject' holds a line break or NUL"},
				{to + R"("subject": "Hi" + string::from_code(0))", "'subject' holds a line break or NUL"}};
			for (const auto& [fields, reason] : message_cases) {
				check_eq("mail::smtp.send: " + reason, caught(e, relay_options(25) + R"(relay.send({"text": "hi", )" + fields + "});"));
			}
			const std::vector<std::pair<std::string, std::string>> option_cases = {{R"("from": "shop@snapjaw.example\r\nBcc: x@evil.test")", "option 'from'" + not_address},
				{R"("reply_to": "help@snapjaw.example\n")", "option 'reply_to'" + not_address}, {R"("from_name": "Shop\r\nBcc: x@evil.test")", "option 'from_name' holds a line break or NUL"},
				{R"("username": "shop\r\n")", "option 'username' holds a line break or NUL"}, {R"("password": "hunter2\n")", "option 'password' holds a line break or NUL"}};
			for (const auto& [fields, reason] : option_cases) {
				check_eq("mail::smtp: " + reason, caught(e, R"(mail::smtp relay = mail::smtp({"host": "127.0.0.1", )" + fields + "});"));
			}
			check_eq(size_t {0}, transport->seen.size());
		});

		test("options_and_fields_say_what_they_take", [this, caught]() {
			auto transport = std::make_shared<recording_transport>();
			auto e = mail_engine(transport);
			const std::vector<std::pair<std::string, std::string>> option_cases = {
				{R"({"host": "127.0.0.1", "retries": 3})", "unknown option 'retries' (host, port, security, username, password, from, from_name, reply_to, timeout)"},
				{R"({"port": 25})", "the options need 'host'"}, {R"({"host": "mail example"})", "option 'host' takes a host name or address"},
				{R"({"host": "127.0.0.1", "port": 0})", "option 'port' takes an int from 1 to 65535"}, {R"({"host": "127.0.0.1", "security": "ssl"})", "option 'security' takes \"starttls\", \"tls\" or \"none\""},
				{R"({"host": "127.0.0.1", "timeout": 0})", "option 'timeout' takes a number of seconds above 0"}, {R"({"host": "127.0.0.1", "username": 7})", "option 'username' takes text"},
				{R"({"host": "127.0.0.1", "username": "shop"})", "options 'username' and 'password' go together: give both or neither"}};
			for (const auto& [options, reason] : option_cases) { check_eq("mail::smtp: " + reason, caught(e, "mail::smtp relay = mail::smtp(" + options + ");")); }
			const std::vector<std::pair<std::string, std::string>> message_cases = {
				{R"({"to": "pup@example.test", "text": "hi", "attachments": []})", "unknown field 'attachments' (to, to_name, cc, bcc, from, from_name, reply_to, subject, text, html)"},
				{R"({"text": "hi"})", "the message needs 'to'"}, {R"({"to": [], "text": "hi"})", "the message needs 'to'"}, {R"({"to": "pup@example.test", "subject": "hi"})", "the message needs 'text', 'html' or both"},
				{R"({"to": "pup@example.test", "text": 7})", "'text' takes text"},
				{R"({"to": ["a@example.test", "b@example.test"], "to_name": "Pup", "text": "hi"})", "'to_name' names a single 'to': with an array of addresses, leave it out"}};
			for (const auto& [message, reason] : message_cases) { check_eq("mail::smtp.send: " + reason, caught(e, relay_options(25) + "relay.send(" + message + ");")); }
			check_eq(std::string("mail::smtp expects 1 argument (options)"), caught(e, "mail::smtp();"));
			check_eq(std::string("mail::smtp: the options are a map (host, port, security, username, password, from, from_name, reply_to, timeout)"), caught(e, "mail::smtp(7);"));
			check_eq(std::string("mail::smtp.send: the message is a map (to, to_name, cc, bcc, from, from_name, reply_to, subject, text, html)"), caught(e, relay_options(25) + "relay.send(7);"));
			check_eq(std::string("mail::smtp.send: the message needs 'from', in it or in mail::smtp's options"),
				caught(e, R"(mail::smtp relay = mail::smtp({"host": "127.0.0.1"}); relay.send({"to": "pup@example.test", "text": "hi"});)"));
			check_eq(size_t {0}, transport->seen.size());
		});

		test("the_request_carries_the_relay_the_envelope_and_the_defaults", [this, text]() {
			auto transport = std::make_shared<recording_transport>();
			auto e = mail_engine(transport);
			check_eq(std::string("250 2.0.0 ok"), text(e, R"(mail::smtp relay = mail::smtp({"host": "smtp.example.test", "username": "shop", "password": "hunter2", "from": "shop@snapjaw.example"});
				relay.send({"to": "pup@example.test", "from": "orders@snapjaw.example", "bcc": "log@snapjaw.example", "text": "hi"});)"));
			text(e, R"(mail::smtp({"host": "smtp.example.test", "security": "tls", "timeout": 5}).send({"to": "pup@example.test", "from": "a@b.test", "text": "hi"});
				mail::smtp({"host": "localhost", "security": "none", "port": 2525}).send({"to": "pup@example.test", "from": "a@b.test", "text": "hi"}); "";)");
			check_eq(size_t {3}, transport->seen.size());
			const smtp_request& first = transport->seen[0];
			check_eq(std::string("smtp.example.test|587|shop|hunter2|snapjaw.example|orders@snapjaw.example|2|30"), first.host + "|" + std::to_string(first.port) + "|" + first.username + "|" +
				first.password + "|" + first.hello + "|" + first.from + "|" + std::to_string(first.recipients.size()) + "|" + std::to_string(static_cast<int>(first.timeout_seconds)));
			check(first.security == smtp_security::starttls, "starttls is the default");
			check_eq(std::string("log@snapjaw.example"), first.recipients[1]);
			check(first.message.find("From: orders@snapjaw.example\r\n") != std::string::npos && first.message.find("log@") == std::string::npos, "the message's from wins; bcc stays out");
			check(transport->seen[1].security == smtp_security::tls && transport->seen[1].port == 465 && transport->seen[1].timeout_seconds == 5.0, "tls defaults to port 465");
			check(transport->seen[2].security == smtp_security::none && transport->seen[2].port == 2525, "a given port wins");
		});

		test("send_needs_the_network_permission_and_a_transport", [this, caught]() {
			auto transport = std::make_shared<recording_transport>();
			auto e = make_engine();
			stdlib::register_all(*e);
			stdlib::register_mail(*e, transport);
			check_eq(std::string("Insufficient permission: network."), caught(e, relay_options(25) + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
			check_eq(size_t {0}, transport->seen.size());
			auto bare = mail_engine(nullptr);
			check_eq(std::string("mail::smtp.send: no SMTP transport installed: the host passes one to jai::stdlib::register_mail"),
				caught(bare, relay_options(25) + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
			transport->reply = smtp_reply {"", "the relay is on fire"};
			e->enable(jai::permissions::network);
			check_eq(std::string("mail::smtp.send: 127.0.0.1:25: the relay is on fire"), caught(e, relay_options(25) + R"(relay.send({"to": "pup@example.test", "text": "hi"});)"));
		});
	}
};

}

using mail_tests = jai::foundry::tests::mail_tests;
FOUNDRY_REGISTER(mail_tests)
