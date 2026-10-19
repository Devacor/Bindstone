#include "MV/Network/email.h"
#include "MV/Network/networkContextAsio.h"
#include "MV/Network/tlsAsio.h"
#include "MV/Utility/log.h"
#include "MV/Utility/require.hpp"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <optional>
#include <sstream>

namespace MV {

	namespace {
		using tcp = asio::ip::tcp;

		// Every line ends in CRLF, and a line that starts with '.' gets a second one so it cannot end the DATA section early
		std::string smtpBody(const std::string& a_message) {
			std::string result;
			result.reserve(a_message.size() + 16);
			bool lineStart = true;
			for (size_t i = 0; i < a_message.size(); ++i) {
				char c = a_message[i];
				if (c == '\r' && i + 1 < a_message.size() && a_message[i + 1] == '\n') {
					continue;
				}
				if (c == '\n') {
					result += "\r\n";
					lineStart = true;
					continue;
				}
				if (lineStart && c == '.') {
					result += '.';
				}
				result += c;
				lineStart = false;
			}
			return result;
		}

		std::string withoutLineBreaks(std::string a_text) {
			a_text.erase(std::remove_if(a_text.begin(), a_text.end(), [](char c) { return c == '\r' || c == '\n'; }), a_text.end());
			return a_text;
		}
	}

	struct Email::AsioSmtp {
		AsioSmtp(std::shared_ptr<NetworkContext> a_context, const std::string& a_trustedCaFile) :
			sharedContext(std::move(a_context)),
			ioContext(sharedContext->asio().ioContext),
			resolver(ioContext),
			tlsContext(makePublicTlsClientContext(a_trustedCaFile)),
			stream(ioContext, *tlsContext) {
		}

		explicit AsioSmtp(const std::string& a_trustedCaFile) :
			ownedContext(std::in_place),
			ioContext(*ownedContext),
			resolver(ioContext),
			tlsContext(makePublicTlsClientContext(a_trustedCaFile)),
			stream(ioContext, *tlsContext) {
		}

		std::shared_ptr<NetworkContext> sharedContext;
		std::optional<asio::io_context> ownedContext;
		asio::io_context& ioContext;
		tcp::resolver resolver;
		std::shared_ptr<asio::ssl::context> tlsContext;
		TlsStream stream;
		asio::streambuf response;
	};

	Email::Email(const std::string& a_host, const std::string &a_port, const Credentials &a_credentials) :
		credentials(a_credentials),
		host(a_host),
		port(a_port){
	}

	Email::~Email() = default;

	std::shared_ptr<Email> Email::make(const std::string &a_host, const std::string &a_port, const Credentials &a_credentials) {
		return std::shared_ptr<Email>(new Email(a_host, a_port, a_credentials));
	}


	EmailResult Email::sendBlocking(const Addresses &a_addresses, const std::string &a_title, const std::string &a_message) {
		begin(nullptr, a_addresses, a_title, a_message);
		if (asioSmtp) {
			asioSmtp->ownedContext->run();
		}
		return result();
	}

	void Email::send(const std::shared_ptr<NetworkContext> &a_context, const Addresses &a_addresses, const std::string &a_title, const std::string &a_message, CompletionCallback a_onComplete, std::shared_ptr<CallbackQueue> a_callbackQueue) {
		onComplete = std::move(a_onComplete);
		callbackQueue = std::move(a_callbackQueue);
		begin(a_context, a_addresses, a_title, a_message);
	}

	void Email::begin(const std::shared_ptr<NetworkContext> &a_context, const Addresses &a_addresses, const std::string &a_title, const std::string &a_message) {
		{
			std::lock_guard<std::mutex> guard(lock);
			require<LogicException>(!started, "An Email sends one message: make another for [", a_addresses.to, "]");
			started = true;
		}
		addresses = a_addresses;
		try {
			asioSmtp = a_context ? std::make_unique<AsioSmtp>(a_context, ourTrustedCaFile) : std::make_unique<AsioSmtp>(ourTrustedCaFile);
			storeMessageString(a_title, a_message);
			initiateConnection();
		} catch (std::exception& e) {
			fail(EmailError::START, std::string("Email failed to start: ") + e.what());
		} catch (...) {
			fail(EmailError::START, "Email failed to start.");
		}
	}

	void Email::storeMessageString(const std::string &a_title, const std::string &a_message) {
		std::stringstream output;
		output << "Subject: " << withoutLineBreaks(a_title) << "\r\n";
		output << "From: " << (addresses.fromName.empty() ? "" : ("\"" + addresses.fromName + "\"")) << " <" << addresses.from << ">\r\n";
		output << "To: " << (addresses.toName.empty() ? "" : ("\"" + addresses.toName + "\"")) << " <" << addresses.to << ">\r\n";
		output << "\r\n";
		output << smtpBody(a_message);
		output << "\r\n" << "." << "\r\n";
		message = output.str();
	}

	std::string Email::domainFromEmail(const std::string &a_email) {
		auto atLocation = a_email.find('@');
		if (atLocation != std::string::npos && atLocation > 0) {
			return a_email.substr(atLocation + 1);
		}
		return "";
	}

	void Email::initiateConnection() {
		auto self = shared_from_this();
		asioSmtp->resolver.async_resolve(host, port, [this, self](const asio::error_code& a_err, tcp::resolver::results_type a_endpoints) {
			if (a_err) {
				fail(EmailError::RESOLVE, a_err, "Email Resolve Failure");
				return;
			}
			asio::async_connect(asioSmtp->stream.lowest_layer(), a_endpoints, [this, self](const asio::error_code& a_connectErr, const tcp::endpoint&) {
				if (a_connectErr) {
					fail(EmailError::CONNECT, a_connectErr, "Email Connection Failure");
				} else {
					smtpHandshake();
				}
			});
		});
	}

	void Email::smtpHandshake() {
		auto self = shared_from_this();
		readReply({220}, [this, self] {
			command("EHLO " + domainFromEmail(addresses.from) + "\r\n", {250}, [this, self] {
				command("STARTTLS\r\n", {220}, [this, self] {
					try {
						verifyTlsPeerAs(asioSmtp->stream, host);
					} catch (std::exception& e) {
						fail(EmailError::TLS, e.what());
						return;
					}
					asioSmtp->stream.async_handshake(asio::ssl::stream_base::client, [this, self](const asio::error_code& a_err) {
						if (a_err) {
							fail(EmailError::TLS, "TLS handshake with " + host + " failed: " + describeTlsFailure(a_err, asioSmtp->stream));
						} else {
							secureLogin();
						}
					});
				}, false);
			}, false);
		}, false);
	}

	void Email::secureLogin() {
		auto self = shared_from_this();
		command("EHLO " + domainFromEmail(addresses.from) + "\r\n", {250}, [this, self] {
			command("AUTH LOGIN\r\n", {334}, [this, self] {
				command(jai::base64_encode(credentials.name) + "\r\n", {334}, [this, self] {
					command(jai::base64_encode(credentials.password) + "\r\n", {235}, [this, self] {
						sendMessageAndQuit();
					}, true);
				}, true);
			}, true);
		}, true);
	}

	void Email::sendMessageAndQuit() {
		auto self = shared_from_this();
		command("MAIL FROM:<" + addresses.from + ">\r\n", {250}, [this, self] {
			command("RCPT TO:<" + addresses.to + ">\r\n", {250, 251}, [this, self] {
				command("DATA\r\n", {354}, [this, self] {
					command(message, {250}, [this, self] {
						succeed();
						info("Email Complete [", addresses.from, "] -> [", addresses.to, "]");
						command("QUIT\r\n", {}, {}, true);
					}, true);
				}, true);
			}, true);
		}, true);
	}

	void Email::command(const std::string &a_content, std::vector<int> a_validResponses, std::function<void()> a_onReply, bool a_secure) {
		auto self = shared_from_this();
		auto payload = std::make_shared<std::string>(a_content);
		auto onWritten = [this, self, payload, a_validResponses, a_onReply, a_secure](const asio::error_code& a_err, size_t) {
			if (a_err) {
				fail(EmailError::WRITE, a_err, "Email Write Failure");
			} else if (a_onReply) {
				readReply(a_validResponses, a_onReply, a_secure);
			}
		};
		if (a_secure) {
			asio::async_write(asioSmtp->stream, asio::buffer(*payload), onWritten);
		} else {
			asio::async_write(asioSmtp->stream.next_layer(), asio::buffer(*payload), onWritten);
		}
	}

	// A reply is any number of "ddd-text" lines and one final "ddd text" line, possibly split across reads.
	void Email::readReply(std::vector<int> a_validResponses, std::function<void()> a_onReply, bool a_secure) {
		auto self = shared_from_this();
		auto onLines = [this, self, a_validResponses, a_onReply, a_secure](const asio::error_code& a_err, size_t) {
			if (a_err) {
				fail(EmailError::READ, a_err, "Email Read Failure");
				return;
			}
			auto& buffer = asioSmtp->response;
			std::string available(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
			size_t consumed = 0;
			bool final = false;
			for (auto lineEnd = available.find("\r\n"); lineEnd != std::string::npos && !final; lineEnd = available.find("\r\n", consumed)) {
				auto line = available.substr(consumed, lineEnd - consumed);
				consumed = lineEnd + 2;
				info("SMTP: ", line);
				final = line.size() < 4 || line[3] != '-';
				if (final) {
					try {
						pendingReply.status = std::stoi(line.substr(0, 3));
					} catch (...) {
						pendingReply.status = 0;
					}
					pendingReply.message = line.size() > 4 ? line.substr(4) : "";
				} else {
					pendingReply.lines.push_back(line.substr(4));
				}
			}
			buffer.consume(consumed);
			if (!final) {
				readReply(a_validResponses, a_onReply, a_secure);
				return;
			}
			auto reply = std::move(pendingReply);
			pendingReply = {};
			{
				std::lock_guard<std::mutex> guard(lock);
				ourResult.lastReply = reply;
			}
			if (a_validResponses.empty() || std::find(a_validResponses.begin(), a_validResponses.end(), reply.status) != a_validResponses.end()) {
				a_onReply();
			} else {
				fail(EmailError::REJECTED, "SMTP server replied " + std::to_string(reply.status) + " " + reply.message);
			}
		};
		if (a_secure) {
			asio::async_read_until(asioSmtp->stream, asioSmtp->response, "\r\n", onLines);
		} else {
			asio::async_read_until(asioSmtp->stream.next_layer(), asioSmtp->response, "\r\n", onLines);
		}
	}

	void Email::succeed() {
		{
			std::lock_guard<std::mutex> guard(lock);
			if (done) {
				return;
			}
			done = true;
		}
		finish();
	}

	void Email::fail(EmailError a_error, const std::error_code &a_err, const std::string &a_what) {
		fail(a_error, a_what + ": " + a_err.message());
	}

	void Email::fail(EmailError a_error, const std::string &a_reason) {
		{
			std::lock_guard<std::mutex> guard(lock);
			if (done) {
				return;
			}
			done = true;
			ourResult.error = a_error;
			ourResult.errorMessage = a_reason;
		}
		error("Email failed [", addresses.from, "] -> [", addresses.to, "]: ", a_reason);
		finish();
	}

	void Email::finish() {
		if (!onComplete) {
			return;
		}
		auto self = shared_from_this();
		if (callbackQueue) {
			callbackQueue->then([self, callback = onComplete] { callback(self); });
		} else {
			onComplete(self);
		}
	}

}
