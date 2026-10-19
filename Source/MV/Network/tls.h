#ifndef _MV_TLS_H_
#define _MV_TLS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <asio.hpp>
#include <asio/ssl.hpp>

namespace MV {
	using TlsStream = asio::ssl::stream<asio::ip::tcp::socket>;

	// File paths resolve like assets: the working directory, then the user folder, then Assets/.
	struct TlsConfig {
		// The peer's certificate must chain to a certificate in this PEM file; no other root is trusted.
		std::string trustedCaFile;
		// Our certificate chain and key: a server's identity, or the client certificate a client presents.
		std::string certificateFile;
		std::string privateKeyFile;
		// Server only: refuse a peer without a certificate from trustedCaFile (mutual TLS).
		bool requirePeerCertificate = false;
	};

	struct PeerCertificate {
		std::string commonName;
		std::vector<std::string> dnsNames;
		std::vector<std::string> ipAddresses;

		// Exact (case-insensitive) DNS name or IP address match against the subjectAltNames.
		bool names(const std::string &a_host) const;
		std::string describe() const;
	};

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

	std::string openSslVersion();
	void logOpenSslVersion();
}

#endif
