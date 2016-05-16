#ifndef _MV_EMAIL_H_
#define _MV_EMAIL_H_

#define OPENSSL_NO_SSL2

#include <iostream>
#include <string>
#include <memory>
#include "Utility/generalUtility.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/bind.hpp>

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
			std::cout << status_code <<  message << std::endl;
			std::string line;
			while (getline_platform_agnostic(response_stream, line) && !line.empty()) {
				lines.push_back(line);
				std::cout << "L:" << line << std::endl;
			}
		}
	};

	class Email : public std::enable_shared_from_this<Email> {
	public:
		static std::shared_ptr<Email> make(const std::string& a_from, const std::string &a_to) {
			return std::shared_ptr<Email>(new Email(a_from, a_to));
		}

		void send(const std::string &a_title, const std::string &a_message) {
			try {
				auto toDomain = domainFromEmail(to);
				if (!toDomain.empty()) {
					storeMessageString(a_title, a_message);
					initiateRequest(toDomain);
					ioService.run();
				} else {
					success = false;
					errorMessage = "Malformed email address!";
					std::cerr << errorMessage << std::endl;
				}
			} catch (...) {
				success = false;
				errorMessage = "Exception thrown to top level.";
				std::cerr << errorMessage << std::endl;
			}
		}


	private:
		Email(const std::string& a_from, const std::string &a_to) :
			resolver(ioService),
			from(a_from),
			to(a_to),
			tlsContext(ioService, boost::asio::ssl::context::tlsv1_client),
			socket(ioService, tlsContext){
		}

		void storeMessageString(const std::string &a_title, const std::string &a_message) {
			std::stringstream output;
			output << "Subject: " << a_title << "\r\n";
			output << "From: " << from << "\r\n";
			output << "To: " << to << "\r\n";
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

		void initiateRequest(const std::string& a_domain) {
			request = std::make_unique<boost::asio::streambuf>();
			response = std::make_unique<boost::asio::streambuf>();
			using boost::asio::ip::tcp;

			tcp::resolver::query query("email-smtp.us-west-2.amazonaws.com", "587");
			resolver.async_resolve(query, boost::bind(&Email::handleResolve, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
			if (!err) {
				// Attempt a connection to the first endpoint in the list. Each endpoint
				// will be tried until we successfully establish a connection.
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket.lowest_layer().async_connect(endpoint, boost::bind(&Email::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
			} else {
				success = false;
				errorMessage = "Download Resolve Failure: " + err.message();
				std::cerr << errorMessage << std::endl;
			}
		}

		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
			if (!err) {
				smtpHandshake();
			} else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
				// The connection failed. Try the next endpoint in the list.
				socket.lowest_layer().close();
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket.lowest_layer().async_connect(endpoint, boost::bind(&Email::handleConnect, this, boost::asio::placeholders::error, ++endpoint_iterator));
			} else {
				success = false;
				errorMessage = "Download Connection Failure: " + err.message();
				std::cerr << errorMessage << std::endl;
			}
		}

		void smtpHandshake() {
			listenForMessageUnsecure([=] {
				if (activeResponse.status == 220) {
					sendInternalUnsecure("EHLO " + domainFromEmail(from) + "\r\n", [=] { listenForMessageUnsecure([=] {
						if (activeResponse.status == 250) {
							sendInternalUnsecure("STARTTLS\r\n", [=] { listenForMessageUnsecure([=] {
								if (activeResponse.status == 220) {
									socket.handshake(boost::asio::ssl::stream_base::client);
									sendInternal("EHLO " + domainFromEmail(from) + "\r\n", [=] {
										if (activeResponse.status == 250) {
											sendInternal("AUTH LOGIN\r\n", [=] {
												if (activeResponse.status == 334) {
													sendInternal(cereal::base64::encode("AKIAIVINRAMKWEVUT6UQ")+ "\r\n", [=] {
														if (activeResponse.status == 334) {
															sendInternal(cereal::base64::encode("AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw") + "\r\n", [=] {
																if (activeResponse.status == 235) {
																	sendInternal("MAIL FROM:<" + from + ">\r\n", [=] {
																		if (activeResponse.status == 250) {
																			sendInternal("RCPT TO:<" + to + ">\r\n", [=] {
																				if (activeResponse.status == 500) {
																					std::cerr << "Bad address." << std::endl;
																				} else if (activeResponse.status == 250 || activeResponse.status == 251) {
																					sendMessageAndCloseConnection();
																				}
																			});
																		}
																	});
																}
															});
														}
													});
												} else { std::cerr << "ERROR: 4" << std::endl; }
											});
										} else { std::cerr << "ERROR: 3" << std::endl; }
									});
								} else { std::cerr << "ERROR: 2" << std::endl; }
							}); });
						} else { std::cerr << "ERROR: 1" << std::endl; }
					}); });
				} else { std::cerr << "ERROR: 0" << std::endl; }
			});
		}

		void sendMessageAndCloseConnection() {
			sendInternal("DATA\r\n", [=] {
				if (activeResponse.status == 354) {
					sendInternal(message, [=] {
						if (activeResponse.status == 250) {
							sendInternal("QUIT\r\n");
						}
					});
				}
			});
		}

		void listenForMessage(std::function<void ()> a_callback) {
			auto self = shared_from_this();
			boost::asio::async_read_until(socket, *response, "\r\n", [&, self, a_callback](const boost::system::error_code& a_err, size_t a_amount) {
				if (!a_err) {
					responseStream = std::make_unique<std::istream>(&(*response));
					activeResponse.read(*responseStream);
					a_callback();
				}
			});
		}

		void sendInternal(const std::string &a_content, std::function<void()> a_callback = std::function<void()>()) {
			ioService.post([=] {
				auto self = shared_from_this();
				
				boost::asio::async_write(socket, boost::asio::buffer(a_content, a_content.size()), [&,self,a_callback](boost::system::error_code a_err, size_t a_amount) {
					if (a_err) {
						success = false;
						errorMessage = "Email Connection Failure: " + a_err.message();
						std::cerr << errorMessage << std::endl;
					} else {
						if (a_callback) {
							listenForMessage(a_callback);
						}
					}
				});
			});
		}

		void listenForMessageUnsecure(std::function<void()> a_callback) {
			auto self = shared_from_this();
			boost::asio::async_read_until(socket.next_layer(), *response, "\r\n", [&, self, a_callback](const boost::system::error_code& a_err, size_t a_amount) {
				if (!a_err) {
					responseStream = std::make_unique<std::istream>(&(*response));
					activeResponse.read(*responseStream);
					a_callback();
				}
			});
		}

		void sendInternalUnsecure(const std::string &a_content, std::function<void()> a_callback = std::function<void()>()) {
			ioService.post([=] {
				auto self = shared_from_this();

				boost::asio::async_write(socket.next_layer(), boost::asio::buffer(a_content, a_content.size()), [&, self, a_callback](boost::system::error_code a_err, size_t a_amount) {
					if (a_err) {
						success = false;
						errorMessage = "Email Connection Failure: " + a_err.message();
						std::cerr << errorMessage << std::endl;
					} else {
						if (a_callback) {
							a_callback();
						}
					}
				});
			});
		}

		boost::asio::io_service ioService;
		boost::asio::ip::tcp::resolver resolver;
		//boost::asio::ip::tcp::socket socket;

		boost::asio::ssl::context tlsContext;
		boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket;

		std::unique_ptr<std::istream> responseStream;

		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> response;

		bool success = false;
		std::string errorMessage;

		SmtpResponse activeResponse;

		std::string from;
		std::string to;

		std::string message;
	};

}

#endif
