#pragma once

#ifndef __JAISCRIPT_SIGNALS_SIGNAL_HPP__
#define __JAISCRIPT_SIGNALS_SIGNAL_HPP__

#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <type_traits>

#ifndef JAI_SIGNALS_NO_THREADSAFE
#include <mutex>
#endif

namespace jai {

// ============================================================================
// Thread Safety Support
// ============================================================================
// Thread safety is ENABLED by default. All connect/disconnect/emit operations
// are protected by a recursive_mutex to allow callbacks to modify the signal.
//
// Define JAI_SIGNALS_NO_THREADSAFE to disable mutex protection (for single-threaded
// scenarios where you want maximum performance).

#ifndef JAI_SIGNALS_NO_THREADSAFE
#define JAI_SIGNAL_LOCK() std::lock_guard<std::recursive_mutex> _signal_lock(mutex_)
#else
#define JAI_SIGNAL_LOCK() ((void)0)
#endif

// Forward declarations
class engine;
template<typename T> class signal_emitter;

// ============================================================================
// receiver<T> - Holds a callback (C++ function or script string)
// ============================================================================
//
// A receiver wraps either a C++ callback or a script string that gets
// evaluated when the signal fires. Receivers can be blocked/unblocked
// and support both notify (void return) and predicate (bool return) modes.
//
// Usage:
//   auto recv = jai::receiver<void(int)>::make([](int x) { std::cout << x; });
//   recv->notify(42);
//
//   auto script_recv = jai::receiver<void(int)>::make("print(x)", &engine, {"x"});
//   script_recv->notify(42);

template<typename T>
class receiver {
public:
	using function_type = std::function<T>;
	using shared_type = std::shared_ptr<receiver<T>>;
	using weak_type = std::weak_ptr<receiver<T>>;

	// Factory methods
	static std::shared_ptr<receiver<T>> make(std::function<T> callback) {
		return std::shared_ptr<receiver<T>>(new receiver<T>(std::move(callback), ++unique_id_));
	}

	static std::shared_ptr<receiver<T>> make(
		const std::string& script,
		engine* eng,
		const std::shared_ptr<std::vector<std::string>>& parameter_names = nullptr
	) {
		return std::shared_ptr<receiver<T>>(new receiver<T>(script, eng, parameter_names, ++unique_id_));
	}

	// Notify with arguments (void return)
	template<typename... Args>
	void notify(Args&&... args) {
		if (!blocked() && !invalid()) {
			if (script_callback_.empty()) {
				callback_(std::forward<Args>(args)...);
			} else {
				call_script(std::forward<Args>(args)...);
			}
		}
	}

	// Predicate with arguments (bool return)
	template<typename... Args>
	bool predicate(Args&&... args) {
		if (!blocked() && !invalid()) {
			if (script_callback_.empty()) {
				return callback_(std::forward<Args>(args)...);
			} else {
				return call_script_predicate(std::forward<Args>(args)...);
			}
		}
		return true;  // Default: continue propagation
	}

	// Notify without arguments
	void notify() {
		if (!blocked() && !invalid()) {
			if (script_callback_.empty()) {
				callback_();
			} else {
				call_script();
			}
		}
	}

	// Predicate without arguments
	bool predicate() {
		if (!blocked() && !invalid()) {
			if (script_callback_.empty()) {
				return callback_();
			} else {
				return call_script_predicate();
			}
		}
		return true;  // Default: continue propagation
	}

	// Function call operator (alias for notify)
	template<typename... Args>
	void operator()(Args&&... args) {
		notify(std::forward<Args>(args)...);
	}

	void operator()() {
		notify();
	}

	// Check if receiver is invalid (no callback set)
	bool invalid() const {
		return script_callback_.empty() && !callback_;
	}

	// Check if receiver is still connected to a signal
	bool connected() const {
		return !owner_signal_.expired();
	}

	// Block/unblock the receiver
	void block() { ++is_blocked_; }

	void unblock() {
		--is_blocked_;
		if (is_blocked_ < 0) {
			is_blocked_ = 0;
		}
	}

	bool blocked() const { return is_blocked_ != 0; }

	// One-shot flag
	bool is_oneshot() const { return oneshot_; }

	// Comparison operators for sorting/removal
	bool operator<(const receiver<T>& rhs) const { return id_ < rhs.id_; }
	bool operator>(const receiver<T>& rhs) const { return id_ > rhs.id_; }
	bool operator==(const receiver<T>& rhs) const { return id_ == rhs.id_; }
	bool operator!=(const receiver<T>& rhs) const { return id_ != rhs.id_; }

	// Script accessors
	const std::string& script() const { return script_callback_; }
	bool has_script() const { return !script_callback_.empty(); }

	void script_engine(engine* eng) { script_engine_ = eng; }
	engine* script_engine() const { return script_engine_; }

	const std::shared_ptr<std::vector<std::string>>& parameter_names() const {
		return ordered_parameter_names_;
	}

	int64_t id() const { return id_; }

private:
	// Default constructor for serialization
	receiver() : id_(0) {}

	// C++ callback constructor
	receiver(std::function<T> callback, int64_t id)
		: callback_(std::move(callback))
		, id_(id) {}

	// Script callback constructor
	receiver(
		const std::string& script,
		engine* eng,
		const std::shared_ptr<std::vector<std::string>>& parameter_names,
		int64_t id
	)
		: id_(id)
		, script_callback_(script)
		, script_engine_(eng)
		, ordered_parameter_names_(parameter_names) {}

	// Script execution - implemented in signal_impl.hpp (needs engine definition)
	template<typename... Args>
	void call_script(Args&&... args);

	template<typename... Args>
	bool call_script_predicate(Args&&... args);

	void call_script();
	bool call_script_predicate();

	int is_blocked_ = 0;
	bool oneshot_ = false;
	std::function<T> callback_;
	std::string script_callback_;
	std::shared_ptr<std::vector<std::string>> ordered_parameter_names_;
	engine* script_engine_ = nullptr;
	std::weak_ptr<void> owner_signal_;  // Weak ref to owning signal for connected() check

	int64_t id_;
	static int64_t unique_id_;

	// Allow signal_emitter to access private members
	template<typename U> friend class signal_emitter;
};

template<typename T>
int64_t receiver<T>::unique_id_ = 0;


// ============================================================================
// signal_emitter<T> - Multi-receiver signal dispatcher (private/internal use)
// ============================================================================
//
// The signal_emitter owns the observer list and can emit signals.
// Typically used as a private member, with a public `signal<T>` wrapper.
//
// Features:
//   - Insertion-order iteration (vector-based)
//   - One-shot connections (auto-disconnect after first fire)
//   - Short-circuit emission (stop on predicate false)
//   - Weak reference observers (auto-cleanup when receiver dies)
//
// Usage:
//   class Button {
//   private:
//       jai::signal_emitter<void(int, int)> on_click_;
//   public:
//       jai::signal<void(int, int)> on_click{on_click_};
//
//       void click(int x, int y) { on_click_(x, y); }
//   };

template<typename T>
class signal_emitter {
public:
	using function_type = std::function<T>;
	using receiver_type = receiver<T>;
	using shared_receiver_type = std::shared_ptr<receiver<T>>;
	using weak_receiver_type = std::weak_ptr<receiver<T>>;

	signal_emitter() : prevent_shared_ownership_(std::make_shared<char>()) {}
	~signal_emitter() = default;

	// Non-copyable (prevent accidental copies of signal state)
	signal_emitter(const signal_emitter&) = delete;
	signal_emitter& operator=(const signal_emitter&) = delete;

	// Moveable
	signal_emitter(signal_emitter&&) = default;
	signal_emitter& operator=(signal_emitter&&) = default;

	// Connect a C++ callback (appends to end - last to be called)
	[[nodiscard]]
	shared_receiver_type connect(std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(std::move(callback));
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	// Connect a script callback
	[[nodiscard]]
	shared_receiver_type connect(const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(script_callback, script_engine_, ordered_parameter_names_);
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	// Connect one-shot (auto-disconnects after first fire)
	[[nodiscard]]
	shared_receiver_type connect_oneshot(std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(std::move(callback));
		recv->oneshot_ = true;
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	// Connect one-shot script callback
	[[nodiscard]]
	shared_receiver_type connect_oneshot(const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(script_callback, script_engine_, ordered_parameter_names_);
		recv->oneshot_ = true;
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	// Connect an existing receiver
	bool connect(shared_receiver_type recv) {
		JAI_SIGNAL_LOCK();
		if (recv) {
			recv->owner_signal_ = prevent_shared_ownership_;
			observers_.push_back(recv);
			return true;
		}
		return false;
	}

	// Connect with named ID (owned connection - persists for signal lifetime)
	shared_receiver_type connect(const std::string& id, std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = connect(std::move(callback));
		owned_connections_[id] = recv;
		return recv;
	}

	shared_receiver_type connect(const std::string& id, const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = connect(script_callback);
		owned_connections_[id] = recv;
		return recv;
	}

	// Get owned connection by ID
	shared_receiver_type connection(const std::string& id) {
		JAI_SIGNAL_LOCK();
		auto it = owned_connections_.find(id);
		return it != owned_connections_.end() ? it->second : shared_receiver_type();
	}

	// Check if owned connection exists
	bool connected(const std::string& id) const {
		JAI_SIGNAL_LOCK();
		return owned_connections_.find(id) != owned_connections_.end();
	}

	// Number of live observers
	size_t observer_count() const {
		JAI_SIGNAL_LOCK();
		size_t count = 0;
		for (const auto& weak_obs : observers_) {
			if (!weak_obs.expired()) ++count;
		}
		return count;
	}

	// Disconnect a receiver
	void disconnect(shared_receiver_type recv) {
		JAI_SIGNAL_LOCK();
		if (recv) {
			recv->owner_signal_.reset();
			if (!in_call_) {
				remove_observer(recv);
			} else {
				disconnect_queue_.push_back(recv);
			}
		}
	}

	// Disconnect by ID
	void disconnect(const std::string& id) {
		JAI_SIGNAL_LOCK();
		auto it = owned_connections_.find(id);
		if (it != owned_connections_.end()) {
			disconnect_unlocked(it->second);
			owned_connections_.erase(it);
		}
	}

	// Clear all observers
	void clear() {
		JAI_SIGNAL_LOCK();
		for (auto& weak_obs : observers_) {
			if (auto obs = weak_obs.lock()) {
				obs->owner_signal_.reset();
			}
		}
		owned_connections_.clear();
		if (!in_call_) {
			observers_.clear();
		} else {
			// Queue all for removal
			for (auto& weak_obs : observers_) {
				if (auto obs = weak_obs.lock()) {
					disconnect_queue_.push_back(obs);
				}
			}
		}
	}

	// Block/unblock the signal
	void block() {
		if (is_blocked_++ == 0) {
			called_while_blocked_ = false;
		}
	}

	bool unblock() {
		if (--is_blocked_ == 0) {
			return called_while_blocked_;
		}
		if (is_blocked_ < 0) {
			is_blocked_ = 0;
		}
		return false;
	}

	bool blocked() const { return is_blocked_ != 0; }

	// Emit signal with arguments
	template<typename... Args>
	void operator()(Args&&... args) {
		emit_impl<false>(std::forward<Args>(args)...);
	}

	// Emit signal without arguments
	void operator()() {
		emit_impl_noargs<false>();
	}

	// Emit method (alias for operator())
	template<typename... Args>
	void emit(Args&&... args) {
		emit_impl<false>(std::forward<Args>(args)...);
	}

	// Emit with short-circuit: stops if any receiver returns false
	// Only available for signals where receivers return bool
	template<typename... Args>
	bool emit_while(Args&&... args)
		requires std::is_same_v<typename std::function<T>::result_type, bool>
	{
		return emit_impl<true>(std::forward<Args>(args)...);
	}

	bool emit_while()
		requires std::is_same_v<typename std::function<T>::result_type, bool>
	{
		return emit_impl_noargs<true>();
	}

	// Remove dead observers and return remaining count
	size_t cull_dead_observers() {
		JAI_SIGNAL_LOCK();
		return cull_dead_observers_unlocked();
	}

	// Script engine configuration
	signal_emitter& script_engine(engine* eng) {
		script_engine_ = eng;
		for (auto& weak_obs : observers_) {
			if (auto obs = weak_obs.lock()) {
				obs->script_engine(eng);
			}
		}
		return *this;
	}

	engine* script_engine() const { return script_engine_; }

	// Parameter names for script receivers
	signal_emitter& parameter_names(const std::vector<std::string>& names) {
		ordered_parameter_names_ = names.empty()
			? nullptr
			: std::make_shared<std::vector<std::string>>(names);
		return *this;
	}

	std::vector<std::string> parameter_names() const {
		return ordered_parameter_names_ ? *ordered_parameter_names_ : std::vector<std::string>();
	}

	bool has_parameter_names() const {
		return ordered_parameter_names_ && !ordered_parameter_names_->empty();
	}

	// Set callback to invoke when signal is blocked
	void set_blocked_callback(std::function<T> callback) {
		blocked_callback_ = std::move(callback);
	}

private:
	// Internal disconnect without acquiring lock (caller must hold lock)
	void disconnect_unlocked(shared_receiver_type recv) {
		if (recv) {
			recv->owner_signal_.reset();
			if (!in_call_) {
				remove_observer(recv);
			} else {
				disconnect_queue_.push_back(recv);
			}
		}
	}

	// Internal cull without acquiring lock (caller must hold lock)
	size_t cull_dead_observers_unlocked() {
		observers_.erase(
			std::remove_if(observers_.begin(), observers_.end(),
				[](const weak_receiver_type& w) { return w.expired(); }),
			observers_.end()
		);
		return observers_.size();
	}

	// Emit implementation with optional short-circuit
	template<bool ShortCircuit, typename... Args>
	bool emit_impl(Args&&... args) {
		JAI_SIGNAL_LOCK();

		if (blocked()) {
			called_while_blocked_ = true;
			if (blocked_callback_) {
				blocked_callback_(std::forward<Args>(args)...);
			}
			return true;
		}

		in_call_ = true;
		bool result = true;
		std::vector<shared_receiver_type> oneshots_to_remove;

		for (size_t i = 0; i < observers_.size(); ++i) {
			auto locked = observers_[i].lock();
			if (!locked) continue;

			if constexpr (ShortCircuit) {
				if (!locked->predicate(std::forward<Args>(args)...)) {
					result = false;
					if (locked->is_oneshot()) oneshots_to_remove.push_back(locked);
					break;
				}
			} else {
				locked->notify(std::forward<Args>(args)...);
			}

			if (locked->is_oneshot()) {
				oneshots_to_remove.push_back(locked);
			}
		}

		in_call_ = false;

		// Process disconnect queue
		for (auto& recv : disconnect_queue_) {
			remove_observer(recv);
		}
		disconnect_queue_.clear();

		// Remove one-shots
		for (auto& recv : oneshots_to_remove) {
			recv->owner_signal_.reset();
			remove_observer(recv);
		}

		// Cull dead observers
		cull_dead_observers_unlocked();

		return result;
	}

	// Emit implementation without arguments
	template<bool ShortCircuit>
	bool emit_impl_noargs() {
		JAI_SIGNAL_LOCK();

		if (blocked()) {
			called_while_blocked_ = true;
			if (blocked_callback_) {
				blocked_callback_();
			}
			return true;
		}

		in_call_ = true;
		bool result = true;
		std::vector<shared_receiver_type> oneshots_to_remove;

		for (size_t i = 0; i < observers_.size(); ++i) {
			auto locked = observers_[i].lock();
			if (!locked) continue;

			if constexpr (ShortCircuit) {
				if (!locked->predicate()) {
					result = false;
					if (locked->is_oneshot()) oneshots_to_remove.push_back(locked);
					break;
				}
			} else {
				locked->notify();
			}

			if (locked->is_oneshot()) {
				oneshots_to_remove.push_back(locked);
			}
		}

		in_call_ = false;

		// Process disconnect queue
		for (auto& recv : disconnect_queue_) {
			remove_observer(recv);
		}
		disconnect_queue_.clear();

		// Remove one-shots
		for (auto& recv : oneshots_to_remove) {
			recv->owner_signal_.reset();
			remove_observer(recv);
		}

		// Cull dead observers
		cull_dead_observers_unlocked();

		return result;
	}

	void remove_observer(const shared_receiver_type& recv) {
		observers_.erase(
			std::remove_if(observers_.begin(), observers_.end(),
				[&recv](const weak_receiver_type& w) {
					auto locked = w.lock();
					return !locked || locked->id() == recv->id();
				}),
			observers_.end()
		);
	}

	// Vector for insertion-order iteration
	std::vector<weak_receiver_type> observers_;
	std::map<std::string, shared_receiver_type> owned_connections_;

	// Prevent shared_from_this issues - we use this for connected() tracking
	std::shared_ptr<char> prevent_shared_ownership_;

	bool in_call_ = false;
	int is_blocked_ = 0;
	std::function<T> blocked_callback_;
	std::vector<shared_receiver_type> disconnect_queue_;
	bool called_while_blocked_ = false;

	std::shared_ptr<std::vector<std::string>> ordered_parameter_names_;
	engine* script_engine_ = nullptr;

#ifndef JAI_SIGNALS_NO_THREADSAFE
	mutable std::recursive_mutex mutex_;
#endif

	// Allow signal<T> to access internals
	template<typename U> friend class signal;
};


// ============================================================================
// signal<T> - Public connection interface (connect-only, cannot emit)
// ============================================================================
//
// A signal wraps a signal_emitter and provides a connect-only interface.
// Use this as a public member to expose signal connection to external code
// while keeping emission private.
//
// Usage:
//   class Button {
//   private:
//       jai::signal_emitter<void(int, int)> on_click_;
//   public:
//       jai::signal<void(int, int)> on_click{on_click_};
//   };
//
//   Button btn;
//   btn.on_click.connect([](int x, int y) { ... });

template<typename T>
class signal {
public:
	using function_type = std::function<T>;
	using receiver_type = receiver<T>;
	using shared_receiver_type = std::shared_ptr<receiver<T>>;
	using weak_receiver_type = std::weak_ptr<receiver<T>>;

	// Construct from signal_emitter reference
	explicit signal(signal_emitter<T>& emitter) : emitter_(emitter) {}

	// Copy constructor (copies the reference)
	signal(const signal& other) : emitter_(other.emitter_) {}

	// Connect a C++ callback
	[[nodiscard]]
	shared_receiver_type connect(std::function<T> callback) {
		return emitter_.connect(std::move(callback));
	}

	// Connect a script callback
	[[nodiscard]]
	shared_receiver_type connect(const std::string& script_callback) {
		return emitter_.connect(script_callback);
	}

	// Connect one-shot (auto-disconnects after first fire)
	[[nodiscard]]
	shared_receiver_type connect_oneshot(std::function<T> callback) {
		return emitter_.connect_oneshot(std::move(callback));
	}

	// Connect one-shot script callback
	[[nodiscard]]
	shared_receiver_type connect_oneshot(const std::string& script_callback) {
		return emitter_.connect_oneshot(script_callback);
	}

	// Connect an existing receiver
	bool connect(shared_receiver_type recv) {
		return emitter_.connect(recv);
	}

	// Connect with named ID
	shared_receiver_type connect(const std::string& id, std::function<T> callback) {
		return emitter_.connect(id, std::move(callback));
	}

	shared_receiver_type connect(const std::string& id, const std::string& script_callback) {
		return emitter_.connect(id, script_callback);
	}

	// Get owned connection by ID
	shared_receiver_type connection(const std::string& id) {
		return emitter_.connection(id);
	}

	// Check if owned connection exists
	bool connected(const std::string& id) const {
		return emitter_.connected(id);
	}

	// Number of live observers
	size_t observer_count() const {
		return emitter_.observer_count();
	}

	// Disconnect a receiver
	void disconnect(shared_receiver_type recv) {
		emitter_.disconnect(recv);
	}

	// Disconnect by ID
	void disconnect(const std::string& id) {
		emitter_.disconnect(id);
	}

	// Script engine configuration
	void script_engine(engine* eng) {
		emitter_.script_engine(eng);
	}

	engine* script_engine() const {
		return emitter_.script_engine();
	}

	// Parameter names
	void parameter_names(const std::vector<std::string>& names) {
		emitter_.parameter_names(names);
	}

	std::vector<std::string> parameter_names() const {
		return emitter_.parameter_names();
	}

	bool has_parameter_names() const {
		return emitter_.has_parameter_names();
	}

private:
	signal_emitter<T>& emitter_;
};

} // namespace jai

#endif // __JAISCRIPT_SIGNALS_SIGNAL_HPP__
