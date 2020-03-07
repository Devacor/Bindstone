#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <typeindex>
#include "typestring.hpp"

namespace MV {
	struct MessageHandle {
		virtual ~MessageHandle() {}
	};

	class MessageCallerNoArgCastable : public MessageHandle {
	public:
		typedef void type;

		virtual bool operator()() = 0;
	};
	template<typename F>
	class MessageCallerNoArg : public MessageCallerNoArgCastable {
	public:
		MessageCallerNoArg(F&& a_f) : f(a_f) {}
		bool operator()() override {
			if constexpr (std::is_pointer<F>::value) {
				return callMember();
			} else {
				return callLambda();
			}
		}
	private:
		bool callMember() {
			if constexpr (std::is_same<decltype((*f)()), bool>::value) {
				return (*f)();
			} else {
				(*f)();
				return true; //never self kill
			}
		}

		bool callLambda() {
			if constexpr (std::is_same<decltype(f()), bool>::value) {
				return f();
			} else {
				f();
				return true; //never self kill
			}
		}
		F f;
	};

	template<typename F>
	MessageCallerNoArg<typename std::decay<F>::type> makeMessageCallerNoArg(F&& t) {
		return { std::forward<F>(t) };
	}

	template<typename T>
	class MessageCallerCastable : public MessageHandle {
	public:
		typedef T type;

		virtual bool operator()(const T&) = 0;
	};
	template<typename F, typename T>
	class MessageCaller : public MessageCallerCastable<T> {
	public:
		MessageCaller(F&& a_f) :f(a_f) {}

		bool operator()(const T& a_value) override {
			if constexpr (std::is_pointer<F>::value) {
				return callMember(a_value);
			} else {
				return callLambda(a_value);
			}
		}
	private:
		bool callMember(const T& a_value) {
			if constexpr (std::is_same<decltype(f->operator()(a_value)), bool>::value) {
				return f->operator()(a_value);
			} else {
				f->operator()(a_value);
				return true; //never self kill
			}
		}

		bool callLambda(const T& a_value) {
			if constexpr (std::is_same<decltype(f(a_value)), bool>::value) {
				return f(a_value);
			} else {
				f(a_value);
				return true; //never self kill
			}
		}

		F f;
	};

	template<typename F, typename T>
	MessageCaller<typename std::decay<F>::type, T> makeMessageCaller(F&& t) {
		return { std::forward<F>(t) };
	}

	template<typename F>
	std::shared_ptr<MessageHandle> make_message_caller(F&& a_f) {
		return std::static_pointer_cast<MessageHandle>(std::make_shared<MessageCallerNoArg<typename std::decay<F>::type>>(std::forward<F>(a_f)));
	}
	template<typename T, typename F>
	std::shared_ptr<MessageHandle> make_message_caller(F&& a_f) {
		if constexpr (std::is_same<T, void>::value) {
			return make_message_caller(std::forward<F>(a_f));
		} else {
			return std::static_pointer_cast<MessageHandle>(std::make_shared<MessageCaller<typename std::decay<F>::type, T>>(std::forward<F>(a_f)));
		}
	}

	namespace detail {
		template<typename T>
		struct is_string : public std::disjunction<
			std::is_same<char*, typename std::decay<T>::type>,
			std::is_same<const char*, typename std::decay<T>::type>,
			std::is_same<std::string, typename std::decay<T>::type>> {
		};
	}

	class Messenger {
	private:
		struct EventKey {
			std::string key;
			std::type_index valueType;
			bool operator<(const EventKey& a_rhs) const {
				return std::tie(valueType, key) < std::tie(a_rhs.valueType, a_rhs.key);
			}
		};

		template <typename T>
		static EventKey makeKey(const std::string& a_key) {
			return { a_key, typeid(T) };
		}

	public:
		template <typename T>
		void broadcast(const std::string& a_key, const T& a_value) {
			if constexpr (detail::is_string<T>::value) { //force const char * -> std::string to enable m.broadcast("Key", "Value");
				broadcastCommon<MessageCallerCastable<std::string>>(a_key, [&](auto&& callable) -> bool { return (*callable)(a_value); });
			} else {
				broadcastCommon<MessageCallerCastable<T>>(a_key, [&](auto&& callable) -> bool { return (*callable)(a_value); });
			}
		}

		template <typename ValueType>
		void broadcast(const ValueType& a_value) {
			static_assert(!detail::is_string<ValueType>::value, "Disambiguate broadcast of a string by using either broadcastKey or broadcastValue to clarify intent.");
			broadcast(std::string(), a_value);
		}

		//Zero Argument broadcast of a specific event key.
		void broadcastKey(const std::string& a_key) {
			broadcastCommon<MessageCallerNoArgCastable>(a_key, [&](auto&& callable) -> bool { return (*callable)(); });
		}

		//One Argument broadcast of a string value to any unkeyed observers.
		void broadcastValue(const std::string& a_value) {
			broadcast(std::string(), a_value);
		}

		template <typename T, typename F>
		[[nodiscard]] std::shared_ptr<MessageHandle> observe(const std::string& a_key, F&& a_method) {
			std::lock_guard<std::recursive_mutex> guard(mutex);
			auto observer = make_message_caller<T>(std::forward<F>(a_method));
			auto key = makeKey<T>(a_key);
			if (activeKeys[key] > 0) {
				pendingObservers[key].push_back(observer);
			} else {
				observers[key].push_back(observer);
			}
			return observer;
		}
		template <typename F>
		[[nodiscard]] std::shared_ptr<MessageHandle> observe(const std::string& a_key, F&& a_method) {
			return observe<void, F>(a_key, std::forward<F>(a_method));
		}
		template <typename T, typename F>
		[[nodiscard]] std::shared_ptr<MessageHandle> observe(F&& a_method) {
			return observe<T, F>(std::string(), std::forward<F>(a_method));
		}
	private:
		template <typename T>
		struct ScopedKeyLock {
			ScopedKeyLock(const std::string& a_key, Messenger& a_owner) :
				key(makeKey<T>(a_key)),
				owner(a_owner),
				guard(a_owner.mutex),
				observerCollection(a_owner.observers[key]) {

				++owner.activeKeys[key];
			}
			~ScopedKeyLock() {
				if (--owner.activeKeys[key] == 0) {
					auto& pendingOserversRef = owner.pendingObservers[key];
					for (auto&& pendingObserver : pendingOserversRef) {
						if (!pendingObserver.expired()) {
							observerCollection.push_back(pendingObserver);
						}
					}
					pendingOserversRef.clear();
				}
			}

			EventKey key;
			Messenger& owner;
			std::lock_guard<std::recursive_mutex> guard;
			std::vector<std::weak_ptr<MessageHandle>>& observerCollection;
		};

		template <typename T>
		friend struct ScopedKeyLock;

		template <typename T, typename F>
		void broadcastCommon(const std::string& a_key, F&& a_invokeCaller) {
			ScopedKeyLock<typename T::type> scopedLock(a_key, *this);
			scopedLock.observerCollection.erase(std::remove_if(scopedLock.observerCollection.begin(), scopedLock.observerCollection.end(), [&](auto weakObserver) {
				if (auto observer = weakObserver.lock()) {
					return !a_invokeCaller(std::static_pointer_cast<T>(observer));
				} else {
					return true;
				}
			}), scopedLock.observerCollection.end());
		}

		std::recursive_mutex mutex;
		std::map<EventKey, int> activeKeys;
		std::map<EventKey, std::vector<std::weak_ptr<MessageHandle>>> observers;
		std::map<EventKey, std::vector<std::weak_ptr<MessageHandle>>> pendingObservers;
	};

	struct KeyPairComparer {};

	template <typename KeyParam, typename T = void>
	struct KeyPair : public KeyPairComparer {
		static const std::string & key() noexcept { return KeyParam::value(); }
		typedef T type;
	};

	template <typename DerivedType, typename ... ObservableTypes>
	class MessengerObserver {
	public:
		MessengerObserver(MV::Messenger& a_m) {
			autoObserve<ObservableTypes...>(a_m);
		}
		virtual ~MessengerObserver() {}

	protected:
		//Allow manual connection
		void addHandle(const std::shared_ptr<MV::MessageHandle>& a_handle) {
			handles.push_back(a_handle);
		}
	private:
		template <int = 0>
		void autoObserve(MV::Messenger& a_m) {}

		template <typename T, typename... Ts>
		void autoObserve(MV::Messenger& a_m) {
			if constexpr (std::is_base_of<KeyPairComparer, T>::value) {
				pushKeyComparerType<T>(a_m);
			} else {
				handles.push_back(a_m.observe<T>(static_cast<DerivedType*>(this)));
			}
			autoObserve<Ts...>(a_m);
		}

		template <typename T>
		void pushKeyComparerType(MV::Messenger& a_m) {
			if constexpr (std::is_same<typename T::type, void>::value) {
				handles.push_back(a_m.observe(T::key(), static_cast<DerivedType*>(this)));
			} else {
				handles.push_back(a_m.observe<typename T::type>(T::key(), static_cast<DerivedType*>(this)));
			}
		}

		std::vector<std::shared_ptr<MV::MessageHandle>> handles;
	};
}
