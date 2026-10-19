// The linked OpenSSL and a loopback TLS handshake through asio, Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>

#include <asio.hpp>
#include <asio/ssl.hpp>
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
	asio::error_code serverError {asio::error::would_block};
	asio::error_code clientError {asio::error::would_block};
	int serverProtocol = 0;
	std::string clientProtocol;
	std::string cipher;
	long clientVerifyResult = X509_V_OK;
	std::string received;
};

HandshakeOutcome loopbackHandshake(const Identity& a_serverIdentity, const Identity& a_clientTrusts) {
	asio::io_context io;

	asio::ssl::context serverContext(asio::ssl::context::tls_server);
	SSL_CTX_set_min_proto_version(serverContext.native_handle(), TLS1_3_VERSION);
	SSL_CTX_use_certificate(serverContext.native_handle(), a_serverIdentity.certificate.get());
	SSL_CTX_use_PrivateKey(serverContext.native_handle(), a_serverIdentity.key.get());

	asio::ssl::context clientContext(asio::ssl::context::tls_client);
	X509_STORE_add_cert(SSL_CTX_get_cert_store(clientContext.native_handle()), a_clientTrusts.certificate.get());
	clientContext.set_verify_mode(asio::ssl::verify_peer);

	asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
	asio::ssl::stream<asio::ip::tcp::socket> server(io, serverContext);
	asio::ssl::stream<asio::ip::tcp::socket> client(io, clientContext);
	SSL_set_tlsext_host_name(client.native_handle(), "localhost");
	SSL_set1_host(client.native_handle(), "localhost");

	HandshakeOutcome result;
	std::string serverBuffer(5, '\0');
	acceptor.async_accept(server.lowest_layer(), [&](const asio::error_code& a_acceptError) {
		if (a_acceptError) {
			result.serverError = a_acceptError;
			return;
		}
		server.async_handshake(asio::ssl::stream_base::server, [&](const asio::error_code& a_handshakeError) {
			result.serverError = a_handshakeError;
			if (!a_handshakeError) {
				result.serverProtocol = SSL_version(server.native_handle());
				asio::async_read(server, asio::buffer(serverBuffer), [&](const asio::error_code& a_readError, std::size_t) {
					if (!a_readError) {
						result.received = serverBuffer;
					}
				});
			}
		});
	});
	client.lowest_layer().async_connect(acceptor.local_endpoint(), [&](const asio::error_code& a_connectError) {
		if (a_connectError) {
			result.clientError = a_connectError;
			return;
		}
		client.async_handshake(asio::ssl::stream_base::client, [&](const asio::error_code& a_handshakeError) {
			result.clientError = a_handshakeError;
			if (!a_handshakeError) {
				asio::async_write(client, asio::buffer(std::string_view("hello")), [](const asio::error_code&, std::size_t) {});
			}
		});
	});
	io.run_for(std::chrono::seconds(10));

	result.clientProtocol = SSL_get_version(client.native_handle());
	if (const SSL_CIPHER* cipher = SSL_get_current_cipher(client.native_handle())) {
		result.cipher = SSL_CIPHER_get_name(cipher);
	}
	result.clientVerifyResult = SSL_get_verify_result(client.native_handle());
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

		test("a loopback handshake through asio negotiates TLS 1.3, verifies the server and carries data", [this]() {
			auto identity = openssl_detail::makeSelfSignedIdentity("localhost");
			auto outcome = openssl_detail::loopbackHandshake(identity, identity);
			std::cout << "    negotiated: " << outcome.clientProtocol << " " << outcome.cipher << std::endl;
			check_eq(std::string(), outcome.clientError ? outcome.clientError.message() : std::string());
			check(!outcome.serverError);
			check_eq(static_cast<int>(TLS1_3_VERSION), outcome.serverProtocol);
			check_eq(std::string("TLSv1.3"), outcome.clientProtocol);
			check_eq(static_cast<long>(X509_V_OK), outcome.clientVerifyResult);
			check_eq(std::string("hello"), outcome.received);
		});

		test("a client refuses a server certificate it does not trust", [this]() {
			auto serverIdentity = openssl_detail::makeSelfSignedIdentity("localhost");
			auto otherIdentity = openssl_detail::makeSelfSignedIdentity("localhost");
			auto outcome = openssl_detail::loopbackHandshake(serverIdentity, otherIdentity);
			check(static_cast<bool>(outcome.clientError));
			check(outcome.clientError != asio::error::would_block);
			check(outcome.clientVerifyResult != X509_V_OK);
			check(outcome.received.empty());
		});
	}
};

}

using openssl_suite = mv_tests::openssl_suite;
FOUNDRY_REGISTER(openssl_suite)
