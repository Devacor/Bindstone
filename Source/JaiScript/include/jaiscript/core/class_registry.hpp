#pragma once

#include "jaiscript/core/dynamic_binder.hpp"
#include "jaiscript/core/script_class.hpp"
#include <memory>
#include <map>
#include <string>

namespace jai {

class class_registry {
public:
    checked_result<void> register_script_class(std::shared_ptr<script_class_definition> class_def);
    checked_result<void> register_cpp_class(std::shared_ptr<class_definition> class_def);
    std::shared_ptr<script_class_definition> find_script_class(const std::string& name);

    std::map<std::string, std::shared_ptr<script_class_definition>> script_classes_;
    std::map<std::string, std::shared_ptr<class_definition>> cpp_classes_;

private:
    void validate_class_hierarchy(const std::string& class_name);
};

// A slot declared `t` can own an instance whose destruction runs a script destructor: a by-value class, array or map whose
// value tree can hold one, a shared_ptr whose pointee's can (it may hold the last handle), a var or auto slot while any
// script class declares a destructor; never a primitive, a string, a weak_ptr or a reference. Decides which locals a scope
// exit destroys (both backends).
bool slot_may_own_destructor(engine& eng, const type_info_ptr& t);

} // namespace jai