#pragma once
// A portable jai::smtp_transport over the sockets of http_socket_transport.hpp: one conversation per message (EHLO,
// STARTTLS when asked, AUTH PLAIN or LOGIN, MAIL FROM, RCPT TO, DATA, QUIT), every wait bounded by the request's timeout.
// TLS goes through a tls_layer the host supplies (openssl_tls.hpp is one); without one "starttls" and "tls" fail with a
// clear error. "none" connects only to a host whose every address is loopback. Header-only and opt-in: nothing in the
// library includes it. Name resolution blocks outside the timeout.
#include <jaiscript/stdlib/mail.hpp>
#include <jaiscript/net/http_socket_transport.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jai {
namespace net {

	namespace smtp_wire {

		inline std::string base64(std::string_view a_bytes) {
			static constexpr char k_digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string result;
			for (size_t i = 0; i < a_bytes.size(); i += 3) {
				const size_t left = a_bytes.size() - i;
				const uint32_t group = (uint32_t {static_cast<unsigned char>(a_bytes[i])} << 16) | (left > 1 ? uint32_t {static_cast<unsigned char>(a_bytes[i + 1])} << 8 : 0) |
					(left > 2 ? uint32_t {static_cast<unsigned char>(a_bytes[i + 2])} : 0);
				result += k_digits[(group >> 18) & 63];
				result += k_digits[(group >> 12) & 63];
				result += left > 1 ? k_digits[(group >> 6) & 63] : '=';
				result += left > 2 ? k_digits[group & 63] : '=';
			}
			return result;
		}

		// The DATA section: every line break CRLF (a bare CR or LF too), a line that starts with '.' given a second, then the end mark
		inline std::string data_section(std::string_view a_message) {
			std::string result;
			result.reserve(a_message.size() + a_message.size() / 64 + 8);
			bool line_start = true;
			for (size_t i = 0; i < a_message.size(); ++i) {
				const char c = a_message[i];
				if (c == '\r' || c == '\n') {
					if (c == '\r' && i + 1 < a_message.size() && a_message[i + 1] == '\n') { ++i; }
					result += "\r\n";
					line_start = true;
					continue;
				}
				if (line_start && c == '.') { result += '.'; }
				result += c;
				line_start = false;
			}
			if (!line_start) { result += "\r\n"; }
			result += ".\r\n";
			return result;
		}

		// One reply: its code and each line's text; after EHLO the lines past the first name the extensions
		struct reply {
			int code = 0;
			std::vector<std::string> lines;

			std::string text() const {
				std::string result = std::to_string(code);
				for (const std::string& line : lines) {
					if (!line.empty()) { result += " " + line; }
				}
				return result;
			}

			// true when an extension line starts with the keyword and, given a_parameter, lists it (both case-insensitive; AUTH=
			// is the old spelling of AUTH)
			bool offers(std::string_view a_keyword, std::string_view a_parameter = {}) const {
				for (size_t i = 1; i < lines.size(); ++i) {
					std::string_view line = lines[i];
					if (line.size() < a_keyword.size() || !http_wire::same_name(line.substr(0, a_keyword.size()), a_keyword)) { continue; }
					line.remove_prefix(a_keyword.size());
					if (!line.empty() && line.front() != ' ' && line.front() != '=') { continue; }
					if (a_parameter.empty()) { return true; }
					while (!line.empty()) {
						line.remove_prefix(1);
						const std::string_view word = line.substr(0, line.find(' '));
						if (http_wire::same_name(word, a_parameter)) { return true; }
						line.remove_prefix(word.size());
					}
				}
				return false;
			}
		};

		// Reads replies ("ddd-text" lines, then the final "ddd text") from a stream that STARTTLS may replace once
		class reader {
		public:
			static constexpr size_t k_max_reply = 64 * 1024;

			reader(byte_stream& a_stream, clock::time_point a_deadline) : stream_(&a_stream), deadline_(a_deadline) {}

			// what the server sent in the clear past its STARTTLS answer is dropped, never read as if it were protected
			void secure(byte_stream& a_stream) {
				stream_ = &a_stream;
				buffer_.clear();
				start_ = 0;
			}

			bool read(reply& a_reply, std::string& a_error) {
				a_reply = reply {};
				size_t size = 0;
				while (true) {
					std::string line;
					if (!read_line(line, a_error)) { return false; }
					size += line.size();
					const bool final = line.size() == 3 || (line.size() > 3 && line[3] == ' ');
					const bool coded = line.size() >= 3 && std::all_of(line.begin(), line.begin() + 3, [](char a_c) { return a_c >= '0' && a_c <= '9'; });
					const int code = coded ? (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0') : 0;
					if (!coded || (!final && line[3] != '-') || (!a_reply.lines.empty() && code != a_reply.code) || size > k_max_reply) {
						a_error = "the server's reply is malformed";
						return false;
					}
					a_reply.code = code;
					a_reply.lines.push_back(line.size() > 4 ? line.substr(4) : std::string());
					if (final) { return true; }
				}
			}

		private:
			bool read_line(std::string& a_line, std::string& a_error) {
				while (true) {
					const size_t end = buffer_.find("\r\n", start_);
					if (end != std::string::npos) {
						a_line.assign(buffer_, start_, end - start_);
						start_ = end + 2;
						return true;
					}
					if (buffer_.size() - start_ > k_max_reply) {
						a_error = "the server's reply is malformed";
						return false;
					}
					buffer_.erase(0, start_);
					start_ = 0;
					char chunk[4096];
					const long got = stream_->read_some(chunk, sizeof(chunk), deadline_, a_error);
					if (got < 0) { return false; }
					if (got == 0) {
						a_error = "the connection closed before the server answered";
						return false;
					}
					buffer_.append(chunk, static_cast<size_t>(got));
				}
			}

			byte_stream* stream_;
			clock::time_point deadline_;
			std::string buffer_;
			size_t start_ = 0;
		};

	} // namespace smtp_wire

	class smtp_socket_transport : public smtp_transport {
	public:
		explicit smtp_socket_transport(std::shared_ptr<tls_layer> a_tls = nullptr) : tls_(std::move(a_tls)) {}

		smtp_reply send(const smtp_request& a_request) override {
			smtp_reply result;
			const auto deadline = clock::now() + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(std::max(a_request.timeout_seconds, 0.0)));
			const bool plain = a_request.security == smtp_security::none;
			if (!plain && !tls_) {
				result.error = std::string("security \"") + (a_request.security == smtp_security::tls ? "tls" : "starttls") +
					"\" needs TLS, and this transport has none: the host gives jai::net::smtp_socket_transport a jai::net::tls_layer (openssl_tls.hpp is one)";
				return result;
			}
			const std::string beyond_loopback = plain ? "security \"none\" sends in the clear, so only to a loopback relay, and '" + a_request.host +
				"' resolves beyond loopback: use \"starttls\" or \"tls\"" : std::string();
			socket_stream socket;
			if (!socket.connect(a_request.host, a_request.port, deadline, result.error, beyond_loopback)) { return result; }
			plain_stream clear(socket);
			std::unique_ptr<byte_stream> secured;
			if (a_request.security == smtp_security::tls && !(secured = secure(socket, a_request.host, deadline, result.error))) { return result; }
			conversation talk(secured ? *secured : static_cast<byte_stream&>(clear), deadline, result.error);
			smtp_wire::reply answer;
			const std::string hello = "EHLO " + a_request.hello + "\r\n";
			if (!talk.step({}, "the connection", 220, answer) || !talk.step(hello, "EHLO", 250, answer)) { return result; }
			if (a_request.security == smtp_security::starttls) {
				if (!answer.offers("STARTTLS")) {
					result.error = "the server does not offer STARTTLS: use security \"tls\" if it takes TLS from the first byte (usually port 465)";
					return result;
				}
				if (!talk.step("STARTTLS\r\n", "STARTTLS", 220, answer) || !(secured = secure(socket, a_request.host, deadline, result.error))) { return result; }
				talk.secure(*secured);
				if (!talk.step(hello, "EHLO", 250, answer)) { return result; }
			}
			if (!a_request.username.empty() && !log_in(talk, answer, a_request)) { return result; }
			if (!talk.step("MAIL FROM:<" + a_request.from + ">\r\n", "MAIL FROM", 250, answer)) { return result; }
			for (const std::string& recipient : a_request.recipients) {
				if (!talk.step("RCPT TO:<" + recipient + ">\r\n", "RCPT TO", 250, answer, 251)) { return result; }
			}
			if (!talk.step("DATA\r\n", "DATA", 354, answer) || !talk.step(smtp_wire::data_section(a_request.message), "the message", 250, answer)) { return result; }
			result.reply = answer.text();
			// the message is the server's at its 250: QUIT is a courtesy, never awaited, so a slow goodbye cannot fail or delay a send
			std::string ignored;
			talk.stream->write_all("QUIT\r\n", deadline, ignored);
			return result;
		}

	private:
		// A command out and its reply in; a reply outside the expected codes is an error naming the step, never the command,
		// which for AUTH holds the login
		struct conversation {
			conversation(byte_stream& a_stream, clock::time_point a_deadline, std::string& a_error) : stream(&a_stream), replies(a_stream, a_deadline), deadline(a_deadline), error(a_error) {}

			bool step(std::string_view a_command, const char* a_step, int a_expected, smtp_wire::reply& a_reply, int a_also = 0) {
				if (!a_command.empty() && !stream->write_all(a_command, deadline, error)) { return false; }
				if (!replies.read(a_reply, error)) { return false; }
				if (a_reply.code == a_expected || (a_also != 0 && a_reply.code == a_also)) { return true; }
				error = std::string("the server answered ") + a_step + " with " + a_reply.text();
				return false;
			}

			void secure(byte_stream& a_stream) {
				stream = &a_stream;
				replies.secure(a_stream);
			}

			byte_stream* stream;
			smtp_wire::reader replies;
			clock::time_point deadline;
			std::string& error;
		};

		std::unique_ptr<byte_stream> secure(socket_stream& a_socket, const std::string& a_host, clock::time_point a_deadline, std::string& a_error) {
			std::unique_ptr<byte_stream> result = tls_->secure(a_socket, a_host, a_deadline, a_error);
			if (!result && a_error.empty()) { a_error = "the TLS handshake failed"; }
			return result;
		}

		// AUTH PLAIN when the server offers it (RFC 4616), else AUTH LOGIN
		static bool log_in(conversation& a_talk, smtp_wire::reply& a_answer, const smtp_request& a_request) {
			if (a_answer.offers("AUTH", "PLAIN")) {
				const std::string token = std::string(1, '\0') + a_request.username + std::string(1, '\0') + a_request.password;
				return a_talk.step("AUTH PLAIN " + smtp_wire::base64(token) + "\r\n", "AUTH", 235, a_answer);
			}
			if (a_answer.offers("AUTH", "LOGIN")) {
				return a_talk.step("AUTH LOGIN\r\n", "AUTH", 334, a_answer) && a_talk.step(smtp_wire::base64(a_request.username) + "\r\n", "AUTH", 334, a_answer) &&
					a_talk.step(smtp_wire::base64(a_request.password) + "\r\n", "AUTH", 235, a_answer);
			}
			a_talk.error = "the server offers no login this client speaks (AUTH PLAIN or LOGIN)";
			return false;
		}

		std::shared_ptr<tls_layer> tls_;
	};

} // namespace net
} // namespace jai
