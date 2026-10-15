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

namespace {

JAI_NOINLINE checked_result<script_value> next_argument_count_error() {
    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "next() takes 0 or 1 arguments ([count])");
}

JAI_NOINLINE checked_result<script_value> next_count_type_error() {
    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "next() count must be an integer");
}

JAI_NOINLINE checked_result<script_value> next_count_range_error() {
    return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "next() count must be at least 1");
}

} // namespace

checked_result<script_value> coroutine_handle::next(engine* eng, script_int a_count) {
    if (a_count < 1) { return next_count_range_error(); }
    if (a_count == 1) { return resume_once(eng); }
    execution_backend* backend = eng->get_execution_backend();
    if (backend->is_executing() || backend->is_unwinding()) { return resume_steps(*backend, a_count); }
    struct step_request { coroutine_handle* self; script_int count; } request{this, a_count};
    return eng->checked_host_entry([](execution_backend& a_backend, void* a_request) {
        auto* steps = static_cast<step_request*>(a_request);
        return steps->self->resume_steps(a_backend, steps->count);
    }, &request);
}

// The steps of one next(n) are one entry: the entry armed the deadline, no step re-arms it, and a step's throw ends them
checked_result<script_value> coroutine_handle::resume_steps(execution_backend& a_backend, script_int a_count) {
    struct entry_scope {
        bool& continues;
        bool saved;
        ~entry_scope() { continues = saved; }
    } scope{continues_host_entry_, continues_host_entry_};
    continues_host_entry_ = true;
    for (script_int step = 1;; ++step) {
        if (done() || status_ == status::running) { return resume_refusal(); }
        checked_result<script_value> result = a_backend.resume_coroutine(*this);
        if (step == a_count || !result || a_backend.is_unwinding()) { return result; }
    }
}

checked_result<script_value> coroutine_handle::next_from_script(engine* eng, const script_value* a_args, size_t a_argCount) {
    if (a_argCount == 0) { return resume_once(eng); }
    if (a_argCount > 1) { return next_argument_count_error(); }
    if (!a_args[0].is_int()) { return next_count_type_error(); }
    return next(eng, a_args[0].as_int());
}

checked_result<script_value> coroutine_handle::resume_refusal() const {
    if (done()) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::evaluation_failed),
            "Cannot resume a completed coroutine");
    }
    return checked_result<script_value>(
        make_error_code(runtime_error_code::evaluation_failed),
        "Coroutine is already running");
}

checked_result<script_value> coroutine_handle::resume_once(engine* eng) {
    if (done() || status_ == status::running) { return resume_refusal(); }

    execution_backend* backend = eng->get_execution_backend();
    if (backend->is_executing() || backend->is_unwinding()) { return backend->resume_coroutine(*this); }
    return eng->checked_host_entry([](execution_backend& be, void* self) { return be.resume_coroutine(*static_cast<coroutine_handle*>(self)); }, this);
}

} // namespace jai
