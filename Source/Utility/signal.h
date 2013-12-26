#ifndef __MV_SIGNAL_H__
#define __MV_SIGNAL_H__

#include <memory>
#include <utility>
#include <functional>
#include <set>

namespace MV {

	template <class T>
	class Signal {
	public:
		static std::shared_ptr< Signal<T> > make(std::function<T> a_callback){
			return std::shared_ptr< Signal<T> >(new Signal<T>(a_callback, ++uniqueId));
		}

		template <class ...Arg>
		void notify(Arg... a_parameters){
			callback(std::forward<Arg>(a_parameters)...);
		}
		template <class ...Arg>
		void operator()(Arg... a_parameters){
			callback(std::forward<Arg>(a_parameters)...);
		}

		//For sorting and comparison (removal/avoiding duplicates)
		bool operator<(const Signal<T>& a_rhs){
			return id < a_rhs.id;
		}
		bool operator>(const Signal<T>& a_rhs){
			return id > a_rhs.id;
		}
		bool operator==(const Signal<T>& a_rhs){
			return id == a_rhs.id;
		}
		bool operator!=(const Signal<T>& a_rhs){
			return id != a_rhs.id;
		}
	private:
		Signal(std::function<T> a_callback, long long a_id) :
			id(a_id),
			callback(a_callback){
		}
		std::function< T > callback;
		long long id;
		static long long uniqueId;
	};

	template <class T>
	long long Signal<T>::uniqueId = 0;

	template <class T>
	class Socket {
	public:
		//no protection against duplicates
		std::shared_ptr<Signal<T>> connect(std::function<T> a_callback){
			auto signal = Signal<T>::make(a_callback);
			observers.insert(signal);
			return signal;
		}
		//duplicate shared_ptr's will not be added
		void connect(std::shared_ptr<Signal<T>> a_value){
			observers.insert(a_value);
		}

		void disconnect(std::shared_ptr<Signal<T>> a_value){
			observers.erase(a_value);
		}

		template <class ...Arg>
		void operator()(Arg... a_parameters){
			for (auto i = observers.begin(); i != observers.end();) {
				if (i->expired()) {
					observers.erase(i++);
				} else {
					i->lock()->notify(std::forward<Arg>(a_parameters)...);
					++i;
				}
			}
		}
	private:
		std::set< std::weak_ptr< Signal<T> >, std::owner_less<std::weak_ptr<Signal<T>>> > observers;
	};

	//Can be used as a public SocketRegister member for connecting sockets to a private Socket member.
	//In this way you won't have to write forwarding connect/disconnect boilerplate for your classes.
	template <class T>
	class SocketRegister {
	public:
		SocketRegister(Socket<T> &a_socket) :
			socket(a_socket){
		}

		//no protection against duplicates
		std::shared_ptr<Signal<T>> connect(std::function<T> a_callback){
			return socket.connect(a_callback);
		}
		//duplicate shared_ptr's will not be added
		void connect(std::shared_ptr<Signal<T>> a_value){
			socket.connect(a_value);
		}

		void disconnect(std::shared_ptr<Signal<T>> a_value){
			socket.disconnect(a_value);
		}
	private:
		Socket<T> &socket;
	};

}

#endif