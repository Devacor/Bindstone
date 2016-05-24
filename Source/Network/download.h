#ifndef _MV_DOWNLOAD_H_
#define _MV_DOWNLOAD_H_

#include <string>
#include <iostream>
#include <istream>
#include <ostream>
#include <fstream>
#include <algorithm>
#include "Network/url.h"
#include "Utility/generalUtility.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>

namespace MV {
	struct HttpHeader {
		std::string version;
		int status = 0;
		std::string message;
		std::map<std::string, std::string> values;

		std::vector<std::string> bounces;

		bool success = false;
		std::string errorMessage;

		size_t contentLength;

		HttpHeader() {
		}

		HttpHeader(std::istream& response_stream) {
			read(response_stream);
		}

		void read(std::istream& response_stream);
	};


	inline std::ostream& operator<<(std::ostream& os, const HttpHeader& obj) {
		os << "\\/______HTTP_HEADER______\\/\nVersion [" << obj.version << "] Status [" << obj.status << "] Message [" << obj.message << "]\n";
		os << "||-----------------------||\n";
		for (auto&& kvp : obj.values) {
			os << "[" << kvp.first << "]: " << kvp.second << "\n";
		}
		os << "\n||--------Bounces--------||\n";
		for (size_t i = 0; i < obj.bounces.size(); ++i) {
			os << i << ": " << obj.bounces[i] << "\n";
		}
		os << "/\\_______________________/\\" << std::endl;
		return os;
	}
	inline std::istream& operator>>(std::istream& a_is, HttpHeader& a_obj) {
		a_obj.read(a_is);
		return a_is;
	}

	class DownloadRequest : public std::enable_shared_from_this<DownloadRequest> {
	public:
		static std::shared_ptr<DownloadRequest> make(const MV::Url& a_url, const std::shared_ptr<std::ostream> &a_streamOutput) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_streamOutput));
			result->perform(a_url);
			return result;
		}

		//onComplete is called on success or error at the end of the download.
		static std::shared_ptr<DownloadRequest> make(const std::shared_ptr<boost::asio::io_service> &a_ioService, const MV::Url& a_url, const std::shared_ptr<std::ostream> &a_streamOutput, std::function<void (std::shared_ptr<DownloadRequest>)> a_onComplete) {
			auto result = std::shared_ptr<DownloadRequest>(new DownloadRequest(a_streamOutput));
			result->onComplete = a_onComplete;
			result->ioService = a_ioService;
			result->perform(a_url);
			return result;
		}

		HttpHeader& header() {
			return headerData;
		}

		MV::Url finalUrl() {
			return currentUrl;
		}

		MV::Url inputUrl() {
			return originalUrl;
		}

	private:
		DownloadRequest(const std::shared_ptr<std::ostream> &a_streamOutput) :
			streamOutput(a_streamOutput) {
		}

		void perform(const MV::Url& a_url);

		bool initializeSocket();

		void initiateRequest(const MV::Url& a_url);

		void handleResolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleConnect(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
		void handleWriteRequest(const boost::system::error_code& err);
		void handleReadHeaders(const boost::system::error_code& err);
		void handleReadContent(const boost::system::error_code& err);

		void readResponseToStream() {
			(*streamOutput) << &(*response);
		}

		std::shared_ptr<boost::asio::io_service> ioService;
		std::unique_ptr<boost::asio::ip::tcp::resolver> resolver;
		std::unique_ptr<boost::asio::ip::tcp::socket> socket;

		std::unique_ptr<std::istream> responseStream;

		std::unique_ptr<boost::asio::streambuf> request;
		std::unique_ptr<boost::asio::streambuf> response;

		std::shared_ptr<std::ostream> streamOutput;

		HttpHeader headerData;

		MV::Url currentUrl;
		MV::Url originalUrl;

		std::function<void(std::shared_ptr<DownloadRequest>)> onComplete;
	};

	std::string DownloadString(const MV::Url& a_url);

	HttpHeader DownloadFile(const MV::Url& a_url, const std::string &a_path);
	void DownloadFile(const std::shared_ptr<boost::asio::io_service> &a_ioService, const MV::Url& a_url, const std::string &a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete = std::function<void(std::shared_ptr<DownloadRequest>)>());

	void DownloadFiles(const std::vector<MV::Url>& a_url, const std::string &a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete = std::function<void(std::shared_ptr<DownloadRequest>)>());
	void DownloadFiles(const std::shared_ptr<boost::asio::io_service> &a_ioService, const std::vector<MV::Url>& a_url, const std::string &a_path, std::function<void(std::shared_ptr<DownloadRequest>)> a_onComplete = std::function<void(std::shared_ptr<DownloadRequest>)>(), std::function<void()> a_onAllComplete = std::function<void()>());
}
#endif