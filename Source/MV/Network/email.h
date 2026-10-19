#ifndef _MV_EMAIL_H_
#define _MV_EMAIL_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>
#include "MV/Network/networkContext.h"
#include "MV/Utility/generalUtility.h"

namespace MV {

	struct SmtpResponse {
		int status = 0;
		std::string message;
		std::vector<std::string> lines;
	};

	enum class EmailError { NONE, START, RESOLVE, CONNECT, TLS, WRITE, READ, REJECTED };

	struct EmailResult {
		EmailError error = EmailError::NONE;
		// What went wrong, for logs; empty on success.
		std::string errorMessage;
		// The server's last reply.
		SmtpResponse lastReply;

		bool ok() const {
			return error == EmailError::NONE;
		}
	};

	// One plain-text message to one recipient over STARTTLS with AUTH LOGIN: no MIME parts, no CC or BCC.
	class Email : public std::enable_shared_from_this<Email> {
	public:
		struct AsioSmtp;
		using CompletionCallback = std::function<void(const std::shared_ptr<Email>&)>;

		struct Credentials {
			Credentials(const std::string &a_name, const std::string &a_password) :
				name(a_name),
				password(a_password) {
			}

			std::string name;
			std::string password;
		};

		struct Addresses {
			Addresses(){}

			Addresses(const std::string &a_from, const std::string &a_to) :
				from(a_from),
				to(a_to) {

				sanitize();
			}

			Addresses(const std::string &a_fromName, const std::string &a_from, const std::string &a_toName, const std::string &a_to) :
				from(a_from),
				fromName(a_fromName),
				to(a_to),
				toName(a_toName){

				sanitize();
			}

			std::string from;
			std::string fromName;
			std::string to;
			std::string toName;

		private:
			void sanitize() {
				from = strip(from);
				to = strip(to);
				fromName = strip(fromName);
				toName = strip(toName);
			}

			std::string strip(std::string a_original) {
				a_original.erase(std::remove_if(a_original.begin(), a_original.end(), [](char c) {return c == '\n' || c == '\r' || c == '\0' || c == '<' || c == '>' || c == '"'; }), a_original.end());
				return a_original;
			}
		};

		static std::shared_ptr<Email> make(const std::string &a_host, const std::string &a_port, const Credentials &a_credentials);

		Email(const Email&) = delete;
		Email& operator=(const Email&) = delete;
		~Email();

		// Runs the SMTP conversation on the calling thread and returns its outcome.
		EmailResult sendBlocking(const Addresses &a_addresses, const std::string &a_title, const std::string &a_message);

		// Returns at once: the conversation runs on a_context's threads, and a_onComplete runs once it ends, queued on
		// a_callbackQueue when one is given. An Email sends one message.
		void send(const std::shared_ptr<NetworkContext> &a_context, const Addresses &a_addresses, const std::string &a_title, const std::string &a_message, CompletionCallback a_onComplete = {}, std::shared_ptr<CallbackQueue> a_callbackQueue = {});

		// Trust only this CA (PEM) for the server's certificate instead of the system's root certificates.
		std::shared_ptr<Email> trustedCaFile(const std::string &a_caFile) {
			ourTrustedCaFile = a_caFile;
			return shared_from_this();
		}

		bool complete() const {
			std::lock_guard<std::mutex> guard(lock);
			return done;
		}

		// Final once complete() is true.
		EmailResult result() const {
			std::lock_guard<std::mutex> guard(lock);
			return ourResult;
		}

	private:
		Email(const std::string& a_host, const std::string &a_port, const Credentials &a_credentials);

		void begin(const std::shared_ptr<NetworkContext> &a_context, const Addresses &a_addresses, const std::string &a_title, const std::string &a_message);
		void storeMessageString(const std::string &a_title, const std::string &a_message);
		std::string domainFromEmail(const std::string &a_email);

		void initiateConnection();
		void smtpHandshake();
		void secureLogin();
		void sendMessageAndQuit();

		// Writes a_content and, when a_onReply is set, reads the server's reply; a reply outside a_validResponses fails the send.
		void command(const std::string &a_content, std::vector<int> a_validResponses, std::function<void()> a_onReply, bool a_secure);
		void readReply(std::vector<int> a_validResponses, std::function<void()> a_onReply, bool a_secure);

		void succeed();
		void fail(EmailError a_error, const std::string &a_reason);
		void fail(EmailError a_error, const std::error_code &a_err, const std::string &a_what);
		void finish();

		std::unique_ptr<AsioSmtp> asioSmtp;

		mutable std::mutex lock;

		bool started = false;
		bool done = false;
		EmailResult ourResult;
		SmtpResponse pendingReply;

		Addresses addresses;
		std::string message;

		Credentials credentials;

		std::string host;
		std::string port;
		std::string ourTrustedCaFile;

		CompletionCallback onComplete;
		std::shared_ptr<CallbackQueue> callbackQueue;
	};

}

#endif
