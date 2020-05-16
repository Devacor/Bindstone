#include "signal.hpp"
#include "task.h"

namespace MV {
	Script::Registrar<Task> _hookTask([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Task>(), "Task");
		a_script.add(chaiscript::constructor<Task()>(), "Task");
		a_script.add(chaiscript::constructor<Task(ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(ExactType<bool>, ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, ExactType<bool>, ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, std::function<bool(Task&, double)>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, std::function<bool(Task&, double)>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, std::function<bool(Task&, double)>, ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, const Receiver<bool(Task&, double)>::SharedType&)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, const Receiver<bool(Task&, double)>::SharedType&, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, const Receiver<bool(Task&, double)>::SharedType&, ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, const std::shared_ptr<ActionBase>& a_action)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, const std::shared_ptr<ActionBase>& a_action, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::string&, const std::shared_ptr<ActionBase>& a_action, ExactType<bool>, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::shared_ptr<ActionBase>& a_action)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::shared_ptr<ActionBase>& a_action, ExactType<bool>)>(), "Task");
		a_script.add(chaiscript::constructor<Task(const std::shared_ptr<ActionBase>& a_action, ExactType<bool>, ExactType<bool>)>(), "Task");

		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.now(a_name, a_task, a_blockParentCompletion); }), "now");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task) { return &a_self.now(a_name, a_task); }), "now");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.now(a_name, a_task, a_blockParentCompletion); }), "now");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task) { return &a_self.now(a_name, a_task); }), "now");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.now(a_name, a_blockParentCompletion); }), "now");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name) { return &a_self.now(a_name); }), "now");
		a_script.add(chaiscript::fun([](Task& a_self, const std::shared_ptr<Task>& a_task) { return &a_self.now(a_task); }), "now");

		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.then(a_name, a_task, a_blockParentCompletion); }), "then");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task) { return &a_self.then(a_name, a_task); }), "then");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.then(a_name, a_task, a_blockParentCompletion); }), "then");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task) { return &a_self.then(a_name, a_task); }), "then");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.then(a_name, a_blockParentCompletion); }), "then");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name) { return &a_self.then(a_name); }), "then");
		a_script.add(chaiscript::fun([](Task& a_self, const std::shared_ptr<Task>& a_task) { return &a_self.then(a_task); }), "then");

		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.thenAlso(a_name, a_task, a_blockParentCompletion); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task) { return &a_self.thenAlso(a_name, a_task); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.thenAlso(a_name, a_task, a_blockParentCompletion); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task) { return &a_self.thenAlso(a_name, a_task); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, ExactType<bool> a_infinite, ExactType<bool> a_blockParentCompletion) { return &a_self.thenAlso(a_name, a_infinite, a_blockParentCompletion); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, ExactType<bool> a_infinite) { return &a_self.thenAlso(a_name, a_infinite); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name) { return &a_self.thenAlso(a_name); }), "thenAlso");
		a_script.add(chaiscript::fun([](Task& a_self, const std::shared_ptr<Task>& a_task) { return &a_self.thenAlso(a_task); }), "thenAlso");

		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.also(a_name, a_task, a_blockParentCompletion); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task) { return &a_self.also(a_name, a_task); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.also(a_name, a_task, a_blockParentCompletion); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, std::function<bool(Task&, double)> a_task) { return &a_self.also(a_name, a_task); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, ExactType<bool> a_infinite, ExactType<bool> a_blockParentCompletion) { return &a_self.also(a_name, a_infinite, a_blockParentCompletion); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name, ExactType<bool> a_infinite) { return &a_self.also(a_name, a_infinite); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_name) { return &a_self.also(a_name); }), "also");
		a_script.add(chaiscript::fun([](Task& a_self, const std::shared_ptr<Task>& a_task) { return &a_self.also(a_task); }), "also");

		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.after(a_reference, a_name, a_task, a_blockParentCompletion); }), "after");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task) { return &a_self.after(a_reference, a_name, a_task); }), "after");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.after(a_reference, a_name, a_task, a_blockParentCompletion); }), "after");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, std::function<bool(Task&, double)> a_task) { return &a_self.after(a_reference, a_name, a_task); }), "after");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.after(a_reference, a_name, a_blockParentCompletion); }), "after");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name) { return &a_self.after(a_reference, a_name); }), "after");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::shared_ptr<Task>& a_task) { return &a_self.after(a_reference, a_task); }), "after");

		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.before(a_reference, a_name, a_task, a_blockParentCompletion); }), "before");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, const Receiver<bool(Task&, double)>::SharedType& a_task) { return &a_self.before(a_reference, a_name, a_task); }), "before");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.before(a_reference, a_name, a_task, a_blockParentCompletion); }), "before");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, std::function<bool(Task&, double)> a_task) { return &a_self.before(a_reference, a_name, a_task); }), "before");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.before(a_reference, a_name, a_blockParentCompletion); }), "before");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::string& a_name) { return &a_self.before(a_reference, a_name); }), "before");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_reference, const std::shared_ptr<Task>& a_task) { return &a_self.before(a_reference, a_task); }), "before");

		a_script.add(chaiscript::fun(&Task::update), "update");

		a_script.add(chaiscript::fun(&Task::recent), "last");
		a_script.add(chaiscript::fun(&Task::interval), "interval");
		a_script.add(chaiscript::fun(&Task::blocking), "blocking");
		a_script.add(chaiscript::fun(&Task::name), "name");
		a_script.add(chaiscript::fun(&Task::finished), "finished");
		a_script.add(chaiscript::fun([](Task& a_self) {a_self.cancel(); }), "cancel");
		a_script.add(chaiscript::fun([](Task& a_self, const std::string& a_id) {a_self.cancel(a_id); }), "cancel");
		a_script.add(chaiscript::fun(&Task::localElapsed), "localElapsed");
		a_script.add(chaiscript::fun(&Task::elapsed), "elapsed");
		a_script.add(chaiscript::fun(&Task::unblockChildren), "unblockChildren");
		a_script.add(chaiscript::fun(&Task::blockChildren), "blockChildren");
		a_script.add(chaiscript::fun(&Task::get), "get");
		a_script.add(chaiscript::fun(&Task::getDeep), "getDeep");

		a_script.add(chaiscript::fun(&Task::onStart), "onStart");
		a_script.add(chaiscript::fun(&Task::onFinish), "onFinish");
		a_script.add(chaiscript::fun(&Task::onFinishAll), "onFinishAll");
		a_script.add(chaiscript::fun(&Task::onSuspend), "onSuspend");
		a_script.add(chaiscript::fun(&Task::onResume), "onResume");
		a_script.add(chaiscript::fun(&Task::onCancel), "onCancel");
		a_script.add(chaiscript::fun(&Task::onException), "onException");
	});

	ScriptSignalRegistrar<bool(Task&, double)> _taskUpdateSignal{};
	ScriptSignalRegistrar<bool(Task&)> _taskDoSignal{};
	ScriptSignalRegistrar<bool(Task&, std::exception&)> _taskExceptionSignal{};
}