/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _MV_NETWORK_H_
#define _MV_NETWORK_H_

#include <boost/asio.hpp>
#include <iostream>
#include <thread>

namespace MV {
	class Client {
	public:
		Client(std::string &a_hostName):
			socket(ioService){
			std::thread t([&](){ ioService.run(); });

			/*boost::asio::ip::tcp::resolver 

			connect();*/
		}
	private:
		void connect(boost::asio::ip::tcp::resolver::iterator a_endpointIterator)
		{
			/*boost::asio::async_connect(socket, a_endpointIterator, [this](boost::system::error_code errorCode, boost::asio::ip::tcp::resolver::iterator){
				if (!errorCode){
					
				}
			});*/
		}
		boost::asio::io_service ioService;
		boost::asio::ip::tcp::socket socket;
	};

};

#endif
