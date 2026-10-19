#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#endif

#include "tls.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/log.h"
#include "MV/Utility/require.hpp"

#include <algorithm>
#include <cctype>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

// Present from OpenSSL 1.1.0h; the bit is unused before that, so older libraries simply ignore it.
#ifndef SSL_OP_NO_RENEGOTIATION
#define SSL_OP_NO_RENEGOTIATION 0x40000000U
#endif

namespace MV {
	namespace {
		const char* const TLS12_CIPHERS = "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:"
			"ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-CHACHA20-POLY1305";
		const char* const KEY_EXCHANGE_GROUPS = "X25519:P-256:P-384";
		const char* const MAKE_CERTS_HINT = " (make the local certificates with: python Tools/LocalServer/make_certs.py)";

		std::string loadPem(const std::string &a_path, const std::string &a_what) {
			auto result = fileContents(a_path);
			if (result.empty()) {
				throw ResourceException("TLS: missing " + a_what + " [" + a_path + "]" + MAKE_CERTS_HINT);
			}
			return result;
		}

		void harden(asio::ssl::context &a_context, bool a_server) {
			auto* native = a_context.native_handle();
			a_context.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_compression |
				asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1);
			SSL_CTX_set_options(native, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_TICKET);
			require<ResourceException>(SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION) == 1, "TLS: OpenSSL refused TLS 1.2 as the minimum version");
			require<ResourceException>(SSL_CTX_set_cipher_list(native, TLS12_CIPHERS) == 1, "TLS: OpenSSL accepts none of the TLS 1.2 ciphers");
			require<ResourceException>(SSL_CTX_set1_curves_list(native, KEY_EXCHANGE_GROUPS) == 1, "TLS: OpenSSL accepts none of the key exchange groups");
			if (a_server) {
				SSL_CTX_set_options(native, SSL_OP_CIPHER_SERVER_PREFERENCE);
				SSL_CTX_set_session_cache_mode(native, SSL_SESS_CACHE_OFF);
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
				SSL_CTX_set_num_tickets(native, 0);
#endif
			}
		}

		void trust(asio::ssl::context &a_context, const std::string &a_caFile) {
			auto pem = loadPem(a_caFile, "trusted CA certificate");
			asio::error_code error;
			a_context.add_certificate_authority(asio::buffer(pem), error);
			require<ResourceException>(!error, "TLS: unreadable CA certificate [", a_caFile, "]: ", error.message());
		}

		void identify(asio::ssl::context &a_context, const TlsConfig &a_config) {
			auto chain = loadPem(a_config.certificateFile, "certificate");
			auto key = loadPem(a_config.privateKeyFile, "private key");
			asio::error_code error;
			a_context.use_certificate_chain(asio::buffer(chain), error);
			require<ResourceException>(!error, "TLS: unreadable certificate [", a_config.certificateFile, "]: ", error.message());
			a_context.use_private_key(asio::buffer(key), asio::ssl::context::pem, error);
			require<ResourceException>(!error, "TLS: unreadable private key [", a_config.privateKeyFile, "]: ", error.message());
			require<ResourceException>(SSL_CTX_check_private_key(a_context.native_handle()) == 1, "TLS: [", a_config.privateKeyFile, "] is not the key of [", a_config.certificateFile, "]");
		}

		void trustSystemRoots(asio::ssl::context &a_context) {
#ifdef _WIN32
			HCERTSTORE systemStore = CertOpenSystemStoreW(0, L"ROOT");
			if (!systemStore) {
				warning("TLS: the Windows ROOT certificate store did not open; no public host will verify");
				return;
			}
			X509_STORE* store = SSL_CTX_get_cert_store(a_context.native_handle());
			PCCERT_CONTEXT certificate = nullptr;
			while ((certificate = CertEnumCertificatesInStore(systemStore, certificate)) != nullptr) {
				const unsigned char* encoded = certificate->pbCertEncoded;
				if (X509* x509 = d2i_X509(nullptr, &encoded, static_cast<long>(certificate->cbCertEncoded))) {
					X509_STORE_add_cert(store, x509);
					X509_free(x509);
				}
			}
			ERR_clear_error();
			CertCloseStore(systemStore, 0);
#else
			asio::error_code ignored;
			a_context.set_default_verify_paths(ignored);
#endif
		}

		std::string asn1Text(const ASN1_STRING* a_string) {
			return std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(a_string)), static_cast<size_t>(ASN1_STRING_length(a_string)));
		}

		std::string ipText(const ASN1_OCTET_STRING* a_bytes) {
			auto data = ASN1_STRING_get0_data(a_bytes);
			auto length = ASN1_STRING_length(a_bytes);
			if (length == 4) {
				return asio::ip::address_v4({data[0], data[1], data[2], data[3]}).to_string();
			} else if (length == 16) {
				asio::ip::address_v6::bytes_type bytes;
				std::copy(data, data + 16, bytes.begin());
				return asio::ip::address_v6(bytes).to_string();
			}
			return {};
		}

		std::string normalizedHost(const std::string &a_host) {
			auto result = a_host;
			if (result.size() > 2 && result.front() == '[' && result.back() == ']') {
				result = result.substr(1, result.size() - 2);
			}
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		std::optional<asio::ip::address> ipAddress(const std::string &a_host) {
			asio::error_code error;
			auto result = asio::ip::make_address(a_host, error);
			if (error) {
				return std::nullopt;
			}
			return result;
		}
	}

	bool PeerCertificate::names(const std::string &a_host) const {
		auto host = normalizedHost(a_host);
		if (auto address = ipAddress(host)) {
			return std::any_of(ipAddresses.begin(), ipAddresses.end(), [&](const std::string &a_ip) {
				auto certified = ipAddress(a_ip);
				return certified && *certified == *address;
			});
		}
		return std::any_of(dnsNames.begin(), dnsNames.end(), [&](const std::string &a_name) { return normalizedHost(a_name) == host; });
	}

	std::string PeerCertificate::describe() const {
		std::string result = "CN=" + commonName;
		for (auto&& name : dnsNames) {
			result += " DNS:" + name;
		}
		for (auto&& address : ipAddresses) {
			result += " IP:" + address;
		}
		return result;
	}

	std::shared_ptr<asio::ssl::context> makeTlsServerContext(const TlsConfig &a_config) {
		auto result = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
		harden(*result, true);
		identify(*result, a_config);
		trust(*result, a_config.trustedCaFile);
		result->set_verify_mode(a_config.requirePeerCertificate ? (asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert) : asio::ssl::verify_none);
		return result;
	}

	std::shared_ptr<asio::ssl::context> makeTlsClientContext(const TlsConfig &a_config) {
		auto result = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
		harden(*result, false);
		trust(*result, a_config.trustedCaFile);
		if (!a_config.certificateFile.empty()) {
			identify(*result, a_config);
		}
		result->set_verify_mode(asio::ssl::verify_peer);
		return result;
	}

	std::shared_ptr<asio::ssl::context> makePublicTlsClientContext(const std::string &a_trustedCaFile) {
		auto result = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
		harden(*result, false);
		if (a_trustedCaFile.empty()) {
			trustSystemRoots(*result);
		} else {
			trust(*result, a_trustedCaFile);
		}
		result->set_verify_mode(asio::ssl::verify_peer);
		return result;
	}

	void verifyTlsPeerAs(TlsStream &a_stream, const std::string &a_host) {
		auto host = normalizedHost(a_host);
		auto* ssl = a_stream.native_handle();
		auto* parameters = SSL_get0_param(ssl);
		X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
		bool expected = false;
		if (ipAddress(host)) {
			expected = X509_VERIFY_PARAM_set1_ip_asc(parameters, host.c_str()) == 1;
		} else {
			expected = SSL_set_tlsext_host_name(ssl, host.c_str()) == 1 && SSL_set1_host(ssl, host.c_str()) == 1;
		}
		require<ResourceException>(expected && !host.empty(), "TLS: cannot verify a peer as [", a_host, "]");
	}

	std::optional<PeerCertificate> tlsPeerCertificate(TlsStream &a_stream) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
		X509* certificate = SSL_get1_peer_certificate(a_stream.native_handle());
#else
		X509* certificate = SSL_get_peer_certificate(a_stream.native_handle());
#endif
		if (!certificate) {
			return std::nullopt;
		}
		PeerCertificate result;
		if (auto* subject = X509_get_subject_name(certificate)) {
			auto index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
			if (index >= 0) {
				result.commonName = asn1Text(X509_NAME_ENTRY_get_data(X509_NAME_get_entry(subject, index)));
			}
		}
		if (auto* names = static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr))) {
			for (int i = 0; i < sk_GENERAL_NAME_num(names); ++i) {
				auto* name = sk_GENERAL_NAME_value(names, i);
				if (name->type == GEN_DNS) {
					result.dnsNames.push_back(asn1Text(name->d.dNSName));
				} else if (name->type == GEN_IPADD) {
					result.ipAddresses.push_back(ipText(name->d.iPAddress));
				}
			}
			GENERAL_NAMES_free(names);
		}
		X509_free(certificate);
		return result;
	}

	std::string tlsSessionDescription(TlsStream &a_stream) {
		auto* ssl = a_stream.native_handle();
		return std::string(SSL_get_version(ssl)) + " " + SSL_CIPHER_get_name(SSL_get_current_cipher(ssl));
	}

	std::string describeTlsFailure(const asio::error_code &a_error, TlsStream &a_stream) {
		auto result = a_error.message();
		auto verification = SSL_get_verify_result(a_stream.native_handle());
		if (verification != X509_V_OK) {
			result += ": " + std::string(X509_verify_cert_error_string(verification));
		}
		return result;
	}

	std::string openSslVersion() {
		return OpenSSL_version(OPENSSL_VERSION);
	}

	void logOpenSslVersion() {
		info("TLS: ", openSslVersion(), " (built against ", OPENSSL_VERSION_TEXT, ")");
	}
}
