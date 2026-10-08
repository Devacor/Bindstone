#include "../../include/jaiscript/core/coroutine.hpp"
#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/core/execution_backend.hpp"
#include "../../include/jaiscript/detail/arg_capture.hpp"

namespace jai {

coroutine_handle::coroutine_handle(engine* eng)
    : engine_(eng)
    , yield_value_(std::monostate{}, eng)
    , receiver_(std::monostate{}, eng) {
}

void coroutine_handle::set_function(std::shared_ptr<function_decl> func,
                                     std::vector<script_value> args,
                                     std::shared_ptr<environment> closure_env) {
    function_ = std::move(func);
    initial_args_ = std::move(args);
    closure_env_ = std::move(closure_env);
    // the call's arguments take their values at the call; the body binds them at the first resume
    for (size_t i = 0; i < initial_args_.size(); ++i) {
        if (!detail::capture_candidate(initial_args_[i], detail::transient_read_marker::arg_capture_full)) continue;
        detail::capture_argument(initial_args_[i], detail::transient_read_marker::arg_capture_full,
                                 function_ ? detail::parameters_binding(function_->parameters, i) : detail::arg_binding::opaque);
    }
}

void coroutine_handle::do_yield(script_value value) {
    yield_value_ = std::move(value);
    status_ = status::suspended;
}

checked_result<script_value> coroutine_handle::resume(engine* eng) {
    if (done()) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::evaluation_failed),
            "Cannot resume a completed coroutine");
    }
    if (status_ == status::running) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::evaluation_failed),
            "Coroutine is already running");
    }

    execution_backend* backend = eng->get_execution_backend();
    if (backend->is_executing() || backend->is_unwinding()) { return backend->resume_coroutine(*this); }
    return eng->checked_host_entry([](execution_backend& be, void* self) { return be.resume_coroutine(*static_cast<coroutine_handle*>(self)); }, this);
}

} // namespace jai
