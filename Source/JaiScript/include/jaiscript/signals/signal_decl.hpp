#pragma once

#ifndef __JAISCRIPT_SIGNALS_SIGNAL_DECL_HPP__
#define __JAISCRIPT_SIGNALS_SIGNAL_DECL_HPP__

#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <type_traits>
#include <atomic>

#ifndef JAI_SIGNALS_NO_THREADSAFE
#include <mutex>
#endif

namespace jai {

// Thread safety is ENABLED by default: connect/disconnect/emit are guarded by a
// recursive_mutex so callbacks may modify the signal while it fires. Define
// JAI_SIGNALS_NO_THREADSAFE to drop the lock (single-threaded, max performance).
#ifndef JAI_SIGNALS_NO_THREADSAFE
#define JAI_SIGNAL_LOCK() std::lock_guard<std::recursive_mutex> _signal_lock(mutex_)
#else
#define JAI_SIGNAL_LOCK() ((void)0)
#endif

// Forward declarations
class engine;
class script_value;
template<typename T> class signal_emitter;

template<typename T>
class receiver {
public:
	using function_type = std::function<T>;
	using shared_type = std::shared_ptr<receiver<T>>;
	using weak_type = std::weak_ptr<receiver<T>>;

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

	void notify() {
		if (!blocked() && !invalid()) {
			if (script_callback_.empty()) {
				callback_();
			} else {
				call_script();
			}
		}
	}

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

	template<typename... Args>
	void operator()(Args&&... args) {
		notify(std::forward<Args>(args)...);
	}

	void operator()() {
		notify();
	}

	bool invalid() const {
		return script_callback_.empty() && !callback_;
	}

	bool connected() const {
		return !owner_signal_.expired();
	}

	void block() { ++is_blocked_; }

	void unblock() {
		--is_blocked_;
		if (is_blocked_ < 0) {
			is_blocked_ = 0;
		}
	}

	bool blocked() const { return is_blocked_ != 0; }

	bool is_oneshot() const { return oneshot_; }

	bool operator<(const receiver<T>& rhs) const { return id_ < rhs.id_; }
	bool operator>(const receiver<T>& rhs) const { return id_ > rhs.id_; }
	bool operator==(const receiver<T>& rhs) const { return id_ == rhs.id_; }
	bool operator!=(const receiver<T>& rhs) const { return id_ != rhs.id_; }

	const std::string& script() const { return script_callback_; }
	bool has_script() const { return !script_callback_.empty(); }

	void script_engine(engine* eng) { script_engine_ = eng; }
	engine* script_engine() const { return script_engine_; }

	const std::shared_ptr<std::vector<std::string>>& parameter_names() const {
		return ordered_parameter_names_;
	}

	int64_t id() const { return id_; }

private:
	receiver() : id_(0) {}

	receiver(std::function<T> callback, int64_t id)
		: callback_(std::move(callback))
		, id_(id) {}

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

	// Implemented in signal_impl.hpp (needs the full engine definition).
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
	static std::atomic<int64_t> unique_id_;

	template<typename U> friend class signal_emitter;
};

template<typename T>
std::atomic<int64_t> receiver<T>::unique_id_ = 0;


template<typename T>
class signal_emitter {
public:
	using function_type = std::function<T>;
	using receiver_type = receiver<T>;
	using shared_receiver_type = std::shared_ptr<receiver<T>>;
	using weak_receiver_type = std::weak_ptr<receiver<T>>;

	signal_emitter() : prevent_shared_ownership_(std::make_shared<char>()) {}
	~signal_emitter() = default;

	signal_emitter(const signal_emitter&) = delete;
	signal_emitter& operator=(const signal_emitter&) = delete;

	signal_emitter(signal_emitter&&) = default;
	signal_emitter& operator=(signal_emitter&&) = default;

	[[nodiscard]]
	shared_receiver_type connect(std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(std::move(callback));
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	[[nodiscard]]
	shared_receiver_type connect(const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(script_callback, script_engine_, ordered_parameter_names_);
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	[[nodiscard]]
	shared_receiver_type connect_oneshot(std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(std::move(callback));
		recv->oneshot_ = true;
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	[[nodiscard]]
	shared_receiver_type connect_oneshot(const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(script_callback, script_engine_, ordered_parameter_names_);
		recv->oneshot_ = true;
		recv->owner_signal_ = prevent_shared_ownership_;
		observers_.push_back(recv);
		return recv;
	}

	bool connect(shared_receiver_type recv) {
		JAI_SIGNAL_LOCK();
		if (recv) {
			recv->owner_signal_ = prevent_shared_ownership_;
			observers_.push_back(recv);
			return true;
		}
		return false;
	}

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

	// Connect a script FUNCTION value as an owned, named connection. The function
	// value carries its engine; arguments convert through the same machinery as
	// source-string receivers. Defined in signal_impl.hpp (needs the full engine).
	shared_receiver_type connect(const std::string& id, const script_value& fn_callback);

	shared_receiver_type connection(const std::string& id) {
		JAI_SIGNAL_LOCK();
		auto it = owned_connections_.find(id);
		return it != owned_connections_.end() ? it->second : shared_receiver_type();
	}

	bool connected(const std::string& id) const {
		JAI_SIGNAL_LOCK();
		return owned_connections_.find(id) != owned_connections_.end();
	}

	size_t observer_count() const {
		JAI_SIGNAL_LOCK();
		size_t count = 0;
		for (const auto& weak_obs : observers_) {
			if (!weak_obs.expired()) ++count;
		}
		return count;
	}

	void disconnect(shared_receiver_type recv) {
		JAI_SIGNAL_LOCK();
		if (recv) {
			recv->owner_signal_.reset();
			if (in_call_depth_ == 0) {
				remove_observer(recv);
			} else {
				disconnect_queue_.push_back(recv);
			}
		}
	}

	void disconnect(const std::string& id) {
		JAI_SIGNAL_LOCK();
		auto it = owned_connections_.find(id);
		if (it != owned_connections_.end()) {
			disconnect_unlocked(it->second);
			owned_connections_.erase(it);
		}
	}

	void clear() {
		JAI_SIGNAL_LOCK();
		for (auto& weak_obs : observers_) {
			if (auto obs = weak_obs.lock()) {
				obs->owner_signal_.reset();
			}
		}
		owned_connections_.clear();
		if (in_call_depth_ == 0) {
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

	template<typename... Args>
	void operator()(Args&&... args) {
		emit_impl<false>(std::forward<Args>(args)...);
	}

	void operator()() {
		emit_impl_noargs<false>();
	}

	template<typename... Args>
	void emit(Args&&... args) {
		emit_impl<false>(std::forward<Args>(args)...);
	}

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

	size_t cull_dead_observers() {
		JAI_SIGNAL_LOCK();
		return cull_dead_observers_unlocked();
	}

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

	void set_blocked_callback(std::function<T> callback) {
		blocked_callback_ = std::move(callback);
	}

private:
	// Caller must hold the lock.
	void disconnect_unlocked(shared_receiver_type recv) {
		if (recv) {
			recv->owner_signal_.reset();
			if (in_call_depth_ == 0) {
				remove_observer(recv);
			} else {
				disconnect_queue_.push_back(recv);
			}
		}
	}

	// Caller must hold the lock.
	size_t cull_dead_observers_unlocked() {
		observers_.erase(
			std::remove_if(observers_.begin(), observers_.end(),
				[](const weak_receiver_type& w) { return w.expired(); }),
			observers_.end()
		);
		return observers_.size();
	}

	// Deferred post-emit cleanup, run once the outermost emit unwinds: apply queued
	// disconnects, remove fired one-shots, cull dead observers. Invoked from emit_scope's
	// destructor so it still runs if a slot throws.
	void drain_after_emit() {
		for (auto& recv : disconnect_queue_) {
			remove_observer(recv);
		}
		disconnect_queue_.clear();

		// Remove one-shots only if any fired (skips the scan in the common case).
		if (has_oneshots_pending_) {
			for (size_t i = observers_.size(); i > 0; --i) {
				auto locked = observers_[i - 1].lock();
				if (locked && locked->is_oneshot()) {
					locked->owner_signal_.reset();
					remove_observer(locked);
				}
			}
			has_oneshots_pending_ = false;
		}

		// Only scan/erase when this emit actually saw a dead observer (or short-circuited) —
		// turns the common steady-state drain (live observers, no queue, no one-shots) into O(1).
		if (has_dead_observers_) {
			cull_dead_observers_unlocked();
			has_dead_observers_ = false;
		}
	}

	// RAII depth guard for emit (see in_call_depth_): increments on entry, and on exit — normal
	// or via exception — decrements and drains at depth 0. Constructed AFTER JAI_SIGNAL_LOCK so
	// it destructs first, draining while the lock is still held.
	struct emit_scope {
		signal_emitter* self;
		explicit emit_scope(signal_emitter* s) : self(s) { ++self->in_call_depth_; }
		~emit_scope() { if (--self->in_call_depth_ == 0) self->drain_after_emit(); }
		emit_scope(const emit_scope&) = delete;
		emit_scope& operator=(const emit_scope&) = delete;
	};

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

		emit_scope guard(this);  // drains queue / one-shots / dead observers on the outermost
		                         // unwind, even if a slot throws
		bool result = true;

		for (size_t i = 0; i < observers_.size(); ++i) {
			auto locked = observers_[i].lock();
			if (!locked) { has_dead_observers_ = true; continue; }

			if constexpr (ShortCircuit) {
				// Pass as LVALUES: forwarding rvalue args inside the loop would let the
				// first receiver's by-value parameter move-consume them, handing every
				// later receiver an empty shell (null shared_ptr, empty string, ...).
				if (!locked->predicate(args...)) {
					result = false;
					if (locked->is_oneshot()) has_oneshots_pending_ = true;
					has_dead_observers_ = true;  // unscanned tail may hold dead observers
					break;
				}
			} else {
				locked->notify(args...);
			}

			if (locked->is_oneshot()) {
				has_oneshots_pending_ = true;
			}
		}

		return result;
	}

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

		emit_scope guard(this);  // drains queue / one-shots / dead observers on the outermost
		                         // unwind, even if a slot throws
		bool result = true;

		for (size_t i = 0; i < observers_.size(); ++i) {
			auto locked = observers_[i].lock();
			if (!locked) { has_dead_observers_ = true; continue; }

			if constexpr (ShortCircuit) {
				if (!locked->predicate()) {
					result = false;
					if (locked->is_oneshot()) has_oneshots_pending_ = true;
					has_dead_observers_ = true;  // unscanned tail may hold dead observers
					break;
				}
			} else {
				locked->notify();
			}

			if (locked->is_oneshot()) {
				has_oneshots_pending_ = true;
			}
		}

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

	// Vector, not set: receivers fire in insertion order.
	std::vector<weak_receiver_type> observers_;
	std::map<std::string, shared_receiver_type> owned_connections_;

	// Prevent shared_from_this issues - we use this for connected() tracking
	std::shared_ptr<char> prevent_shared_ownership_;

	// Emit can run reentrantly (a slot may emit the same signal), so track call DEPTH, not a
	// bool: the deferred post-emit cleanup runs only when the OUTERMOST emit unwinds. An RAII
	// guard (emit_scope) owns the decrement, so a throwing slot can't leave the depth stuck —
	// which would otherwise wedge the emitter (disconnects queue forever, dead receivers keep
	// firing). has_oneshots_pending_ accumulates across the reentrant call stack.
	int in_call_depth_ = 0;
	bool has_oneshots_pending_ = false;
	// Set when an emit pass sees an expired observer (or stops early on short-circuit, leaving an
	// unscanned tail) — gates the post-emit cull so a steady-state emit over live observers skips
	// the O(n) scan entirely. Accumulates across reentrant emits like has_oneshots_pending_.
	bool has_dead_observers_ = false;
	int is_blocked_ = 0;
	std::function<T> blocked_callback_;
	std::vector<shared_receiver_type> disconnect_queue_;
	bool called_while_blocked_ = false;

	std::shared_ptr<std::vector<std::string>> ordered_parameter_names_;
	engine* script_engine_ = nullptr;

#ifndef JAI_SIGNALS_NO_THREADSAFE
	mutable std::recursive_mutex mutex_;
#endif

	template<typename U> friend class signal;

	template<typename Archive, typename U>
	friend void save_signal_emitter(Archive& ar, const signal_emitter<U>& sig);
	template<typename Archive, typename U>
	friend void load_signal_emitter(Archive& ar, signal_emitter<U>& sig);
};


template<typename T>
class signal {
public:
	using function_type = std::function<T>;
	using receiver_type = receiver<T>;
	using shared_receiver_type = std::shared_ptr<receiver<T>>;
	using weak_receiver_type = std::weak_ptr<receiver<T>>;

	explicit signal(signal_emitter<T>& emitter) : emitter_(emitter) {}

	signal(const signal& other) : emitter_(other.emitter_) {}

	[[nodiscard]]
	shared_receiver_type connect(std::function<T> callback) {
		return emitter_.connect(std::move(callback));
	}

	[[nodiscard]]
	shared_receiver_type connect(const std::string& script_callback) {
		return emitter_.connect(script_callback);
	}

	[[nodiscard]]
	shared_receiver_type connect_oneshot(std::function<T> callback) {
		return emitter_.connect_oneshot(std::move(callback));
	}

	[[nodiscard]]
	shared_receiver_type connect_oneshot(const std::string& script_callback) {
		return emitter_.connect_oneshot(script_callback);
	}

	bool connect(shared_receiver_type recv) {
		return emitter_.connect(recv);
	}

	shared_receiver_type connect(const std::string& id, std::function<T> callback) {
		return emitter_.connect(id, std::move(callback));
	}

	shared_receiver_type connect(const std::string& id, const script_value& fn_callback) {
		return emitter_.connect(id, fn_callback);
	}

	shared_receiver_type connect(const std::string& id, const std::string& script_callback) {
		return emitter_.connect(id, script_callback);
	}

	shared_receiver_type connection(const std::string& id) {
		return emitter_.connection(id);
	}

	bool connected(const std::string& id) const {
		return emitter_.connected(id);
	}

	size_t observer_count() const {
		return emitter_.observer_count();
	}

	void disconnect(shared_receiver_type recv) {
		emitter_.disconnect(recv);
	}

	void disconnect(const std::string& id) {
		emitter_.disconnect(id);
	}

	void script_engine(engine* eng) {
		emitter_.script_engine(eng);
	}

	engine* script_engine() const {
		return emitter_.script_engine();
	}

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

// NOTE: signal_impl.hpp (the script-callback execution glue) is intentionally NOT
// included here. It drags in core/engine.hpp + core/value.hpp (the entire interpreter
// + value system, ~58 extra transitive includes, ~1.8s parse cost per TU), which the
// vast majority of consumers using plain C++ signals never need.
//
// Consumers that actually fire SCRIPT receivers (receiver<T> constructed from a script
// string + engine*) must include <jaiscript/signals/signal_impl.hpp> explicitly.
// dynamic_binder.hpp and signal_serialization.hpp already do so.
// For convenience, <jaiscript/signals/signal.hpp> includes both.

#endif // __JAISCRIPT_SIGNALS_SIGNAL_DECL_HPP__
