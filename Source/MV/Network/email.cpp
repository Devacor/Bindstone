#include "MV/Network/email.h"
#include "MV/Network/networkReactor.h"
#include "MV/Network/networkStream.h"
#include "MV/Utility/log.h"
#include "MV/Utility/require.hpp"

#include <jaiscript/serialization/convenience.hpp>
#include <optional>
#include <sstream>

namespace MV {

	namespace {
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

	struct Email::Transport {
		Transport(std::shared_ptr<NetworkContext> a_context, const std::string& a_trustedCaFile) :
			sharedContext(std::move(a_context)),
			reactor(&sharedContext->reactor()),
			tlsContext(makePublicTlsClientContext(a_trustedCaFile)) {
		}

		// sendBlocking runs this reactor on the calling thread until the conversation ends.
		explicit Transport(const std::string& a_trustedCaFile) :
			ownedReactor(std::make_unique<NetworkReactor>(1)),
			reactor(ownedReactor.get()),
			tlsContext(makePublicTlsClientContext(a_trustedCaFile)) {
		}

		std::shared_ptr<NetworkContext> sharedContext;
		std::unique_ptr<NetworkReactor> ownedReactor;
		NetworkReactor* reactor;
		std::shared_ptr<TlsContext> tlsContext;
		std::shared_ptr<NetworkStream> stream;
		NetworkReactor::TimerId quitDeadline = 0;
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

	// The reactor dies here, after its run, so no stream or posted work of this send keeps the Email alive.
	EmailResult Email::sendBlocking(const Addresses &a_addresses, const std::string &a_title, const std::string &a_message) {
		begin(nullptr, a_addresses, a_title, a_message);
		if (transport && transport->ownedReactor) {
			transport->ownedReactor->run();
			transport->stream.reset();
			transport->ownedReactor.reset();
			transport->reactor = nullptr;
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
			transport = a_context ? std::make_unique<Transport>(a_context, ourTrustedCaFile) : std::make_unique<Transport>(ourTrustedCaFile);
			storeMessageString(a_title, a_message);
			transport->reactor->post([self = shared_from_this()] { self->initiateConnection(); });
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
		transport->stream = NetworkStream::make(*transport->reactor);
		auto stream = transport->stream;
		transport->reactor->resolve(host, port, [this, self, stream](const std::error_code& a_err, std::vector<SocketAddress> a_addresses) {
			if (a_err) {
				fail(EmailError::RESOLVE, a_err, "Email Resolve Failure");
				return;
			}
			stream->connect(std::move(a_addresses), [this, self, stream](const std::error_code& a_connectErr) {
				if (a_connectErr) {
					fail(EmailError::CONNECT, a_connectErr, "Email Connection Failure");
					return;
				}
				stream->onInput([this, self] { readReplies(); });
				stream->onClosed([this, self, stream](const std::error_code& a_closeErr) {
					if (stream->writing()) {
						fail(EmailError::WRITE, a_closeErr, "Email Write Failure");
					} else {
						fail(EmailError::READ, a_closeErr, "Email Read Failure");
					}
				});
				stream->reading(true);
				smtpHandshake();
			});
		});
	}

	void Email::smtpHandshake() {
		auto self = shared_from_this();
		expectReply({220}, [this, self] {
			command("EHLO " + domainFromEmail(addresses.from) + "\r\n", {250}, [this, self] {
				command("STARTTLS\r\n", {220}, [this, self] {
					ssl_st* ssl = nullptr;
					try {
						ssl = transport->stream->beginTls(*transport->tlsContext);
						verifyTlsPeerAs(ssl, host);
					} catch (std::exception& e) {
						fail(EmailError::TLS, e.what());
						return;
					}
					transport->stream->handshake([this, self, ssl](const std::error_code& a_err) {
						if (a_err) {
							fail(EmailError::TLS, "TLS handshake with " + host + " failed: " + describeTlsFailure(a_err, ssl));
						} else {
							secureLogin();
						}
					});
				});
			});
		});
	}

	void Email::secureLogin() {
		auto self = shared_from_this();
		command("EHLO " + domainFromEmail(addresses.from) + "\r\n", {250}, [this, self] {
			command("AUTH LOGIN\r\n", {334}, [this, self] {
				command(jai::base64_encode(credentials.name) + "\r\n", {334}, [this, self] {
					command(jai::base64_encode(credentials.password) + "\r\n", {235}, [this, self] {
						sendMessageAndQuit();
					});
				});
			});
		});
	}

	void Email::sendMessageAndQuit() {
		auto self = shared_from_this();
		command("MAIL FROM:<" + addresses.from + ">\r\n", {250}, [this, self] {
			command("RCPT TO:<" + addresses.to + ">\r\n", {250, 251}, [this, self] {
				command("DATA\r\n", {354}, [this, self] {
					command(message, {250}, [this, self] {
						succeed();
						info("Email Complete [", addresses.from, "] -> [", addresses.to, "]");
						command("QUIT\r\n", {}, {});
					});
				});
			});
		});
	}

	// The last command ends the conversation: TLS close_notify, then the server's close, or 2 s.
	void Email::command(const std::string &a_content, std::vector<int> a_validResponses, std::function<void()> a_onReply) {
		transport->stream->write(a_content);
		if (a_onReply) {
			expectReply(std::move(a_validResponses), std::move(a_onReply));
			return;
		}
		auto self = shared_from_this();
		transport->stream->finish([this, self] { closeTransport(); });
		transport->quitDeadline = transport->reactor->scheduleAfter(std::chrono::seconds(2), [this, self] {
			transport->quitDeadline = 0;
			closeTransport();
		});
	}

	void Email::expectReply(std::vector<int> a_validResponses, std::function<void()> a_onReply) {
		expectedReplies = std::move(a_validResponses);
		onReply = std::move(a_onReply);
		readReplies();
	}

	// A reply is any number of "ddd-text" lines and one final "ddd text" line, possibly split across reads.
	void Email::readReplies() {
		if (!onReply || !transport->stream) {
			return;
		}
		auto& stream = *transport->stream;
		auto available = stream.input();
		size_t consumed = 0;
		bool final = false;
		for (auto lineEnd = available.find("\r\n"); lineEnd != std::string_view::npos && !final; lineEnd = available.find("\r\n", consumed)) {
			auto line = std::string(available.substr(consumed, lineEnd - consumed));
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
				pendingReply.lines.push_back(line.size() > 4 ? line.substr(4) : "");
			}
		}
		stream.consume(consumed);
		if (!final) {
			return;
		}
		auto reply = std::move(pendingReply);
		pendingReply = {};
		{
			std::lock_guard<std::mutex> guard(lock);
			ourResult.lastReply = reply;
		}
		auto valid = std::move(expectedReplies);
		auto callback = std::move(onReply);
		expectedReplies.clear();
		onReply = nullptr;
		if (valid.empty() || std::find(valid.begin(), valid.end(), reply.status) != valid.end()) {
			callback();
		} else {
			fail(EmailError::REJECTED, "SMTP server replied " + std::to_string(reply.status) + " " + reply.message);
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
		bool failing = false;
		{
			std::lock_guard<std::mutex> guard(lock);
			if (!done) {
				done = true;
				failing = true;
				ourResult.error = a_error;
				ourResult.errorMessage = a_reason;
			}
		}
		if (failing) {
			error("Email failed [", addresses.from, "] -> [", addresses.to, "]: ", a_reason);
			finish();
		}
		closeTransport();
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

	// Network thread only once the stream exists; before that (a send that failed to start) only the stop applies.
	void Email::closeTransport() {
		if (!transport) {
			return;
		}
		if (transport->stream) {
			transport->stream->close();
			transport->reactor->cancel(transport->quitDeadline);
			transport->quitDeadline = 0;
		}
		if (transport->ownedReactor) {
			transport->ownedReactor->stop();
		}
	}

}
