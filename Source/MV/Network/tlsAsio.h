#ifndef _MV_TLS_ASIO_H_
#define _MV_TLS_ASIO_H_

// The asio half of tls.h. Include it only from .cpp files: headers stay free of asio.
#include <memory>
#include <optional>
#include <string>
#include "MV/Network/tls.h"
#include <asio.hpp>
#include <asio/ssl.hpp>

namespace MV {
	using TlsStream = asio::ssl::stream<asio::ip::tcp::socket>;

	std::shared_ptr<asio::ssl::context> makeTlsServerContext(const TlsConfig &a_config);
	std::shared_ptr<asio::ssl::context> makeTlsClientContext(const TlsConfig &a_config);
	// For public hosts (HTTPS, SMTP): the system's root certificates, or only a_trustedCaFile when given.
	std::shared_ptr<asio::ssl::context> makePublicTlsClientContext(const std::string &a_trustedCaFile = {});

	// Before a client handshake: sends SNI (for a name) and makes the handshake fail unless the
	// certificate is valid for a_host (a DNS name or an IP address).
	void verifyTlsPeerAs(TlsStream &a_stream, const std::string &a_host);

	std::optional<PeerCertificate> tlsPeerCertificate(TlsStream &a_stream);
	// The negotiated protocol and cipher, e.g. "TLSv1.3 TLS_AES_256_GCM_SHA384".
	std::string tlsSessionDescription(TlsStream &a_stream);
	// The handshake error with the certificate verification result when that is what failed.
	std::string describeTlsFailure(const asio::error_code &a_error, TlsStream &a_stream);
}

#endif
