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
#include <utility>
#include <atomic>

#ifndef JAI_SIGNALS_NO_THREADSAFE
#include <mutex>
#endif

namespace jai {

// Thread safety is ENABLED by default: connect/disconnect/emit are guarded by a
// recursive_mutex so callbacks may modify the signal while it fires. Define
// JAI_SIGNALS_NO_THREADSAFE to drop the lock (single-threaded, max performance).
#ifndef JAI_SIGNALS_NO_THREADSAFE
#define JAI_SIGNAL_LOCK() std::lock_guard<std::recursive_mutex> _signal_lock(state_->mutex)
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
	bool oneshot_fired_ = false;
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
	// What an emit touches after a receiver returns lives here, off the emitter, so a receiver may destroy the emitter.
	// Emit can run reentrantly (a slot may emit the same signal), so it tracks call DEPTH, not a bool: the deferred
	// post-emit cleanup runs only when the OUTERMOST emit unwinds. Receivers hold it weakly for connected().
	struct emit_state {
#ifndef JAI_SIGNALS_NO_THREADSAFE
		std::recursive_mutex mutex;
#endif
		int depth = 0;
		bool orphaned = false;
		std::shared_ptr<emit_state> self;   // the destroyed emitter's hand-off to the outermost emit
	};
	// A moved-from emitter keeps a fresh state of its own
	struct emit_state_handle {
		std::shared_ptr<emit_state> ptr = std::make_shared<emit_state>();
		emit_state_handle() = default;
		emit_state_handle(emit_state_handle&& a_other) : ptr(std::exchange(a_other.ptr, std::make_shared<emit_state>())) {}
		emit_state_handle& operator=(emit_state_handle&& a_other) {
			ptr = std::exchange(a_other.ptr, std::make_shared<emit_state>());
			return *this;
		}
		emit_state* operator->() const { return ptr.get(); }
	};

public:
	using function_type = std::function<T>;
	using receiver_type = receiver<T>;
	using shared_receiver_type = std::shared_ptr<receiver<T>>;
	using weak_receiver_type = std::weak_ptr<receiver<T>>;

	signal_emitter() = default;
	~signal_emitter() {
		// Destroyed by one of its own receivers: the emit in progress keeps the state and frees it as it unwinds
		if (state_->depth > 0) {
			state_->orphaned = true;
			state_->self = state_.ptr;
		}
	}

	signal_emitter(const signal_emitter&) = delete;
	signal_emitter& operator=(const signal_emitter&) = delete;

#ifndef JAI_SIGNALS_NO_THREADSAFE
	signal_emitter(signal_emitter&&) = delete;
	signal_emitter& operator=(signal_emitter&&) = delete;
#else
	signal_emitter(signal_emitter&&) = default;
	signal_emitter& operator=(signal_emitter&&) = default;
#endif

	[[nodiscard]]
	shared_receiver_type connect(std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(std::move(callback));
		recv->owner_signal_ = state_.ptr;
		observers_.push_back(recv);
		return recv;
	}

	[[nodiscard]]
	shared_receiver_type connect(const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(script_callback, script_engine_, ordered_parameter_names_);
		recv->owner_signal_ = state_.ptr;
		observers_.push_back(recv);
		return recv;
	}

	[[nodiscard]]
	shared_receiver_type connect_oneshot(std::function<T> callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(std::move(callback));
		recv->oneshot_ = true;
		recv->owner_signal_ = state_.ptr;
		observers_.push_back(recv);
		return recv;
	}

	[[nodiscard]]
	shared_receiver_type connect_oneshot(const std::string& script_callback) {
		JAI_SIGNAL_LOCK();
		auto recv = receiver<T>::make(script_callback, script_engine_, ordered_parameter_names_);
		recv->oneshot_ = true;
		recv->owner_signal_ = state_.ptr;
		observers_.push_back(recv);
		return recv;
	}

	bool connect(shared_receiver_type recv) {
		JAI_SIGNAL_LOCK();
		if (recv) {
			recv->owner_signal_ = state_.ptr;
			recv->oneshot_fired_ = false;
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
			if (state_->depth == 0) {
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
		if (state_->depth == 0) {
			observers_.clear();
			loaded_observers_.clear();
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
			if (state_->depth == 0) {
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
			const auto fired = [](const shared_receiver_type& a_receiver) { return a_receiver->is_oneshot() && a_receiver->oneshot_fired_; };
			std::erase_if(observers_, [&fired](const weak_receiver_type& a_observer) {
				auto locked = a_observer.lock();
				if (!locked) { return true; }
				if (!fired(locked)) { return false; }
				locked->owner_signal_.reset();
				return true;
			});
			if (!loaded_observers_.empty()) { std::erase_if(loaded_observers_, fired); }
			has_oneshots_pending_ = false;
		}

		// Only scan/erase when this emit actually saw a dead observer (or short-circuited) —
		// turns the common steady-state drain (live observers, no queue, no one-shots) into O(1).
		if (has_dead_observers_) {
			cull_dead_observers_unlocked();
			has_dead_observers_ = false;
		}
	}

	// One emit's lock and depth. The outermost exit (normal, or a receiver's throw) drains while still locked; when a
	// receiver destroyed the emitter it instead unlocks and frees the state the destructor handed over.
	struct emit_scope {
		signal_emitter* self;
		emit_state* state;
#ifndef JAI_SIGNALS_NO_THREADSAFE
		std::unique_lock<std::recursive_mutex> lock;
		explicit emit_scope(signal_emitter* a_self) : self(a_self), state(a_self->state_.ptr.get()), lock(state->mutex) { ++state->depth; }
#else
		explicit emit_scope(signal_emitter* a_self) : self(a_self), state(a_self->state_.ptr.get()) { ++state->depth; }
#endif
		~emit_scope() {
			if (--state->depth != 0) { return; }
			if (!state->orphaned) {
				self->drain_after_emit();
				return;
			}
#ifndef JAI_SIGNALS_NO_THREADSAFE
			lock.unlock();
#endif
			std::shared_ptr<emit_state> last = std::move(state->self);
		}
		bool emitter_alive() const { return !state->orphaned; }
		emit_scope(const emit_scope&) = delete;
		emit_scope& operator=(const emit_scope&) = delete;
	};

	// A one-shot fires once: marked before the call, so one that throws or re-emits has fired
	bool fire_oneshot_once(const shared_receiver_type& a_receiver) {
		if (!a_receiver->is_oneshot()) { return true; }
		if (a_receiver->oneshot_fired_) { return false; }
		a_receiver->oneshot_fired_ = true;
		has_oneshots_pending_ = true;
		return true;
	}

	template<bool ShortCircuit, typename... Args>
	bool emit_impl(Args&&... args) {
		emit_scope scope(this);

		if (blocked()) {
			called_while_blocked_ = true;
			if (blocked_callback_) {
				blocked_callback_(std::forward<Args>(args)...);
			}
			return true;
		}

		bool result = true;

		for (size_t i = 0; i < observers_.size(); ++i) {
			auto locked = observers_[i].lock();
			if (!locked) { has_dead_observers_ = true; continue; }
			if (!fire_oneshot_once(locked)) { continue; }

			if constexpr (ShortCircuit) {
				// Pass as LVALUES: forwarding rvalue args inside the loop would let the
				// first receiver's by-value parameter move-consume them, handing every
				// later receiver an empty shell (null shared_ptr, empty string, ...).
				const bool keepGoing = locked->predicate(args...);
				if (!scope.emitter_alive()) { return keepGoing; }
				if (!keepGoing) {
					result = false;
					has_dead_observers_ = true;  // unscanned tail may hold dead observers
					break;
				}
			} else {
				locked->notify(args...);
				if (!scope.emitter_alive()) { return result; }
			}
		}

		return result;
	}

	template<bool ShortCircuit>
	bool emit_impl_noargs() {
		emit_scope scope(this);

		if (blocked()) {
			called_while_blocked_ = true;
			if (blocked_callback_) {
				blocked_callback_();
			}
			return true;
		}

		bool result = true;

		for (size_t i = 0; i < observers_.size(); ++i) {
			auto locked = observers_[i].lock();
			if (!locked) { has_dead_observers_ = true; continue; }
			if (!fire_oneshot_once(locked)) { continue; }

			if constexpr (ShortCircuit) {
				const bool keepGoing = locked->predicate();
				if (!scope.emitter_alive()) { return keepGoing; }
				if (!keepGoing) {
					result = false;
					has_dead_observers_ = true;  // unscanned tail may hold dead observers
					break;
				}
			} else {
				locked->notify();
				if (!scope.emitter_alive()) { return result; }
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
		if (!loaded_observers_.empty()) {
			std::erase_if(loaded_observers_, [&recv](const shared_receiver_type& held) { return held->id() == recv->id(); });
		}
	}

	// Vector, not set: receivers fire in insertion order.
	std::vector<weak_receiver_type> observers_;
	std::map<std::string, shared_receiver_type> owned_connections_;
	// unnamed script receivers a load rebuilt: nothing outside holds them, so the emitter does (signal_serialization.hpp)
	std::vector<shared_receiver_type> loaded_observers_;

	emit_state_handle state_;

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

	template<typename U> friend class signal;

	// ADL serialization hooks (named save/load so the archive's ADL dispatch finds them).
	template<typename Archive, typename U>
	friend void save(Archive& ar, const signal_emitter<U>& sig);
	template<typename Archive, typename U>
	friend void load(Archive& ar, signal_emitter<U>& sig);
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
