#pragma once

#ifndef __JAISCRIPT_STDLIB_MAIL_HPP__
#define __JAISCRIPT_STDLIB_MAIL_HPP__

#include <memory>
#include <string>
#include <vector>

namespace jai {
class engine;

	// How a conversation is secured: STARTTLS after the greeting (refused when the server does not offer it), TLS from the
	// first byte (port 465), or none, which a transport allows only to a host whose every address is loopback
	enum class smtp_security { starttls, tls, none };

	// One message as mail::smtp.send hands it to the host: the relay, its security, the login (none when username is empty),
	// the name EHLO gives, the envelope (MAIL FROM and every RCPT TO, bcc included), the message (headers and body with CRLF
	// line ends, not yet dot-stuffed) and the seconds left for the whole conversation.
	struct smtp_request {
		std::string host;
		int port = 587;
		smtp_security security = smtp_security::starttls;
		std::string username;
		std::string password;
		std::string hello;
		std::string from;
		std::vector<std::string> recipients;
		std::string message;
		double timeout_seconds = 30.0;
	};

	// What came back: the server's reply to the message (its code and text, "250 2.0.0 queued as 4F3B2"), or why the
	// conversation failed in error, which never holds the login
	struct smtp_reply {
		std::string reply;
		std::string error;
	};

	// The host's SMTP client. send() runs one whole conversation on the calling thread; the library composes the message and
	// applies the options and the error texts above it. An exception thrown out of send() is reported like an error text.
	// jaiscript/net/smtp_socket_transport.hpp is a portable one over sockets.
	class smtp_transport {
	public:
		virtual ~smtp_transport() = default;
		virtual smtp_reply send(const smtp_request& a_request) = 0;
	};

namespace stdlib {

	// The mail:: SMTP client, registered only by this call (never by register_all): mail::smtp(options) and the mail::smtp
	// class's send(message) (source/implementation/stdlib/mail.cpp). send goes through a_transport and requires the
	// jai::permissions::network permission, disabled until the host enables it.
	void register_mail(engine& eng_ref, std::shared_ptr<smtp_transport> a_transport);

} // namespace stdlib
} // namespace jai

#endif
