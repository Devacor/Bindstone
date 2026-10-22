// The linked OpenSSL and a loopback TLS handshake through MV's network stream, Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>

#include "MV/Network/networkStream.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

using namespace jai::foundry;

namespace mv_tests {

namespace openssl_detail {

struct Identity {
	std::shared_ptr<EVP_PKEY> key;
	std::shared_ptr<X509> certificate;
};

Identity makeSelfSignedIdentity(const char* a_commonName) {
	Identity result;
	result.key.reset(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"), EVP_PKEY_free);
	result.certificate.reset(X509_new(), X509_free);
	X509* certificate = result.certificate.get();
	X509_set_version(certificate, X509_VERSION_3);
	ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
	X509_gmtime_adj(X509_getm_notBefore(certificate), -60);
	X509_gmtime_adj(X509_getm_notAfter(certificate), 3600);
	X509_set_pubkey(certificate, result.key.get());
	X509_NAME* name = X509_get_subject_name(certificate);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(a_commonName), -1, -1, 0);
	X509_set_issuer_name(certificate, name);
	X509V3_CTX extensionContext;
	X509V3_set_ctx_nodb(&extensionContext);
	X509V3_set_ctx(&extensionContext, certificate, certificate, nullptr, nullptr, 0);
	for (auto [nid, value] : {std::pair{NID_subject_alt_name, "DNS:localhost"}, std::pair{NID_basic_constraints, "critical,CA:TRUE"}}) {
		X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &extensionContext, nid, value);
		X509_add_ext(certificate, extension, -1);
		X509_EXTENSION_free(extension);
	}
	X509_sign(certificate, result.key.get(), EVP_sha256());
	return result;
}

struct HandshakeOutcome {
	bool serverDone = false;
	std::string serverError;
	bool clientDone = false;
	std::string clientError;
	int serverProtocol = 0;
	std::string clientProtocol;
	std::string cipher;
	long clientVerifyResult = X509_V_OK;
	std::string received;
};

HandshakeOutcome loopbackHandshake(const Identity& a_serverIdentity, const Identity& a_clientTrusts) {
	MV::TlsContext serverContext(true);
	SSL_CTX_set_min_proto_version(serverContext.native(), TLS1_3_VERSION);
	SSL_CTX_use_certificate(serverContext.native(), a_serverIdentity.certificate.get());
	SSL_CTX_use_PrivateKey(serverContext.native(), a_serverIdentity.key.get());

	MV::TlsContext clientContext(false);
	X509_STORE_add_cert(SSL_CTX_get_cert_store(clientContext.native()), a_clientTrusts.certificate.get());
	SSL_CTX_set_verify(clientContext.native(), SSL_VERIFY_PEER, nullptr);

	HandshakeOutcome result;
	MV::NetworkReactor reactor;
	std::shared_ptr<MV::NetworkStream> server;
	auto listener = MV::NetworkListener::make(reactor, *MV::SocketAddress::parse("127.0.0.1"), [&](MV::Socket a_socket) {
		server = MV::NetworkStream::make(reactor, std::move(a_socket));
		server->beginTls(serverContext);
		server->handshake([&](const std::error_code& a_error) {
			result.serverDone = true;
			result.serverError = a_error ? a_error.message() : std::string();
			if (a_error) {
				return;
			}
			result.serverProtocol = SSL_version(server->tls());
			server->onInput([&] {
				if (server->input().size() >= 5) {
					result.received = std::string(server->input().substr(0, 5));
					reactor.stop();
				}
			});
			server->reading(true);
		});
	});
	auto client = MV::NetworkStream::make(reactor);
	client->connect({*MV::SocketAddress::parse("127.0.0.1", listener->port())}, [&](const std::error_code& a_connectError) {
		if (a_connectError) {
			result.clientDone = true;
			result.clientError = a_connectError.message();
			reactor.stop();
			return;
		}
		auto* ssl = client->beginTls(clientContext);
		SSL_set_tlsext_host_name(ssl, "localhost");
		SSL_set1_host(ssl, "localhost");
		client->handshake([&](const std::error_code& a_handshakeError) {
			result.clientDone = true;
			result.clientError = a_handshakeError ? a_handshakeError.message() : std::string();
			if (a_handshakeError) {
				reactor.stop();
			} else {
				client->write("hello");
			}
		});
	});
	reactor.scheduleAfter(std::chrono::seconds(10), [&] { reactor.stop(); });
	reactor.run();

	if (client->tls()) {
		result.clientProtocol = SSL_get_version(client->tls());
		if (const SSL_CIPHER* cipher = SSL_get_current_cipher(client->tls())) {
			result.cipher = SSL_CIPHER_get_name(cipher);
		}
		result.clientVerifyResult = SSL_get_verify_result(client->tls());
	}
	return result;
}

}

class openssl_suite : public suite {
public:
	openssl_suite() : suite("MV OpenSSL") {}

	void forge_tests() override {
		test("the linked library is the OpenSSL 3 release the headers declare", [this]() {
			std::cout << "    linked: " << OpenSSL_version(OPENSSL_VERSION) << ", headers: " << OPENSSL_VERSION_TEXT << std::endl;
			check_eq(static_cast<unsigned long>(OPENSSL_VERSION_NUMBER), OpenSSL_version_num());
			check(OpenSSL_version_num() >= 0x30000000UL);
		});

		test("a loopback handshake through MV's network stream negotiates TLS 1.3, verifies the server and carries data", [this]() {
			auto identity = openssl_detail::makeSelfSignedIdentity("localhost");
			auto outcome = openssl_detail::loopbackHandshake(identity, identity);
			std::cout << "    negotiated: " << outcome.clientProtocol << " " << outcome.cipher << std::endl;
			check(outcome.clientDone && outcome.serverDone);
			check_eq(std::string(), outcome.clientError);
			check_eq(std::string(), outcome.serverError);
			check_eq(static_cast<int>(TLS1_3_VERSION), outcome.serverProtocol);
			check_eq(std::string("TLSv1.3"), outcome.clientProtocol);
			check_eq(static_cast<long>(X509_V_OK), outcome.clientVerifyResult);
			check_eq(std::string("hello"), outcome.received);
		});

		test("a client refuses a server certificate it does not trust", [this]() {
			auto serverIdentity = openssl_detail::makeSelfSignedIdentity("localhost");
			auto otherIdentity = openssl_detail::makeSelfSignedIdentity("localhost");
			auto outcome = openssl_detail::loopbackHandshake(serverIdentity, otherIdentity);
			check(outcome.clientDone && !outcome.clientError.empty());
			check(outcome.clientVerifyResult != X509_V_OK);
			check(outcome.received.empty());
		});
	}
};

}

using openssl_suite = mv_tests::openssl_suite;
FOUNDRY_REGISTER(openssl_suite)
