#include "jaiscript/core/execution_backend.hpp"
#include "jaiscript/core/engine.hpp"
#include "jaiscript/core/types.hpp"

namespace jai {

// Stub implementation of vm_backend until we have a proper VM
class vm_backend_stub : public execution_backend {
private:
    static script_exception dummy_exception;
    
public:
    vm_backend_stub() = default;
    ~vm_backend_stub() override = default;
    
    // Core execution
    script_value execute(const std::vector<declaration_ptr>& declarations) override {
        throw std::runtime_error("VM backend is currently being refactored");
    }
    
    void prepare_for_execution() override {
        // Stub
    }
    
    // Variable access
    script_value get_variable(const std::string& name) const override {
        throw std::runtime_error("VM backend is currently being refactored");
    }
    
    bool has_variable(const std::string& name) const override {
        return false;
    }
    
    // Scope management
    void push_scope() override {
        // Stub
    }
    
    void pop_scope() override {
        // Stub
    }
    
    void define_variable(const std::string& name, const script_value& value) override {
        // Stub
    }
    
    // Configuration
    void set_has_custom_numeric_ops(bool value) override {
        // Stub
    }
    
    void set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) override {
        // Stub
    }
    
    void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) override {
        // Stub
    }
    
    void set_engine_reference(std::weak_ptr<engine> engine_ref) override {
        // Stub
    }
    
    // Exception handling
    bool is_unwinding() const override {
        return false;
    }
    
    const script_exception& get_current_exception() const override {
        return dummy_exception;
    }
    
    // Backend name
    std::string get_backend_name() const override {
        return "VM Backend (Stub - Under Refactoring)";
    }
};

script_exception vm_backend_stub::dummy_exception("No exception");

std::unique_ptr<execution_backend> create_vm_backend() {
    return std::make_unique<vm_backend_stub>();
}

} // namespace jai