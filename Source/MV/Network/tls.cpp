#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#endif

#include "MV/Network/tls.h"
#include "MV/Network/socket.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/log.h"
#include "MV/Utility/require.hpp"

#include <algorithm>
#include <cctype>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
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

		class TlsCategory : public std::error_category {
		public:
			const char* name() const noexcept override {
				return "TLS";
			}

			std::string message(int a_value) const override {
				auto packed = static_cast<unsigned long>(static_cast<unsigned int>(a_value));
				const char* reason = ERR_reason_error_string(packed);
				if (!reason) {
					return "TLS error";
				}
				std::string result = reason;
				if (const char* library = ERR_lib_error_string(packed)) {
					result += std::string(" (") + library + ")";
				}
				return result;
			}
		};

		std::string lastTlsError() {
			auto packed = ERR_get_error();
			ERR_clear_error();
			return std::error_code(static_cast<int>(packed), tlsCategory()).message();
		}

		struct BioDeleter {
			void operator()(BIO* a_bio) const {
				BIO_free(a_bio);
			}
		};

		std::unique_ptr<BIO, BioDeleter> memoryBio(const std::string &a_bytes) {
			return std::unique_ptr<BIO, BioDeleter>(BIO_new_mem_buf(a_bytes.data(), static_cast<int>(a_bytes.size())));
		}

		std::string loadPem(const std::string &a_path, const std::string &a_what) {
			auto result = fileContents(a_path);
			if (result.empty()) {
				throw ResourceException("TLS: missing " + a_what + " [" + a_path + "]" + MAKE_CERTS_HINT);
			}
			return result;
		}

		void harden(SSL_CTX *a_context, bool a_server) {
			SSL_CTX_set_options(a_context, SSL_OP_ALL | SSL_OP_NO_COMPRESSION | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_TICKET);
			require<ResourceException>(SSL_CTX_set_min_proto_version(a_context, TLS1_2_VERSION) == 1, "TLS: OpenSSL refused TLS 1.2 as the minimum version");
			require<ResourceException>(SSL_CTX_set_cipher_list(a_context, TLS12_CIPHERS) == 1, "TLS: OpenSSL accepts none of the TLS 1.2 ciphers");
			require<ResourceException>(SSL_CTX_set1_curves_list(a_context, KEY_EXCHANGE_GROUPS) == 1, "TLS: OpenSSL accepts none of the key exchange groups");
			if (a_server) {
				SSL_CTX_set_options(a_context, SSL_OP_CIPHER_SERVER_PREFERENCE);
				SSL_CTX_set_session_cache_mode(a_context, SSL_SESS_CACHE_OFF);
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
				SSL_CTX_set_num_tickets(a_context, 0);
#endif
			}
		}

		// Every certificate in the PEM file joins the trusted store.
		void trust(SSL_CTX *a_context, const std::string &a_caFile) {
			auto pem = loadPem(a_caFile, "trusted CA certificate");
			ERR_clear_error();
			auto bio = memoryBio(pem);
			X509_STORE* store = SSL_CTX_get_cert_store(a_context);
			bool added = false;
			while (X509* certificate = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)) {
				auto stored = X509_STORE_add_cert(store, certificate) == 1;
				X509_free(certificate);
				require<ResourceException>(stored, "TLS: unreadable CA certificate [", a_caFile, "]: ", lastTlsError());
				added = true;
			}
			auto ending = ERR_peek_last_error();
			bool cleanEnd = ERR_GET_LIB(ending) == ERR_LIB_PEM && ERR_GET_REASON(ending) == PEM_R_NO_START_LINE;
			require<ResourceException>(added && cleanEnd, "TLS: unreadable CA certificate [", a_caFile, "]: ", lastTlsError());
			ERR_clear_error();
		}

		void identify(SSL_CTX *a_context, const TlsConfig &a_config) {
			auto chain = loadPem(a_config.certificateFile, "certificate");
			auto key = loadPem(a_config.privateKeyFile, "private key");
			ERR_clear_error();
			auto chainBio = memoryBio(chain);
			X509* leaf = PEM_read_bio_X509_AUX(chainBio.get(), nullptr, nullptr, nullptr);
			bool used = leaf && SSL_CTX_use_certificate(a_context, leaf) == 1;
			X509_free(leaf);
			require<ResourceException>(used, "TLS: unreadable certificate [", a_config.certificateFile, "]: ", lastTlsError());
			SSL_CTX_clear_chain_certs(a_context);
			while (X509* intermediate = PEM_read_bio_X509(chainBio.get(), nullptr, nullptr, nullptr)) {
				if (SSL_CTX_add_extra_chain_cert(a_context, intermediate) != 1) {
					X509_free(intermediate);
					require<ResourceException>(false, "TLS: unreadable certificate [", a_config.certificateFile, "]: ", lastTlsError());
				}
			}
			ERR_clear_error();
			auto keyBio = memoryBio(key);
			EVP_PKEY* privateKey = PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr);
			bool keyed = privateKey && SSL_CTX_use_PrivateKey(a_context, privateKey) == 1;
			EVP_PKEY_free(privateKey);
			require<ResourceException>(keyed, "TLS: unreadable private key [", a_config.privateKeyFile, "]: ", lastTlsError());
			require<ResourceException>(SSL_CTX_check_private_key(a_context) == 1, "TLS: [", a_config.privateKeyFile, "] is not the key of [", a_config.certificateFile, "]");
		}

		void trustSystemRoots(SSL_CTX *a_context) {
#ifdef _WIN32
			HCERTSTORE systemStore = CertOpenSystemStoreW(0, L"ROOT");
			if (!systemStore) {
				warning("TLS: the Windows ROOT certificate store did not open; no public host will verify");
				return;
			}
			X509_STORE* store = SSL_CTX_get_cert_store(a_context);
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
			SSL_CTX_set_default_verify_paths(a_context);
			ERR_clear_error();
#endif
		}

		std::string asn1Text(const ASN1_STRING* a_string) {
			return std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(a_string)), static_cast<size_t>(ASN1_STRING_length(a_string)));
		}

		std::string ipText(const ASN1_OCTET_STRING* a_bytes) {
			auto address = SocketAddress::fromBytes(ASN1_STRING_get0_data(a_bytes), static_cast<size_t>(ASN1_STRING_length(a_bytes)));
			return address ? address->host() : std::string();
		}

		std::string normalizedHost(const std::string &a_host) {
			auto result = a_host;
			if (result.size() > 2 && result.front() == '[' && result.back() == ']') {
				result = result.substr(1, result.size() - 2);
			}
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}
	}

	const std::error_category& tlsCategory() {
		static const TlsCategory category;
		return category;
	}

	bool PeerCertificate::names(const std::string &a_host) const {
		auto host = normalizedHost(a_host);
		if (auto address = SocketAddress::parse(host)) {
			return std::any_of(ipAddresses.begin(), ipAddresses.end(), [&](const std::string &a_ip) {
				auto certified = SocketAddress::parse(a_ip);
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

	TlsContext::TlsContext(bool a_server) :
		context(SSL_CTX_new(a_server ? TLS_server_method() : TLS_client_method())),
		ourServer(a_server) {

		require<ResourceException>(context != nullptr, "TLS: OpenSSL could not make a context: ", lastTlsError());
	}

	TlsContext::~TlsContext() {
		SSL_CTX_free(context);
	}

	std::shared_ptr<TlsContext> makeTlsServerContext(const TlsConfig &a_config) {
		auto result = std::make_shared<TlsContext>(true);
		harden(result->native(), true);
		identify(result->native(), a_config);
		trust(result->native(), a_config.trustedCaFile);
		SSL_CTX_set_verify(result->native(), a_config.requirePeerCertificate ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT) : SSL_VERIFY_NONE, nullptr);
		return result;
	}

	std::shared_ptr<TlsContext> makeTlsClientContext(const TlsConfig &a_config) {
		auto result = std::make_shared<TlsContext>(false);
		harden(result->native(), false);
		trust(result->native(), a_config.trustedCaFile);
		if (!a_config.certificateFile.empty()) {
			identify(result->native(), a_config);
		}
		SSL_CTX_set_verify(result->native(), SSL_VERIFY_PEER, nullptr);
		return result;
	}

	std::shared_ptr<TlsContext> makePublicTlsClientContext(const std::string &a_trustedCaFile) {
		auto result = std::make_shared<TlsContext>(false);
		harden(result->native(), false);
		if (a_trustedCaFile.empty()) {
			trustSystemRoots(result->native());
		} else {
			trust(result->native(), a_trustedCaFile);
		}
		SSL_CTX_set_verify(result->native(), SSL_VERIFY_PEER, nullptr);
		return result;
	}

	void verifyTlsPeerAs(ssl_st *a_ssl, const std::string &a_host) {
		auto host = normalizedHost(a_host);
		auto* parameters = SSL_get0_param(a_ssl);
		X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
		bool expected = false;
		if (SocketAddress::parse(host)) {
			expected = X509_VERIFY_PARAM_set1_ip_asc(parameters, host.c_str()) == 1;
		} else {
			expected = SSL_set_tlsext_host_name(a_ssl, host.c_str()) == 1 && SSL_set1_host(a_ssl, host.c_str()) == 1;
		}
		require<ResourceException>(expected && !host.empty(), "TLS: cannot verify a peer as [", a_host, "]");
	}

	std::optional<PeerCertificate> tlsPeerCertificate(ssl_st *a_ssl) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
		X509* certificate = SSL_get1_peer_certificate(a_ssl);
#else
		X509* certificate = SSL_get_peer_certificate(a_ssl);
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

	std::string tlsSessionDescription(ssl_st *a_ssl) {
		return std::string(SSL_get_version(a_ssl)) + " " + SSL_CIPHER_get_name(SSL_get_current_cipher(a_ssl));
	}

	std::string describeTlsFailure(const std::error_code &a_error, ssl_st *a_ssl) {
		auto result = a_error.message();
		auto verification = a_ssl ? SSL_get_verify_result(a_ssl) : X509_V_OK;
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
