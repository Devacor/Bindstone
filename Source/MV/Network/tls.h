#ifndef _MV_TLS_H_
#define _MV_TLS_H_

#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

struct ssl_st;
struct ssl_ctx_st;

namespace MV {
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

	// An OpenSSL SSL_CTX with the settings Network/AGENTS.md lists.
	class TlsContext {
	public:
		explicit TlsContext(bool a_server);
		~TlsContext();

		TlsContext(const TlsContext&) = delete;
		TlsContext& operator=(const TlsContext&) = delete;

		ssl_ctx_st* native() const {
			return context;
		}
		bool server() const {
			return ourServer;
		}

	private:
		ssl_ctx_st *context = nullptr;
		bool ourServer;
	};

	std::shared_ptr<TlsContext> makeTlsServerContext(const TlsConfig &a_config);
	std::shared_ptr<TlsContext> makeTlsClientContext(const TlsConfig &a_config);
	// For public hosts (HTTPS, SMTP): the system's root certificates, or only a_trustedCaFile when given.
	std::shared_ptr<TlsContext> makePublicTlsClientContext(const std::string &a_trustedCaFile = {});

	// Before a client handshake: sends SNI (for a name) and makes the handshake fail unless the
	// certificate is valid for a_host (a DNS name or an IP address).
	void verifyTlsPeerAs(ssl_st *a_ssl, const std::string &a_host);

	std::optional<PeerCertificate> tlsPeerCertificate(ssl_st *a_ssl);
	// The negotiated protocol and cipher, e.g. "TLSv1.3 TLS_AES_256_GCM_SHA384".
	std::string tlsSessionDescription(ssl_st *a_ssl);
	// The handshake error with the certificate verification result when that is what failed.
	std::string describeTlsFailure(const std::error_code &a_error, ssl_st *a_ssl);

	// OpenSSL's packed error codes (ERR_get_error), described as "reason (library)".
	const std::error_category& tlsCategory();

	std::string openSslVersion();
	void logOpenSslVersion();
}

#endif
