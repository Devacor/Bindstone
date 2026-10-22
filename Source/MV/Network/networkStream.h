#ifndef _MV_NETWORK_STREAM_H_
#define _MV_NETWORK_STREAM_H_

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include "MV/Network/networkReactor.h"
#include "MV/Network/tls.h"

struct bio_st;

namespace MV {
	// A non-blocking TCP connection on a reactor's network thread, buffered both ways, with an optional TLS layer that can
	// start at any point (STARTTLS). Every member runs on the network thread. The reactor owns an open stream, so its
	// callbacks may own the stream's owner: close() (or the reactor's end) drops them.
	class NetworkStream : public NetworkWatcher, public std::enable_shared_from_this<NetworkStream> {
	public:
		using ErrorCallback = std::function<void(const std::error_code &)>;

		// Unconnected: connect() next.
		static std::shared_ptr<NetworkStream> make(NetworkReactor &a_reactor);
		static std::shared_ptr<NetworkStream> make(NetworkReactor &a_reactor, Socket a_connected);

		~NetworkStream() override;

		// Tries each address in turn; a_onConnected gets success or the last address's failure.
		void connect(std::vector<SocketAddress> a_addresses, ErrorCallback a_onConnected);

		// Bytes written before this went out as they were; everything after is TLS. Configure the returned SSL (peer
		// verification) before handshake(). Unread input moves into TLS: the peer's first TLS bytes may already be here.
		ssl_st* beginTls(const TlsContext &a_context);
		// While it runs, any failure (the peer closing included) goes to a_onComplete instead of onClosed.
		void handshake(ErrorCallback a_onComplete);

		// Runs whenever bytes join input() while reading.
		void onInput(std::function<void()> a_onInput) {
			onInputCallback = std::move(a_onInput);
		}
		// Runs once: END_OF_STREAM after the peer's clean close (TLS close_notify, or FIN without TLS), STREAM_TRUNCATED
		// when TLS ends without close_notify, otherwise the socket or TLS error.
		void onClosed(ErrorCallback a_onClosed) {
			onClosedCallback = std::move(a_onClosed);
		}
		// A paused stream reads nothing from the socket and calls onInput with nothing.
		void reading(bool a_reading);

		std::string_view input() const {
			return std::string_view(inputBuffer).substr(inputConsumed);
		}
		void consume(size_t a_bytes);

		void write(std::string_view a_bytes);
		// Bytes not yet handed to the OS.
		bool writing() const {
			return outputSent < outputBuffer.size() || !pendingPlaintext.empty();
		}

		// TLS close_notify after everything already written, then the send side shuts; incoming bytes are read and
		// discarded until the peer closes, then a_onFinished runs (also on any error).
		void finish(std::function<void()> a_onFinished);
		void close();

		bool open() const {
			return socket.valid();
		}
		bool handshaking() const {
			return tlsHandshaking;
		}
		ssl_st* tls() const {
			return ssl;
		}
		const std::optional<SocketAddress>& peer() const {
			return ourPeer;
		}

		SocketHandle watchedHandle() const override {
			return socket.handle();
		}
		bool wantsRead() const override;
		bool wantsWrite() const override;
		void ready(bool a_readable, bool a_writable) override;
		void abandon() override;

	private:
		explicit NetworkStream(NetworkReactor &a_reactor);

		void connectNext();
		void connectionSettled();
		void readSocket();
		void runTls();
		void advanceHandshake();
		void decrypt();
		void moveTlsOutput();
		void flushOutput();
		void shutSend();
		void finished();
		void deliverInput();
		void socketClosedByPeer();
		void fail(const std::error_code &a_error);
		void reportClosed(const std::error_code &a_error);
		std::error_code tlsError(int a_code);

		NetworkReactor &reactor;
		Socket socket;
		std::optional<SocketAddress> ourPeer;

		std::vector<SocketAddress> addresses;
		size_t nextAddress = 0;
		bool connecting = false;
		std::error_code connectError;
		ErrorCallback onConnectedCallback;

		ssl_st *ssl = nullptr;
		bio_st *networkIn = nullptr;
		bio_st *networkOut = nullptr;
		bool tlsHandshaking = false;
		ErrorCallback onHandshakeCallback;
		std::string pendingPlaintext;

		bool readEnabled = false;
		bool deliveryPosted = false;
		// The peer's logical end (close_notify, or FIN without TLS) and the socket's FIN, which can come later.
		bool peerFinished = false;
		std::error_code finishCode;
		bool socketEnded = false;
		bool broken = false;
		bool discarding = false;
		bool finishing = false;
		bool sendShut = false;
		bool closeReported = false;

		std::string inputBuffer;
		size_t inputConsumed = 0;
		std::string outputBuffer;
		size_t outputSent = 0;
		std::string scratch;

		std::function<void()> onInputCallback;
		ErrorCallback onClosedCallback;
		std::function<void()> onFinishedCallback;
	};

	// Accepts on a bound, listening socket and hands each connection to a_onAccepted on the network thread.
	class NetworkListener : public NetworkWatcher {
	public:
		// Binds and listens on the calling thread (throwing std::system_error on failure), then watches from the network thread.
		static std::shared_ptr<NetworkListener> make(NetworkReactor &a_reactor, const SocketAddress &a_address, std::function<void(Socket)> a_onAccepted);

		uint16_t port() const {
			return ourPort;
		}

		SocketHandle watchedHandle() const override {
			return socket.handle();
		}
		bool wantsRead() const override {
			return socket.valid();
		}
		bool wantsWrite() const override {
			return false;
		}
		void ready(bool a_readable, bool a_writable) override;
		void abandon() override;

	private:
		NetworkListener() = default;

		Socket socket;
		uint16_t ourPort = 0;
		std::function<void(Socket)> onAccepted;
	};
}

#endif
