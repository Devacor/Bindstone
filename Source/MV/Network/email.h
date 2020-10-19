#ifndef _MV_EMAIL_H_
#define _MV_EMAIL_H_

#include <iostream>
#include <string>
#include <memory>
#include "MV/Utility/stringUtility.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace MV {
	
	struct SmtpResponse {
		int status = 0;
		std::string message;
		std::vector<std::string> lines;

		SmtpResponse() {
		}

		SmtpResponse(std::istream& response_stream) {
			read(response_stream);
		}

		void read(std::istream& response_stream) {
			lines.clear();
			message.clear();
			std::string status_code;
			response_stream >> status_code;
			try {
				status = std::stoi(status_code);
			} catch (...) {
				status = 0;
			}

			getline_platform_agnostic(response_stream, message);
			info(status_code, message);
			std::string line;
			while (getline_platform_agnostic(response_stream, line) && !line.empty()) {
				lines.push_back(line);
				info("L: ", line);
			}
		}
	};

	class Email : public std::enable_shared_from_this<Email> {
	public:
		struct Credentials {
			Credentials(const std::string &a_name, const std::string &a_password) :
				name(a_name),
				password(a_password) {
			}

			std::string name;
			std::string password;
		};

		struct Addresses {
			Addresses(){}

			Addresses(const std::string &a_from, const std::string &a_to) :
				from(a_from),
				to(a_to) {

				sanitize();
			}

			Addresses(const std::string &a_fromName, const std::string &a_from, const std::string &a_toName, const std::string &a_to) :
				from(a_from),
				fromName(a_fromName),
				to(a_to),
				toName(a_toName){

				sanitize();
			}

			std::string from;
			std::string fromName;
			std::string to;
			std::string toName;

		private:
			void sanitize() {
				from = strip(from);
				to = strip(to);
				fromName = strip(fromName);
				toName = strip(toName);
			}

			std::string strip(std::string a_original) {
				a_original.erase(std::remove_if(a_original.begin(), a_original.end(), [](char c) {return c == '\n' || c == '\r' || c == '\0' || c == '<' || c == '>' || c == '"'; }), a_original.end());
				return a_original;
			}
		};

		static std::shared_ptr<Email> make(const std::string &a_host, const std::string &a_port, const Credentials &a_credentials) {
			auto result = std::shared_ptr<Email>(new Email(a_host, a_port, a_credentials));
			return result;
		}

		static std::shared_ptr<Email> make(const std::shared_ptr<boost::asio::io_context> &a_suppliedService, const std::string &a_host, const std::string &a_port, const Credentials &a_credentials, MainThreadCallback a_onFinish = MainThreadCallback()) {
			auto result = std::shared_ptr<Email>(new Email(a_host, a_port, a_credentials));
			result->onFinish = a_onFinish;
			result->ioService = a_suppliedService;
			return result;
		}

		void send(const Addresses &a_addresses, const std::string &a_title, const std::string &a_message) {
			addresses = a_addresses;
			try {
				bool needToCallRun = initializeSocket();
				storeMessageString(a_title, a_message);
				initiateConnection();
				if (needToCallRun) {
					ioService->run();
				}
			} catch (...) {
				alertFailure();
			}
		}

		int failed() {
			std::lock_guard<std::mutex> guard(lock);
			return done && !success;
		}

		SmtpResponse lastResponse() {
			return activeResponse;
		}

		bool succeeded() {
			std::lock_guard<std::mutex> guard(lock);
			return done && success;
		}

		bool complete() {
			std::lock_guard<std::mutex> guard(lock);
			return done;
		}

	private:
		Email(const std::string& a_host, const std::string &a_port, const Credentials &a_credentials) :
			credentials(a_credentials),
			host(a_host),
			port(a_port){
		}

		//returns true if we own our own ioService created
		bool initializeSocket() {
			bool created = false;
			if (!ioService) {
				ioService = std::make_shared<boost::asio::io_context>();
				created = true;
			}

			resolver = std::make_unique<boost::asio::ip::tcp::resolver>(*ioService);
			tlsContext = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv13_client);
			socket = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(*ioService, *tlsContext);

			return created;
		}

		void storeMessageString(const std::string &a_title, const std::string &a_message) {
			std::stringstream output;
			output << "Subject: " << a_title << "\r\n";
			output << "From: " << (addresses.fromName.empty() ? "" : ("\"" + addresses.fromName + "\"")) << " <" << addresses.from << ">\r\n";
			output << "To: " << (addresses.toName.empty() ? "" : ("\"" + addresses.toName + "\"")) << " <" << addresses.to << ">\r\n";
			output << "\r\n";
			output << a_message;
			output << "\r\n" << "." << "\r\n";
			message = output.str();
		}

		std::string domainFromEmail(const std::string &a_email) {
			auto atLocation = a_email.find('@');
			if (atLocation < (a_email.size() + 1) && atLocation > 0) {
				return a_email.substr(atLocation + 1, a_email.size() - (atLocation + 1));
			}
			return "";
		}

		void initiateConnection() {
			request = std::make_unique<boost::asio::streambuf>();
			response = std::make_unique<boost::asio::streambuf>();
			using boost::asio::ip::tcp;

			tcp::resolver::query query(host, port);
			resolver->async_resolve(query, std::bind(&Email::handleResolve, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
		}

		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
			if (!err) {
				// Attempt a connection to the first endpoint in the list. Each endpoint
				// will be tried until we successfully establish a connection.
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket->lowest_layer().async_connect(endpoint, std::bind(&Email::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			} else {
				activeResponse.message = err.message();
				alertFailure();
			}
		}

		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
			if (!err) {
				smtpHandshake();
			} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
				// The connection failed. Try the next endpoint in the list.
				socket->lowest_layer().close();
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket->lowest_layer().async_connect(endpoint, std::bind(&Email::handleConnect, shared_from_this(), std::placeholders::_1, ++endpoint_iterator));
			} else {
				activeResponse.message = err.message();
				alertFailure();
			}
		}

		void smtpHandshake() {
			auto self = shared_from_this();
			listenForMessageUnsecure({220}, [this, self] {
				sendInternalUnsecure("EHLO " + domainFromEmail(addresses.from) + "\r\n", {250}, [this, self] {
					sendInternalUnsecure("STARTTLS\r\n", { 220 }, [this, self] {
						socket->async_handshake(boost::asio::ssl::stream_base::client, [this, self](const boost::system::error_code& a_err) {
							if (!a_err) {
								secureLogin();
							}
						});
					});
				});
			});
		}

		void secureLogin() {
			auto self = shared_from_this();
			sendInternal("EHLO " + domainFromEmail(addresses.from) + "\r\n", {250}, [this, self] {
				sendInternal("AUTH LOGIN\r\n", { 334 }, [this, self] {
					sendInternal(cerealEncodeWrapper(credentials.name) + "\r\n", { 334 }, [this, self] {
						sendInternal(cerealEncodeWrapper(credentials.password) + "\r\n", { 235 }, [this, self] {
							sendMessageAndQuit();
						});
					});
				});
			});
		}

		void sendMessageAndQuit() {
			auto self = shared_from_this();
			sendInternal("MAIL FROM:<" + addresses.from + ">\r\n", {250}, [this, self] {
				sendInternal("RCPT TO:<" + addresses.to + ">\r\n", {250, 251}, [this, self] {
					sendInternal("DATA\r\n", {354}, [this, self] {
						sendInternal(message, { 250 }, [this, self] {
							std::lock_guard<std::mutex> guard(lock);
							success = true;
							done = true;
							onFinish();
							sendInternal("QUIT\r\n", {}, [this, self] {
								info("Email Complete [", addresses.from, "] -> [", addresses.to, "]");
							});
						});
					});
				});
			});
		}

		void listenForMessage(std::vector<int> a_validResponses, std::function<void ()> a_callback) {
			auto self = shared_from_this();
			boost::asio::async_read_until(*socket, *response, "\r\n", [this, a_validResponses, self, a_callback](const boost::system::error_code& a_err, size_t a_amount) {
				if (!a_err) {
					responseStream = std::make_unique<std::istream>(&(*response));
					activeResponse.read(*responseStream);
					if (a_validResponses.empty() || std::find(a_validResponses.begin(), a_validResponses.end(), activeResponse.status) != a_validResponses.end()) {
						if (a_callback) { a_callback(); }
					} else if(!a_validResponses.empty()) {
						alertFailure();
					}
				} else {
					activeResponse.message = a_err.message();
					alertFailure();
				}
			});
		}

		void alertFailure() {
			std::lock_guard<std::mutex> guard(lock);
			success = false;
			done = true;
			onFinish();
		}

		void sendInternal(const std::string &a_content, std::vector<int> a_validResponses = std::vector<int>(), std::function<void()> a_callback = std::function<void()>()) {
			auto self = shared_from_this();
			ioService->post([=] {
				self; //force capture
				boost::asio::async_write(*socket, boost::asio::buffer(a_content, a_content.size()), [this,a_validResponses,self,a_callback](boost::system::error_code a_err, size_t a_amount) {
					if (!a_err) {
						if (a_callback) {
							listenForMessage(a_validResponses, a_callback);
						}
					} else {
						activeResponse.message = a_err.message();
						alertFailure();
					}
				});
			});
		}

		void listenForMessageUnsecure(std::vector<int> a_validResponses, std::function<void()> a_callback) {
			auto self = shared_from_this();
			boost::asio::async_read_until(socket->next_layer(), *response, "\r\n", [this, self, a_validResponses, a_callback](const boost::system::error_code& a_err, size_t a_amount) {
				if (!a_err) {
					responseStream = std::make_unique<std::istream>(&(*response));
					activeResponse.read(*responseStream);
					if (std::find(a_validResponses.begin(), a_validResponses.end(), activeResponse.status) != a_validResponses.end()) {
						a_callback();
					} else if (!a_validResponses.empty()) {
						alertFailure();
					}
				} else {
					activeResponse.message = a_err.message();
					alertFailure();
				}
			});
		}

		void sendInternalUnsecure(const std::string &a_content, std::vector<int> a_validResponses = std::vector<int>(), std::function<void()> a_callback = std::function<void()>()) {
			ioService->post([=] {
				auto self = shared_from_this();

				boost::asio::async_write(socket->next_layer(), boost::asio::buffer(a_content, a_content.size()), [this, a_validResponses, self, a_callback](boost::system::error_code a_err, size_t a_amount) {
					if (!a_err) {
						if (a_callback) {
							listenForMessageUnsecure(a_validResponses, a_callback);
						}
					} else {
						activeResponse.message = a_err.message();
						alertFailure();
					}
				});
			});
		}

		std::shared_ptr<boost::asio::io_context> ioService;
		std::unique_ptr<boost::asio::ip::tcp::resolver> resolver;
		//boost::asio::ip::tcp::socket socket;

		std::unique_ptr<boost::asio::ssl::context> tlsContext;
		std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket;

		std::unique_ptr<std::istream> responseStream;

		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> response;

		std::mutex lock;

		bool success = false;
		bool done = false;
		std::string errorMessage;



		SmtpResponse activeResponse;

		Addresses addresses;
		std::string message;

		Credentials credentials;
		
		std::string host;
		std::string port;

		MainThreadCallback onFinish;
	};

}

#endif
