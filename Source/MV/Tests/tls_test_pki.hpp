// Throwaway certificates for the TLS tests, made at test time with libcrypto into a temporary folder
// (nothing here is ever a real key). Compiles against OpenSSL 1.1 and 3.x alike.
#pragma once

#include "MV/Network/tls.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace mv_tests {

class TestPki {
public:
	struct Issued {
		std::string certificateFile;
		std::string privateKeyFile;
	};

	// a_name keeps two authorities apart; every TestPki is its own CA.
	explicit TestPki(const std::string& a_name = "test") {
		std::random_device random;
		folder = std::filesystem::temp_directory_path() / ("mv_tls_" + a_name + "_" + std::to_string(random()) + std::to_string(random()));
		std::filesystem::create_directories(folder);
		caKey = makeKey();
		caCertificate = makeCertificate(a_name + " Test CA", caKey, nullptr, nullptr, {"critical,CA:TRUE", "critical,keyCertSign,cRLSign", "", ""}, -60, 24 * 3600);
		caFile = write(a_name + "_ca.pem", pem(caCertificate));
	}

	~TestPki() {
		X509_free(caCertificate);
		EVP_PKEY_free(caKey);
		std::error_code ignored;
		std::filesystem::remove_all(folder, ignored);
	}

	TestPki(const TestPki&) = delete;
	TestPki& operator=(const TestPki&) = delete;

	// a_names: subjectAltNames such as "DNS:localhost,IP:127.0.0.1"; a_purpose "serverAuth" or "clientAuth".
	Issued issue(const std::string& a_commonName, const std::string& a_names, const std::string& a_purpose = "serverAuth", long a_validFromSeconds = -60, long a_validUntilSeconds = 24 * 3600) {
		auto key = makeKey();
		auto certificate = makeCertificate(a_commonName, key, caCertificate, caKey, {"critical,CA:FALSE", "critical,digitalSignature", a_purpose, a_names}, a_validFromSeconds, a_validUntilSeconds);
		auto stem = "leaf" + std::to_string(++issuedCount);
		Issued result {write(stem + ".pem", pem(certificate)), write(stem + ".key", pem(key))};
		X509_free(certificate);
		EVP_PKEY_free(key);
		return result;
	}

	Issued localServer(const std::string& a_commonName = "server") {
		return issue(a_commonName, "DNS:localhost,IP:127.0.0.1");
	}

	MV::TlsConfig serverConfig(const Issued& a_identity, bool a_requireClientCertificate = false) const {
		return {caFile, a_identity.certificateFile, a_identity.privateKeyFile, a_requireClientCertificate};
	}

	MV::TlsConfig clientConfig() const {
		return {caFile};
	}

	MV::TlsConfig clientConfig(const Issued& a_identity) const {
		return {caFile, a_identity.certificateFile, a_identity.privateKeyFile};
	}

	std::string caFile;

private:
	struct Extensions {
		std::string basicConstraints;
		std::string keyUsage;
		std::string extendedKeyUsage;
		std::string subjectAltNames;
	};

	static EVP_PKEY* makeKey() {
		EVP_PKEY* result = nullptr;
		EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
		if (!context || EVP_PKEY_keygen_init(context) != 1 || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context, NID_X9_62_prime256v1) != 1 || EVP_PKEY_keygen(context, &result) != 1) {
			EVP_PKEY_CTX_free(context);
			throw std::runtime_error("test PKI: EC key generation failed");
		}
		EVP_PKEY_CTX_free(context);
		return result;
	}

	X509* makeCertificate(const std::string& a_commonName, EVP_PKEY* a_key, X509* a_issuer, EVP_PKEY* a_issuerKey, const Extensions& a_extensions, long a_validFromSeconds, long a_validUntilSeconds) {
		X509* result = X509_new();
		X509_set_version(result, 2);
		ASN1_INTEGER_set(X509_get_serialNumber(result), ++serial);
		X509_gmtime_adj(X509_getm_notBefore(result), a_validFromSeconds);
		X509_gmtime_adj(X509_getm_notAfter(result), a_validUntilSeconds);
		X509_set_pubkey(result, a_key);
		X509_NAME* subject = X509_get_subject_name(result);
		X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(a_commonName.c_str()), -1, -1, 0);
		X509_set_issuer_name(result, a_issuer ? X509_get_subject_name(a_issuer) : subject);
		X509V3_CTX extensionContext;
		X509V3_set_ctx_nodb(&extensionContext);
		X509V3_set_ctx(&extensionContext, a_issuer ? a_issuer : result, result, nullptr, nullptr, 0);
		addExtension(result, extensionContext, NID_basic_constraints, a_extensions.basicConstraints);
		addExtension(result, extensionContext, NID_key_usage, a_extensions.keyUsage);
		addExtension(result, extensionContext, NID_ext_key_usage, a_extensions.extendedKeyUsage);
		addExtension(result, extensionContext, NID_subject_alt_name, a_extensions.subjectAltNames);
		if (X509_sign(result, a_issuerKey ? a_issuerKey : a_key, EVP_sha256()) == 0) {
			X509_free(result);
			throw std::runtime_error("test PKI: signing failed");
		}
		return result;
	}

	static void addExtension(X509* a_certificate, X509V3_CTX& a_context, int a_nid, std::string a_value) {
		if (a_value.empty()) {
			return;
		}
		X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &a_context, a_nid, a_value.data());
		if (!extension) {
			throw std::runtime_error("test PKI: bad extension " + a_value);
		}
		X509_add_ext(a_certificate, extension, -1);
		X509_EXTENSION_free(extension);
	}

	static std::string drain(BIO* a_bio) {
		char* data = nullptr;
		auto length = BIO_get_mem_data(a_bio, &data);
		std::string result(data, static_cast<size_t>(length));
		BIO_free(a_bio);
		return result;
	}

	static std::string pem(X509* a_certificate) {
		BIO* bio = BIO_new(BIO_s_mem());
		PEM_write_bio_X509(bio, a_certificate);
		return drain(bio);
	}

	static std::string pem(EVP_PKEY* a_key) {
		BIO* bio = BIO_new(BIO_s_mem());
		PEM_write_bio_PrivateKey(bio, a_key, nullptr, nullptr, 0, nullptr, nullptr);
		return drain(bio);
	}

	std::string write(const std::string& a_name, const std::string& a_contents) {
		auto path = (folder / a_name).string();
		std::ofstream file(path, std::ios::binary);
		file << a_contents;
		return path;
	}

	std::filesystem::path folder;
	EVP_PKEY* caKey = nullptr;
	X509* caCertificate = nullptr;
	long serial = 0;
	int issuedCount = 0;
};

}
