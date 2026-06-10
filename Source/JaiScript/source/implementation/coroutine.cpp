#include "../../include/jaiscript/core/coroutine.hpp"
#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/detail/interpreter.hpp"
#include "../../include/jaiscript/detail/interpreter_backend.hpp"
#include "../../include/jaiscript/detail/ast.hpp"

namespace jai {

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

void coroutine_handle::push_continuation(ast_node* node, size_t index,
                                          std::shared_ptr<environment> env) {
    continuations_.push_back({node, index, std::move(env)});
}

coroutine_handle::continuation_point* coroutine_handle::peek_continuation(ast_node* node) {
    if (!continuations_.empty() && continuations_.back().node == node) {
        return &continuations_.back();
    }
    return nullptr;
}

void coroutine_handle::pop_continuation() {
    if (!continuations_.empty()) {
        continuations_.pop_back();
    }
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

    // Get the interpreter
    interpreter_backend* backend = dynamic_cast<interpreter_backend*>(
        eng->get_execution_backend());
    if (!backend) {
        status_ = status::failed;
        return checked_result<script_value>(
            make_error_code(runtime_error_code::evaluation_failed),
            "Coroutines require the interpreter backend");
    }
    interpreter* interp = backend->get_interpreter();

    // Host-level resume is its own budgeted entry; a resume nested inside an
    // executing script keeps the outer deadline.
    if (interp->current_call_depth_ == 0) {
        interp->arm_execution_deadline();
    }

    // Save caller's coroutine state (for nested coroutines)
    coroutine_handle* prev_coroutine = interp->active_coroutine_;
    bool prev_yield_request = interp->hasYieldRequest_;
    interp->active_coroutine_ = this;
    interp->hasYieldRequest_ = false;

    auto prev_status = status_;
    status_ = status::running;

    // A runtime error in the (re)started segment must reach the caller as an error;
    // falling through to `return yield_value_` would hand back the PREVIOUS yield
    // as a bogus success and silently swallow the failure.
    std::optional<checked_result<script_value>> error_result;

    if (prev_status == status::created) {
        // ================================================================
        // FIRST EXECUTION - call the function normally via call_function
        // ================================================================
        // call_function will:
        // - Set up call frame with parameters
        // - Execute the function body
        // - On yield: save environment/call_stack into this coroutine (via active_coroutine_)
        //   and return the yield value WITHOUT running cleanup
        // - On normal return: cleanup normally and return the result
        interpreter::script_defined_function scriptFunc(
            function_->name,
            function_->parameters,
            function_->return_type,
            function_->body,
            closure_env_,
            function_->local_count
        );

        auto call_result = interp->call_function(scriptFunc, initial_args_);

        if (interp->hasYieldRequest_) {
            // call_function already saved state into this coroutine
            // (saved_environment_, saved_call_stack_, etc.)
            status_ = status::suspended;
        } else if (!call_result) {
            status_ = status::failed;
            error_result.emplace(std::move(call_result));
        } else {
            status_ = status::completed;
            yield_value_ = std::move(call_result.value());
        }

    } else {
        // ================================================================
        // RESUME - restore saved state and continue from continuations
        // ================================================================

        // Save caller's interpreter state
        auto caller_env = interp->environment_;
        auto caller_call_stack = std::move(interp->call_stack_);
        auto caller_return_value = std::move(interp->returnValue_);
        bool caller_has_return = interp->hasReturnValue_;

        // Restore coroutine's saved state
        interp->environment_ = saved_environment_;
        interp->call_stack_ = std::move(saved_call_stack_);
        interp->returnValue_ = std::move(saved_return_value_);
        interp->hasReturnValue_ = saved_has_return_;

        // The continuations are consumed LIFO (outermost first).
        // The outermost continuation is for the function body (call_function's loop).
        // Pop it to get the statement index where yield occurred.
        auto* body_cont = peek_continuation(function_->body.get());
        size_t start_index = 0;
        if (body_cont) {
            start_index = body_cont->index;
            pop_continuation();
        }
        // Track if we hit an error during execution
        bool had_error = false;
        size_t last_body_idx = start_index;

        // Execute from the saved position in the function body
        for (size_t i = start_index; i < function_->body->declarations.size(); ++i) {
            last_body_idx = i;
            // For the first statement (start_index), the inner continuations
            // are still on the stack and will be consumed by visit_block_stmt,
            // visit_if_stmt, visit_while_stmt, etc. to fast-forward to the
            // exact point where yield occurred.
            checked_result<void> decl_result = interp->dispatch_decl(function_->body->declarations[i].get());

            if (!decl_result) {
                if (decl_result.error() != std::error_code()) {
                    status_ = status::failed;
                    had_error = true;
                    error_result.emplace(decl_result.error_value());
                    break;
                }
            }

            if (interp->hasReturnValue_ || interp->hasYieldRequest_) {
                break;
            }
        }

        // Check outcome
        if (!had_error) {
            if (interp->hasYieldRequest_) {
                // Yielded again - save state
                // Record where to resume in the function body.
                // If inner constructs pushed continuations, re-enter this statement.
                // If no inner continuations, the yield was a direct child, skip it.
                size_t resume_idx = has_continuations() ? last_body_idx : last_body_idx + 1;
                push_continuation(function_->body.get(), resume_idx);
                // The visit_* methods have pushed new continuations
                // and left the environment chain intact
                saved_environment_ = interp->environment_;
                saved_call_stack_ = std::move(interp->call_stack_);
                saved_return_value_ = std::move(interp->returnValue_);
                saved_has_return_ = interp->hasReturnValue_;
                status_ = status::suspended;
            } else if (interp->hasReturnValue_) {
                status_ = status::completed;
                if (interp->returnValue_) {
                    yield_value_ = std::move(interp->returnValue_.value());
                }
                // Clear saved state since coroutine is done
                saved_environment_.reset();
                saved_call_stack_.clear();
            } else {
                status_ = status::completed;
                // Clear saved state since coroutine is done
                saved_environment_.reset();
                saved_call_stack_.clear();
            }
        }

        // Restore caller state
        interp->environment_ = caller_env;
        interp->call_stack_ = std::move(caller_call_stack);
        interp->returnValue_ = std::move(caller_return_value);
        interp->hasReturnValue_ = caller_has_return;
    }

    // Restore caller's coroutine state
    interp->active_coroutine_ = prev_coroutine;
    interp->hasYieldRequest_ = prev_yield_request;

    if (error_result) {
        return std::move(*error_result);
    }
    return yield_value_;
}

} // namespace jai
