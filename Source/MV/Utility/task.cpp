#include "task.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "services.hpp"

// JaiScript binding for Task
static jai::registrar<MV::Task, MV::Services> _hookTask("Task", [](jai::dynamic_binder<MV::Task>& builder, const MV::Services&) {
	builder.auto_bind();

	// Additional constructors
	builder.constructor<const std::string&>();
	builder.constructor<const std::string&, bool>();
	builder.constructor<const std::string&, bool, bool>();
	builder.constructor<const std::string&, bool, bool, bool>();
	builder.constructor<const std::string&, std::function<bool(MV::Task&, double)>>();
	builder.constructor<const std::shared_ptr<MV::ActionBase>&>();

	// Overloaded methods
	builder.method("cancel", [](MV::Task& self) { self.cancel(); });
	builder.method("cancel", [](MV::Task& self, const std::string& id) { self.cancel(id); });

	builder.method("now", [](MV::Task& self, const std::string& name) -> MV::Task& { return self.now(name); });
	builder.method("now", [](MV::Task& self, const std::string& name, bool block) -> MV::Task& { return self.now(name, block); });
	builder.method("now", [](MV::Task& self, const std::string& name, std::function<bool(MV::Task&, double)> fn) -> MV::Task& { return self.now(name, fn); });
	builder.method("now", [](MV::Task& self, const std::shared_ptr<MV::Task>& task) -> MV::Task& { return self.now(task); });

	builder.method("then", [](MV::Task& self, const std::string& name) -> MV::Task& { return self.then(name); });
	builder.method("then", [](MV::Task& self, const std::string& name, bool block) -> MV::Task& { return self.then(name, block); });
	builder.method("then", [](MV::Task& self, const std::string& name, std::function<bool(MV::Task&, double)> fn) -> MV::Task& { return self.then(name, fn); });
	builder.method("then", [](MV::Task& self, const std::shared_ptr<MV::Task>& task) -> MV::Task& { return self.then(task); });

	builder.method("also", [](MV::Task& self, const std::string& name) -> MV::Task& { return self.also(name); });
	builder.method("also", [](MV::Task& self, const std::string& name, bool infinite) -> MV::Task& { return self.also(name, infinite); });
	builder.method("also", [](MV::Task& self, const std::string& name, std::function<bool(MV::Task&, double)> fn) -> MV::Task& { return self.also(name, fn); });
	builder.method("also", [](MV::Task& self, const std::shared_ptr<MV::Task>& task) -> MV::Task& { return self.also(task); });

	builder.method("thenAlso", [](MV::Task& self, const std::string& name) -> MV::Task& { return self.thenAlso(name); });
	builder.method("thenAlso", [](MV::Task& self, const std::string& name, bool infinite) -> MV::Task& { return self.thenAlso(name, infinite); });
	builder.method("thenAlso", [](MV::Task& self, const std::string& name, std::function<bool(MV::Task&, double)> fn) -> MV::Task& { return self.thenAlso(name, fn); });
	builder.method("thenAlso", [](MV::Task& self, const std::shared_ptr<MV::Task>& task) -> MV::Task& { return self.thenAlso(task); });

	builder.method("after", [](MV::Task& self, const std::string& ref, const std::string& name) -> MV::Task& { return self.after(ref, name); });
	builder.method("after", [](MV::Task& self, const std::string& ref, const std::string& name, bool block) -> MV::Task& { return self.after(ref, name, block); });
	builder.method("after", [](MV::Task& self, const std::string& ref, const std::shared_ptr<MV::Task>& task) -> MV::Task& { return self.after(ref, task); });

	builder.method("before", [](MV::Task& self, const std::string& ref, const std::string& name) -> MV::Task& { return self.before(ref, name); });
	builder.method("before", [](MV::Task& self, const std::string& ref, const std::string& name, bool block) -> MV::Task& { return self.before(ref, name, block); });
	builder.method("before", [](MV::Task& self, const std::string& ref, const std::shared_ptr<MV::Task>& task) -> MV::Task& { return self.before(ref, task); });
});
