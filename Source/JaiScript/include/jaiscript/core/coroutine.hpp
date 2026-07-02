#pragma once

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <vector>
#include <memory>

namespace jai {

class engine;
class environment;
class interpreter;
class function_decl;

// Backend-owned continuation state. Each backend stores its own representation
// (interpreter: saved environment + AST continuation stack; VM: frames + ip).
struct coroutine_backend_state {
    virtual ~coroutine_backend_state() = default;
};

class coroutine_handle : public std::enable_shared_from_this<coroutine_handle> {
public:
    enum class status { created, running, suspended, completed, failed };

    coroutine_handle(engine* eng);

    checked_result<script_value> resume(engine* eng);
    bool done() const { return status_ == status::completed || status_ == status::failed; }
    status get_status() const { return status_; }

    // Internal — called by the backend when creating from a coroutine function declaration
    void set_function(std::shared_ptr<function_decl> func,
                      std::vector<script_value> args,
                      std::shared_ptr<environment> closure_env);

    // Internal — yield support
    void do_yield(script_value value);
    const script_value& last_value() const { return yield_value_; }

    std::shared_ptr<function_decl> get_function() const { return function_; }
    const std::vector<script_value>& get_args() const { return initial_args_; }
    std::shared_ptr<environment> get_closure_env() const { return closure_env_; }

    void set_status(status s) { status_ = s; }

    coroutine_backend_state* backend_state() const { return backend_state_.get(); }
    void set_backend_state(std::unique_ptr<coroutine_backend_state> state) {
        backend_state_ = std::move(state);
    }

private:
    friend class interpreter;

    engine* engine_ = nullptr;
    status status_ = status::created;
    script_value yield_value_;

    std::shared_ptr<function_decl> function_;
    std::vector<script_value> initial_args_;
    std::shared_ptr<environment> closure_env_;

    std::unique_ptr<coroutine_backend_state> backend_state_;
};

} // namespace jai
