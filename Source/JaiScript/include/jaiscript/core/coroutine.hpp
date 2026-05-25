#pragma once

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <vector>
#include <memory>

namespace jai {

// Forward declarations
class engine;
class interpreter;
class environment;
class ast_node;
class function_decl;
struct call_frame;

class coroutine_handle : public std::enable_shared_from_this<coroutine_handle> {
public:
    enum class status { created, running, suspended, completed, failed };

    // A breadcrumb recording where we were in the AST when yield fired.
    // Built bottom-up during yield unwind, consumed top-down (LIFO) during resume.
    struct continuation_point {
        ast_node* node;   // which block/loop/if we were inside
        size_t index;             // statement index (blocks), iteration (range-for), branch (if: 0=then,1=else)
        std::shared_ptr<environment> saved_env;  // environment to restore on resume (may be null)
    };

    coroutine_handle(engine* eng);

    // Script-facing API
    checked_result<script_value> resume(engine* eng);
    bool done() const { return status_ == status::completed || status_ == status::failed; }
    status get_status() const { return status_; }

    // Internal — called by interpreter when creating from coroutine function declaration
    void set_function(std::shared_ptr<function_decl> func,
                      std::vector<script_value> args,
                      std::shared_ptr<environment> closure_env);

    // Internal — yield support
    void do_yield(script_value value);
    const script_value& last_value() const { return yield_value_; }

    // Continuation stack — used by visit_* methods during yield unwind and resume fast-forward
    void push_continuation(ast_node* node, size_t index,
                           std::shared_ptr<environment> env = nullptr);
    continuation_point* peek_continuation(ast_node* node);
    void pop_continuation();
    bool has_continuations() const { return !continuations_.empty(); }

    // Function info
    std::shared_ptr<function_decl> get_function() const { return function_; }
    const std::vector<script_value>& get_args() const { return initial_args_; }
    std::shared_ptr<environment> get_closure_env() const { return closure_env_; }

    // Status management
    void set_status(status s) { status_ = s; }

    // Saved interpreter state (captured on yield, restored on resume)
    std::shared_ptr<environment> saved_environment_;
    std::vector<call_frame> saved_call_stack_;
    std::optional<script_value> saved_return_value_;
    bool saved_has_return_ = false;

private:
    engine* engine_ = nullptr;
    status status_ = status::created;
    script_value yield_value_;

    // The coroutine function
    std::shared_ptr<function_decl> function_;
    std::vector<script_value> initial_args_;
    std::shared_ptr<environment> closure_env_;

    // Continuation stack (breadcrumb trail for fast-forward on resume)
    std::vector<continuation_point> continuations_;
};

} // namespace jai
