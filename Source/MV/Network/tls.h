#ifndef _MV_TLS_H_
#define _MV_TLS_H_

#include <string>
#include <vector>

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

	std::string openSslVersion();
	void logOpenSslVersion();
}

#endif
