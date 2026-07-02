#include "../../include/jaiscript/core/coroutine.hpp"
#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/core/execution_backend.hpp"

namespace jai {

checked_result<script_value> execution_backend::resume_coroutine(coroutine_handle& handle) {
    handle.set_status(coroutine_handle::status::failed);
    return checked_result<script_value>(
        make_error_code(runtime_error_code::evaluation_failed),
        "Coroutines are not supported by this backend");
}

coroutine_handle::coroutine_handle(engine* eng)
    : engine_(eng)
    , yield_value_(std::monostate{}, eng) {
}

void coroutine_handle::set_function(std::shared_ptr<function_decl> func,
                                     std::vector<script_value> args,
                                     std::shared_ptr<environment> closure_env) {
    function_ = std::move(func);
    initial_args_ = std::move(args);
    closure_env_ = std::move(closure_env);
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

    return eng->get_execution_backend()->resume_coroutine(*this);
}

} // namespace jai
