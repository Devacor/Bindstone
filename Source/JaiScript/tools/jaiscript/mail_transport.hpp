#pragma once
// The SMTP transport the runner and jai_spider install for mail::smtp: jai::net::smtp_socket_transport on every platform,
// with OpenSSL's TLS when the build found OpenSSL (JAISCRIPT_TOOLS_HAVE_OPENSSL) and without TLS otherwise, where only
// security "none" to a loopback relay sends. Windows has no WinHTTP counterpart for SMTP, so there it is the latter.
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/mail.hpp>
#include <jaiscript/net/smtp_socket_transport.hpp>
#ifdef JAISCRIPT_TOOLS_HAVE_OPENSSL
#include <jaiscript/net/openssl_tls.hpp>
#endif

#include <memory>

namespace jai_runner {

// One transport for the process, shared by every engine on every thread: each conversation keeps its state on its caller's
// stack and openssl_tls only reads its context
inline const std::shared_ptr<jai::smtp_transport>& shared_smtp_transport() {
#ifdef JAISCRIPT_TOOLS_HAVE_OPENSSL
	static const std::shared_ptr<jai::smtp_transport> result = std::make_shared<jai::net::smtp_socket_transport>(std::make_shared<jai::net::openssl_tls>());
#else
	static const std::shared_ptr<jai::smtp_transport> result = std::make_shared<jai::net::smtp_socket_transport>();
#endif
	return result;
}

// mail:: with this build's transport and the network permission granted
inline void register_mail_client(jai::engine& a_engine) {
	jai::stdlib::register_mail(a_engine, shared_smtp_transport());
	a_engine.enable(jai::permissions::network);
}

} // namespace jai_runner
