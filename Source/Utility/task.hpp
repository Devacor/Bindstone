#ifndef _MV_TASK_H_
#define _MV_TASK_H_

#include <list>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include "Utility/require.hpp"
#include "Utility/signal.hpp"
#include "Utility/exactType.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"
#include "cereal/types/polymorphic.hpp"

#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

#include "chaiscript/utility/utility.hpp"

namespace MV {

	class Task;

	class ActionBase {
		friend Task;
		friend ::cereal::access;
	public:
		virtual std::string name() const { 
			return "Base";
		}

		virtual void onStart(Task& a_self) {}

		virtual void onFinish(Task& a_self) {}
		virtual void onFinishAll(Task& a_self) {}

		virtual void onSuspend(Task& a_self) {}
		virtual void onResume(Task& a_self) {}

		virtual void onCancel(Task& a_self) {}
		virtual void onCancelUpdate(Task& a_self) {}

		virtual void onException(Task& a_self, std::exception& e) {}

		//Return true on finish.
		virtual bool update(Task& a_self, double a_dt) {
			return true;
		}

	protected:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/) {
			archive(0);
		}

		virtual bool handleExceptions(){
			return false;
		}

		void initialize(Task* a_ourTask);
	};

	class BasicAction : public ActionBase {
		friend ::cereal::access;
	public:
		virtual std::string name() const override {
			return "BasicAction";
		}

		BasicAction(bool a_infinite = false) : infinite(a_infinite) {}

		virtual bool update(Task& a_self, double a_dt) override { return !infinite; }

	protected:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/) {
			archive(CEREAL_NVP(infinite), cereal::make_nvp("ActionBase", cereal::base_class<ActionBase>(this)));
		}

	private:

		bool infinite;
	};

	class Task {
	private:
		Signal<void(Task&)> onStartSignal;
		Signal<void(Task&)> onFinishSignal;
		Signal<void(Task&)> onFinishAllSignal;
		Signal<void(Task&)> onSuspendSignal;
		Signal<void(Task&)> onResumeSignal;
		Signal<void(Task&)> onCancelSignal;
		Signal<void(Task&)> onCancelUpdateSignal;

		Signal<void(Task&, std::exception &)> onExceptionSignal;

	public:
		Task(ExactType<bool> a_infinite = false, ExactType<bool> a_blocking = true, ExactType<bool> a_blockParentCompletion = true) :
			Task("root", a_infinite, a_blocking, a_blockParentCompletion) {
		}

		Task(const std::string &a_name, ExactType<bool> a_infinite = false, ExactType<bool> a_blocking = true, ExactType<bool> a_blockParentCompletion = true) :
			Task(a_name, std::make_shared<BasicAction>(a_infinite), a_blocking, a_blockParentCompletion) {
			if (a_infinite) {
				unblockChildren();
			}
		}

		Task(const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blocking = true, ExactType<bool> a_blockParentCompletion = true) :
			Task(a_name, Receiver<bool(Task&, double)>::make(a_task), a_blocking, a_blockParentCompletion) {
		}

		Task(const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blocking = true, ExactType<bool> a_blockParentCompletion = true) :
			taskName(a_name),
			task(a_task),
			block(a_blocking),
			blockParentCompletion(a_blockParentCompletion),
			onStart(onStartSignal),
			onFinishAll(onFinishAllSignal),
			onFinish(onFinishSignal),
			onCancel(onCancelSignal),
			onCancelUpdate(onCancelUpdateSignal),
			onSuspend(onSuspendSignal),
			onResume(onResumeSignal),
			onException(onExceptionSignal) {
		}

		Task(const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blocking = true, ExactType<bool> a_blockParentCompletion = true) :
			Task(a_name, std::function<bool(Task&, double)>([](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }), a_blocking, a_blockParentCompletion) {
			registerActionBase(a_action);
		}

		Task(const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blocking = true, ExactType<bool> a_blockParentCompletion = true) :
			Task(a_action->name(), [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blocking, a_blockParentCompletion) {
			registerActionBase(a_action);
		}

		~Task() {
			if (!cancelled) {
				if (!ourTaskComplete) {
					onCancelUpdateSignal(*this);
				}
				onCancelSignal(*this);
			}
		}

		Task* recent() {
			return (!mostRecentCreated.expired()) ? mostRecentCreated.lock().get() : nullptr;
		}

		Task& localInterval(double a_dt, size_t a_maxUpdates = 0) {
			localDeltaInterval = a_dt;
			maxUpdates = a_maxUpdates;
			return *this;
		}

		Task& interval(double a_dt, size_t a_maxUpdates = 0) {
			deltaInterval = a_dt;
			maxUpdates = a_maxUpdates;
			return *this;
		}

		std::string name() const{
			return taskName;
		}

		bool update(double a_dt){
			if (!finished()) {
				unsuspend();

				totalTime += a_dt;
				if (deltaInterval > 0) {
					totalUpdateIntervals();
				} else {
					lastCalledInterval = totalTime;
					totalUpdateStep(a_dt);
				}
			}
			return finished();
		}

		bool finished() {
			return cancelled || (ourTaskComplete && noChildrenBlockingCompletion());
		}

		double localElapsed() const {
			return lastCalledLocalInterval;
		}

		double elapsed() const {
			return lastCalledInterval;
		}

		Task& unblockChildren() {
			alwaysRunChildren = true;
			return *this;
		}

		Task& blockChildren() {
			alwaysRunChildren = false;
			return *this;
		}

		Task& cancel() {
			cancelAllChildren();
			if(!cancelled){
				cancelled = true;
				if (ourTaskStarted) {
					if (!ourTaskComplete) {
						ourTaskComplete = true;
						try { onCancelUpdateSignal(*this); }
						catch (std::exception &a_e) {
							handleCallbackException(a_e);
						} catch (chaiscript::Boxed_Value &bv) {
							handleChaiscriptException(bv);
						}
					}

					try { onCancelSignal(*this); }
					catch (std::exception &a_e) {
						handleCallbackException(a_e);
					} catch (chaiscript::Boxed_Value &bv) {
						handleChaiscriptException(bv);
					}
				}
			}
			return *this;
		}

		bool blocking() const{
			return block;
		}

		Task& now(const std::shared_ptr<Task> &a_other) {
			if (!sequentialTasks.empty()) {
				sequentialTasks.front()->suspend();
			}

			sequentialTasks.push_front(a_other);
			mostRecentCreated = a_other;
			resetFinishState();
			return *this;
		}

		Task& now(const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion = true) {
			return now(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& now(const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion = true) {
			return now(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& now(const std::string &a_name, ExactType<bool> a_blockParentCompletion = true){
			return now(a_name, std::make_shared<BasicAction>(false), a_blockParentCompletion);
		}

		Task& now(const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			now(a_name, [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blockParentCompletion);
			registerActionBase(a_action);
			return *this;
		}

		Task& now(const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			return now(a_action->name(), a_action, a_blockParentCompletion);
		}

		Task& then(const std::shared_ptr<Task> &a_other) {
			sequentialTasks.push_back(a_other);
			mostRecentCreated = a_other;
			resetFinishState();
			return *this;
		}

		Task& then(const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion = true) {
			return then(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& then(const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion = true) {
			return then(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& then(const std::string &a_name, ExactType<bool> a_blockParentCompletion = true){
			return then(a_name, std::make_shared<BasicAction>(false), a_blockParentCompletion);
		}

		Task& then(const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			then(a_name, [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blockParentCompletion);
			registerActionBase(a_action);
			return *this;
		}

		Task& then(const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			return then(a_action->name(), a_action, a_blockParentCompletion);
		}

		Task& thenAlso(const std::shared_ptr<Task> &a_other) {
			sequentialTasks.push_back(a_other);
			a_other->block = false;
			mostRecentCreated = a_other;
			resetFinishState();
			return *this;
		}

		Task& thenAlso(const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion = true) {
			return thenAlso(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
		}

		Task& thenAlso(const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion = true) {
			return thenAlso(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
		}

		Task& thenAlso(const std::string &a_name, ExactType<bool> a_infinite = false, ExactType<bool> a_blockParentCompletion = true) {
			thenAlso(a_name, std::make_shared<BasicAction>(a_infinite), a_blockParentCompletion);
			if (a_infinite) {
				recent()->unblockChildren();
			}
			return *this;
		}

		Task& thenAlso(const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			thenAlso(a_name, [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blockParentCompletion);
			registerActionBase(a_action);
			return *this;
		}

		Task& thenAlso(const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			return thenAlso(a_action->name(), a_action, a_blockParentCompletion);
		}

		Task& also(const std::shared_ptr<Task> &a_other) {
			parallelTasks.push_back(a_other);
			resetFinishState();
			mostRecentCreated = parallelTasks.back();
			return *this;
		}

		Task& also(const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion = true) {
			return also(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
		}

		Task& also(const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion = true) {
			return also(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
		}

		Task& also(const std::string &a_name, ExactType<bool> a_infinite = false, ExactType<bool> a_blockParentCompletion = true) {
			also(a_name, std::make_shared<BasicAction>(a_infinite), a_blockParentCompletion);
			if (a_infinite) {
				recent()->unblockChildren();
			}
			return *this;
		}

		Task& also(const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			also(a_name, [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blockParentCompletion);
			registerActionBase(a_action);
			return *this;
		}

		Task& also(const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			return also(a_action->name(), a_action, a_blockParentCompletion);
		}

		bool sequenceContains(const std::string &a_reference) const {
			return std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_item) {return a_item->name() == a_reference; }) != sequentialTasks.end();
		}

		Task& after(const std::string &a_reference, const std::shared_ptr<Task> &a_other) {
			auto found = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_item) {return a_item->name() == a_reference; });
			require<ResourceException>(found != sequentialTasks.end(), "Task::after missing [", a_reference, "]");
			++found;
			sequentialTasks.insert(found, a_other);
			mostRecentCreated = a_other;
			resetFinishState();
			return *this;
		}

		Task& after(const std::string &a_reference, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion = true) {
			return after(a_reference, std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& after(const std::string &a_reference, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion = true) {
			return after(a_reference, std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& after(const std::string &a_reference, const std::string &a_name, ExactType<bool> a_blockParentCompletion = true) {
			return after(a_reference, a_name, std::make_shared<BasicAction>(false), a_blockParentCompletion);
		}

		Task& after(const std::string &a_reference, const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			after(a_reference, a_name, [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blockParentCompletion);
			registerActionBase(a_action);
			return *this;
		}

		Task& after(const std::string &a_reference, std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			return after(a_reference, a_action->name(), a_action, a_blockParentCompletion);
		}

		Task& before(const std::string &a_reference, const std::shared_ptr<Task> &a_other) {
			auto found = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_item) {return a_item->name() == a_reference; });
			require<ResourceException>(found != sequentialTasks.end(), "Task::before missing [", a_reference, "]");
			if (found == sequentialTasks.begin()) {
				(*found)->suspend();
			}
			sequentialTasks.insert(found, a_other);
			mostRecentCreated = a_other;
			resetFinishState();
			return *this;
		}

		Task& before(const std::string &a_reference, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion = true) {
			return before(a_reference, std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& before(const std::string &a_reference, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion = true) {
			return before(a_reference, std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
		}

		Task& before(const std::string &a_reference, const std::string &a_name, ExactType<bool> a_blockParentCompletion = true) {
			return before(a_reference, a_name, std::make_shared<BasicAction>(false), a_blockParentCompletion);
		}

		Task& before(const std::string &a_reference, const std::string &a_name, const std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			before(a_reference, a_name, [](Task& a_self, double a_dt) {return a_self.optionalAction->update(a_self, a_dt); }, a_blockParentCompletion);
			registerActionBase(a_action);
			return *this;
		}

		Task& before(const std::string &a_reference, std::shared_ptr<ActionBase> &a_action, ExactType<bool> a_blockParentCompletion = true) {
			return before(a_reference, a_action->name(), a_action, a_blockParentCompletion);
		}

		Task* get(const std::string &a_name, bool a_throwOnNotFound = true){
			auto foundInSequentials = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInSequentials != sequentialTasks.end()){
				return (*foundInSequentials).get();
			}

			auto foundInParallels = std::find_if(parallelTasks.begin(), parallelTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInParallels != parallelTasks.end()){
				return (*foundInParallels).get();
			}

			require<ResourceException>(!a_throwOnNotFound, "Failed to find: [", a_name, "] in task: [", taskName, "]");
			return nullptr;
		}

		Task* getDeep(const std::string &a_name, bool a_throwOnNotFound = true) {
			Task* found = get(a_name, false);
			if (found) {
				return found;
			}

			for (auto&& task : sequentialTasks) {
				Task* found = task->getDeep(a_name, false);
				if (found) {
					return found;
				}
			}

			for (auto&& task : parallelTasks) {
				Task* found = task->getDeep(a_name, false);
				if (found) {
					return found;
				}
			}

			require<ResourceException>(!a_throwOnNotFound, "Failed to find: [", a_name, "] in task: [", taskName, "]");
			return nullptr;
		}

		SignalRegister<void(Task&)> onStart;
		SignalRegister<void(Task&)> onFinish;
		SignalRegister<void(Task&)> onFinishAll;
		SignalRegister<void(Task&)> onSuspend;
		SignalRegister<void(Task&)> onResume;
		SignalRegister<void(Task&)> onCancel;
		SignalRegister<void(Task&)> onCancelUpdate;
		SignalRegister<void(Task&, std::exception &)> onException;

        static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
        
		template <class Archive>
		void load(Archive & archive, std::uint32_t const /*version*/) {
			archive(
				CEREAL_NVP(task),
				CEREAL_NVP(onStartSignal),
				CEREAL_NVP(onFinishSignal),
				CEREAL_NVP(onFinishAllSignal),
				CEREAL_NVP(onFinishAllSignal),
				CEREAL_NVP(onSuspendSignal),
				CEREAL_NVP(onResumeSignal),
				CEREAL_NVP(onCancelSignal),
				CEREAL_NVP(onExceptionSignal),
				CEREAL_NVP(sequentialTasks),
				CEREAL_NVP(parallelTasks),
				CEREAL_NVP(taskName),
				CEREAL_NVP(totalTime),
				CEREAL_NVP(totalLocalTime),
				CEREAL_NVP(deltaInterval),
				CEREAL_NVP(localDeltaInterval),
				CEREAL_NVP(lastCalledInterval),
				CEREAL_NVP(lastCalledLocalInterval),
				CEREAL_NVP(currentStep),
				CEREAL_NVP(currentLocalStep),
				CEREAL_NVP(maxUpdates),
				CEREAL_NVP(block),
				CEREAL_NVP(suspended),
				CEREAL_NVP(ourTaskStarted),
				CEREAL_NVP(ourTaskComplete),
				CEREAL_NVP(blockParentCompletion),
				CEREAL_NVP(alwaysRunChildren),
				CEREAL_NVP(mostRecentCreated),
				CEREAL_NVP(optionalAction)
			);
			if (optionalAction) {
				task = MV::Receiver<bool(Task&,double)>::make([&](Task& a_self, double a_dt) -> bool {return a_self.optionalAction->update(a_self, a_dt); });
				optionalAction->initialize(this);
			} else if (!task || task->invalid()) {
				task = MV::Receiver<bool(Task&, double)>::make([&](Task&, double) -> bool {return true; });
			}
		}

		template <class Archive>
		void save(Archive & archive, std::uint32_t const /*version*/) const {
			archive(
				CEREAL_NVP(task),
				CEREAL_NVP(onStartSignal),
				CEREAL_NVP(onFinishSignal),
				CEREAL_NVP(onFinishAllSignal),
				CEREAL_NVP(onFinishAllSignal),
				CEREAL_NVP(onSuspendSignal),
				CEREAL_NVP(onResumeSignal),
				CEREAL_NVP(onCancelSignal),
				CEREAL_NVP(onExceptionSignal),
				CEREAL_NVP(sequentialTasks),
				CEREAL_NVP(parallelTasks),
				CEREAL_NVP(taskName),
				CEREAL_NVP(totalTime),
				CEREAL_NVP(totalLocalTime),
				CEREAL_NVP(deltaInterval),
				CEREAL_NVP(localDeltaInterval),
				CEREAL_NVP(lastCalledInterval),
				CEREAL_NVP(lastCalledLocalInterval),
				CEREAL_NVP(currentStep),
				CEREAL_NVP(currentLocalStep),
				CEREAL_NVP(maxUpdates),
				CEREAL_NVP(block),
				CEREAL_NVP(suspended),
				CEREAL_NVP(ourTaskStarted),
				CEREAL_NVP(ourTaskComplete),
				CEREAL_NVP(blockParentCompletion),
				CEREAL_NVP(alwaysRunChildren),
				CEREAL_NVP(mostRecentCreated),
				CEREAL_NVP(optionalAction)
			);
		}

	private:
		bool cancelled = false;

		void handleCallbackException(std::exception &a_e) {
			std::cerr << "Task [" << name() << "]:" << a_e.what() << std::endl;
			if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
			onExceptionSignal(*this, a_e);
		}

		void handleChaiscriptException(chaiscript::Boxed_Value & bv) {
			const char* whatContents = chaiscript::boxed_cast<chaiscript::exception::eval_error&>(bv).what();
			std::cerr << "Task [" << name() << "] script: " << chaiscript::boxed_cast<chaiscript::exception::eval_error&>(bv).what() << std::endl;
			if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
            auto errorWithContents = std::runtime_error(whatContents);
            std::exception &toThrow = errorWithContents;
			onExceptionSignal(*this, toThrow);
		}

		void registerActionBase(const std::shared_ptr<ActionBase> &a_base) {
			a_base->initialize(this);
			optionalAction = a_base;
		}

		void startIfNeeded() {
			if (!ourTaskStarted) {
				ourTaskStarted = true;

				try { onStartSignal(*this); }
				catch (std::exception &a_e) {
					handleCallbackException(a_e);
				}catch (chaiscript::Boxed_Value &bv) {
					handleChaiscriptException(bv);
				}
			}
		}

		void resetFinishState() {
			if (ourTaskComplete)
			{
				totalLocalTime = 0.0f;
			}
			onFinishAllSignal.unblock();
			onFinishSignal.unblock();
			cancelled = false;
		}

		void totalUpdateIntervals() {
			if (currentStep == 0 && !suspended && !finished()) {
				++currentStep;
				totalUpdateStep(0.0);
			}

			size_t steps = static_cast<size_t>((totalTime - lastCalledInterval) / deltaInterval);
			for (size_t i = 0; i < steps && !suspended && !finished() && (maxUpdates == 0 || i < maxUpdates); ++i) {
				++currentStep;
				lastCalledInterval += deltaInterval;
				totalUpdateStep(deltaInterval);
			}
		}

		void totalUpdateStep(double a_dt){
			try {
				if (!cancelled) {
					startIfNeeded();

					updateLocalTask(a_dt);
					updateParallelTasks(a_dt);
					if ((ourTaskComplete || alwaysRunChildren) && updateChildTasks(a_dt)) {
						onFinishAllSignal(*this);
						onFinishAllSignal.block();
					}
				}
			} catch (std::exception &a_e) {
				handleCallbackException(a_e);
			} catch (chaiscript::Boxed_Value &bv) {
				handleChaiscriptException(bv);
			}
		}

		void localTaskUpdateIntervals() {
			if (currentLocalStep == 0 && !suspended && !finished()) {
				++currentLocalStep;
				localTaskUpdateStep(0.0f);
			}

			size_t steps = static_cast<size_t>((totalLocalTime - lastCalledLocalInterval) / localDeltaInterval);
			for (size_t i = 0; i < steps && !suspended && !cancelled && !ourTaskComplete && (maxUpdates == 0 || i < maxUpdates); ++i) {
				++currentLocalStep;
				lastCalledLocalInterval += localDeltaInterval;
				localTaskUpdateStep(localDeltaInterval);
			}
		}

		void updateLocalTask(double a_dt){
			if (!ourTaskComplete) {
				totalLocalTime += a_dt;

				if (localDeltaInterval > 0) {
					localTaskUpdateIntervals();
				} else {
					lastCalledLocalInterval = totalLocalTime;
					localTaskUpdateStep(a_dt);
				}
			}
		}

		void localTaskUpdateStep(double a_dt) {
			if (!ourTaskComplete) {
				try {
					if (task->predicate(*this, a_dt)) {
						ourTaskComplete = true;
						try { onFinishSignal(*this); }
						catch (std::exception &a_e) {
							handleCallbackException(a_e);
						} catch (chaiscript::Boxed_Value &bv) {
							handleChaiscriptException(bv);
						}
						onFinishSignal.block();
					}
				} catch (std::exception &a_e) {
					handleCallbackException(a_e);
				} catch (chaiscript::Boxed_Value &bv) {
					handleChaiscriptException(bv);
				}
			}
		}

		void updateSequentialTasks(double a_dt){
			while (true) {
				auto firstBlockingTask = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [](const std::shared_ptr<Task> &a_task) {
					return a_task->block;
				});
				if (firstBlockingTask != sequentialTasks.begin()) {
					parallelTasks.insert(parallelTasks.end(), make_move_iterator(sequentialTasks.begin()), make_move_iterator(firstBlockingTask));
					sequentialTasks.erase(sequentialTasks.begin(), firstBlockingTask);
				}
				if (!sequentialTasks.empty()) {
					auto currentSequentialTask = sequentialTasks.begin();
					if ((*currentSequentialTask)->update(a_dt)) {
						sequentialTasks.erase(currentSequentialTask);
						a_dt = 0;
						continue; //Avoid forcing a frame between sequential task completions if they immediately finish.
					}
				}
				break;
			}
		}

		void updateParallelTasks(double a_dt){
			auto temporaryParallel = parallelTasks;
			std::set<std::shared_ptr<Task>> removeList;
			std::for_each(temporaryParallel.begin(), temporaryParallel.end(), [&](const std::shared_ptr<Task> &a_task){
				try { 
					if (a_task->update(a_dt)) {
						removeList.insert(a_task);
					}; 
				} catch (std::exception &a_e) {
					handleCallbackException(a_e);
					removeList.insert(a_task);
				} catch (chaiscript::Boxed_Value &bv) {
					handleChaiscriptException(bv);
					removeList.insert(a_task);
				}
			});

			parallelTasks.erase(std::remove_if(parallelTasks.begin(), parallelTasks.end(), [&](const std::shared_ptr<Task> &a_task) {
				return removeList.find(a_task) != removeList.end();
			}), parallelTasks.end());
		}

		bool noChildrenBlockingCompletion() const{
			return
				(std::find_if(parallelTasks.cbegin(), parallelTasks.cend(), [](const std::shared_ptr<Task> &a_task){
					return a_task->blockParentCompletion;
				}) == parallelTasks.end()) &&
				(std::find_if(sequentialTasks.cbegin(), sequentialTasks.cend(), [](const std::shared_ptr<Task> &a_task){
					return a_task->blockParentCompletion;
				}) == sequentialTasks.end());
		}

		void cancelAllChildren() {
			auto sequentialTasksToCancel = sequentialTasks;
			sequentialTasks.clear();
			for(auto&& task = sequentialTasksToCancel.rbegin();task != sequentialTasksToCancel.rend();++task){
				(*task)->cancel();
			}
			auto parallelTasksToCancel = parallelTasks;
			parallelTasks.clear();
			for(auto&& task = parallelTasksToCancel.rbegin();task != parallelTasksToCancel.rend();++task){
				(*task)->cancel();
			}
		}

		void finishOurTaskAndChildren() {
			bool allFinished = finished();
			if (!ourTaskComplete) {
				if(!ourTaskStarted){
					cancel();
				} else {
					ourTaskComplete = true;
					try { onFinishSignal(*this); }
					catch (std::exception &a_e) {
						handleCallbackException(a_e);
					} catch (chaiscript::Boxed_Value &bv) {
						handleChaiscriptException(bv);
					}
					onFinishSignal.block();
				}
			}
			finishAllChildTasks();
			if (!allFinished && finished()) {
				try { onFinishAllSignal(*this); }
				catch (std::exception &a_e) {
					handleCallbackException(a_e);
				} catch (chaiscript::Boxed_Value &bv) {
					handleChaiscriptException(bv);
				}
				onFinishAllSignal.block();
			}
		}

		void finishAllChildTasks(){
			auto parallelTasksToFinish = parallelTasks;
			parallelTasks.clear();
			for(std::shared_ptr<Task> &task : parallelTasksToFinish){
				task->finishOurTaskAndChildren();
			}
			auto sequentialTasksToFinish = sequentialTasks;
			sequentialTasks.clear();
			for(std::shared_ptr<Task> &task : sequentialTasksToFinish){
				task->finishOurTaskAndChildren();
			}
		}

		bool cleanupChildTasks(){
			if(noChildrenBlockingCompletion()){
				finishAllChildTasks();
				if(noChildrenBlockingCompletion()){
					return true;
				}
			}
			return false;
		}

		bool updateChildTasks(double a_dt){
			updateSequentialTasks(a_dt);

			return cleanupChildTasks();
		}

		void unsuspend() {
			if (suspended) {
				suspended = false;
				try { onResumeSignal(*this); }
				catch (std::exception &a_e) {
					handleCallbackException(a_e);
				} catch (chaiscript::Boxed_Value &bv) {
					handleChaiscriptException(bv);
				}
			}
		}

		void suspend() {
			if (!suspended && ourTaskStarted) {
				suspended = true;
				try { onSuspendSignal(*this); }
				catch (std::exception &a_e) {
					handleCallbackException(a_e);
				} catch (chaiscript::Boxed_Value &bv) {
					handleChaiscriptException(bv);
				}
			}
		}

		Receiver<bool(Task&, double)>::SharedType task;

		std::list<std::shared_ptr<Task>> parallelTasks;
		std::list<std::shared_ptr<Task>> sequentialTasks;

		std::string taskName;
		double totalTime = 0.0;
		double totalLocalTime = 0.0;
		double deltaInterval = 0.0;
		double localDeltaInterval = 0.0;
		double lastCalledInterval = 0.0;
		double lastCalledLocalInterval = 0.0;

		uint64_t currentStep = 0;
		uint64_t currentLocalStep = 0;

		size_t maxUpdates = 0;

		bool block;
		bool suspended = false;
		bool ourTaskComplete = false;
		bool ourTaskStarted = false;

		bool blockParentCompletion;
		bool alwaysRunChildren = false;

		std::weak_ptr<Task> mostRecentCreated;

		std::shared_ptr<ActionBase> optionalAction;
	};

	inline void ActionBase::initialize(Task* a_ourTask) {
		a_ourTask->onStart.connect("onStart", [&](Task& a_self) {onStart(a_self); });
		a_ourTask->onFinish.connect("onFinish", [&](Task& a_self) {onFinish(a_self); });
		a_ourTask->onFinishAll.connect("onFinishAll", [&](Task& a_self) {onFinishAll(a_self); });
		a_ourTask->onSuspend.connect("onSuspend", [&](Task& a_self) {onSuspend(a_self); });
		a_ourTask->onResume.connect("onResume", [&](Task& a_self) {onResume(a_self); });
		a_ourTask->onCancel.connect("onCancel", [&](Task& a_self) {onCancel(a_self); });
		a_ourTask->onCancelUpdate.connect("onCancelUpdate", [&](Task& a_self) {onCancelUpdate(a_self); });

		//If we register onException it will trap exceptions even if the ActionBase does nothing with it, so this needs to conditionally register.
		if (handleExceptions()) {
			a_ourTask->onException.connect("onException", [&](Task& a_self, std::exception &a_e) {onException(a_self, a_e); });
		}
	}
    
    inline chaiscript::ChaiScript& Task::hook(chaiscript::ChaiScript &a_script) {
        a_script.add(chaiscript::user_type<Task>(), "Task");
        a_script.add(chaiscript::constructor<Task()>(), "Task");
        a_script.add(chaiscript::constructor<Task(ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(ExactType<bool>, ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, ExactType<bool>, ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, std::function<bool(Task&, double)>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, std::function<bool(Task&, double)>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, std::function<bool(Task&, double)>, ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, const Receiver<bool(Task&, double)>::SharedType &)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, const Receiver<bool(Task&, double)>::SharedType &, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, const Receiver<bool(Task&, double)>::SharedType &, ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, const std::shared_ptr<ActionBase> &a_action)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, const std::shared_ptr<ActionBase> &a_action, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string &, const std::shared_ptr<ActionBase> &a_action, ExactType<bool>, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::shared_ptr<ActionBase> &a_action)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::shared_ptr<ActionBase> &a_action, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::shared_ptr<ActionBase> &a_action, ExactType<bool>, ExactType<bool>)>(), "Task");
        
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.now(a_name, a_task, a_blockParentCompletion); }), "now");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task) { return &a_self.now(a_name, a_task); }), "now");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.now(a_name, a_task, a_blockParentCompletion); }), "now");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task) { return &a_self.now(a_name, a_task); }), "now");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.now(a_name, a_blockParentCompletion); }), "now");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name) { return &a_self.now(a_name); }), "now");
        a_script.add(chaiscript::fun([](Task &a_self, const std::shared_ptr<Task> &a_task) { return &a_self.now(a_task); }), "now");
        
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.then(a_name, a_task, a_blockParentCompletion); }), "then");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task) { return &a_self.then(a_name, a_task); }), "then");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.then(a_name, a_task, a_blockParentCompletion); }), "then");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task) { return &a_self.then(a_name, a_task); }), "then");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.then(a_name, a_blockParentCompletion); }), "then");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name) { return &a_self.then(a_name); }), "then");
        a_script.add(chaiscript::fun([](Task &a_self, const std::shared_ptr<Task> &a_task) { return &a_self.then(a_task); }), "then");
        
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.thenAlso(a_name, a_task, a_blockParentCompletion); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task) { return &a_self.thenAlso(a_name, a_task); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.thenAlso(a_name, a_task, a_blockParentCompletion); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task) { return &a_self.thenAlso(a_name, a_task); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, ExactType<bool> a_infinite, ExactType<bool> a_blockParentCompletion) { return &a_self.thenAlso(a_name, a_infinite, a_blockParentCompletion); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, ExactType<bool> a_infinite) { return &a_self.thenAlso(a_name, a_infinite); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name) { return &a_self.thenAlso(a_name); }), "thenAlso");
        a_script.add(chaiscript::fun([](Task &a_self, const std::shared_ptr<Task> &a_task) { return &a_self.thenAlso(a_task); }), "thenAlso");
        
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.also(a_name, a_task, a_blockParentCompletion); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task) { return &a_self.also(a_name, a_task); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.also(a_name, a_task, a_blockParentCompletion); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, std::function<bool(Task&, double)> a_task) { return &a_self.also(a_name, a_task); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, ExactType<bool> a_infinite, ExactType<bool> a_blockParentCompletion) { return &a_self.also(a_name, a_infinite, a_blockParentCompletion); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name, ExactType<bool> a_infinite) { return &a_self.also(a_name, a_infinite); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_name) { return &a_self.also(a_name); }), "also");
        a_script.add(chaiscript::fun([](Task &a_self, const std::shared_ptr<Task> &a_task) { return &a_self.also(a_task); }), "also");
        
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.after(a_reference, a_name, a_task, a_blockParentCompletion); }), "after");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task) { return &a_self.after(a_reference, a_name, a_task); }), "after");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.after(a_reference, a_name, a_task, a_blockParentCompletion); }), "after");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, std::function<bool(Task&, double)> a_task) { return &a_self.after(a_reference, a_name, a_task); }), "after");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.after(a_reference, a_name, a_blockParentCompletion); }), "after");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name) { return &a_self.after(a_reference, a_name); }), "after");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::shared_ptr<Task> &a_task) { return &a_self.after(a_reference, a_task); }), "after");
        
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.before(a_reference, a_name, a_task, a_blockParentCompletion); }), "before");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, const Receiver<bool(Task&, double)>::SharedType &a_task) { return &a_self.before(a_reference, a_name, a_task); }), "before");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, std::function<bool(Task&, double)> a_task, ExactType<bool> a_blockParentCompletion) { return &a_self.before(a_reference, a_name, a_task, a_blockParentCompletion); }), "before");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, std::function<bool(Task&, double)> a_task) { return &a_self.before(a_reference, a_name, a_task); }), "before");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name, ExactType<bool> a_blockParentCompletion) { return &a_self.before(a_reference, a_name, a_blockParentCompletion); }), "before");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::string &a_name) { return &a_self.before(a_reference, a_name); }), "before");
        a_script.add(chaiscript::fun([](Task &a_self, const std::string &a_reference, const std::shared_ptr<Task> &a_task) { return &a_self.before(a_reference, a_task); }), "before");
        
        a_script.add(chaiscript::fun(&Task::update), "update");
        
        a_script.add(chaiscript::fun(&Task::recent), "last");
        a_script.add(chaiscript::fun(&Task::interval), "interval");
        a_script.add(chaiscript::fun(&Task::blocking), "blocking");
        a_script.add(chaiscript::fun(&Task::name), "name");
        a_script.add(chaiscript::fun(&Task::finished), "finished");
        a_script.add(chaiscript::fun(&Task::cancel), "cancel");
        a_script.add(chaiscript::fun(&Task::localElapsed), "localElapsed");
        a_script.add(chaiscript::fun(&Task::elapsed), "elapsed");
        a_script.add(chaiscript::fun(&Task::unblockChildren), "unblockChildren");
        a_script.add(chaiscript::fun(&Task::blockChildren), "blockChildren");
        a_script.add(chaiscript::fun(&Task::get), "get");
        a_script.add(chaiscript::fun(&Task::getDeep), "getDeep");
        
        MV::SignalRegister<bool(Task&, double)>::hook(a_script);
        MV::SignalRegister<void(Task&)>::hook(a_script);
        MV::SignalRegister<void(Task&, std::exception &)>::hook(a_script);
        a_script.add(chaiscript::fun(&Task::onStart), "onStart");
        a_script.add(chaiscript::fun(&Task::onFinish), "onFinish");
        a_script.add(chaiscript::fun(&Task::onFinishAll), "onFinishAll");
        a_script.add(chaiscript::fun(&Task::onSuspend), "onSuspend");
        a_script.add(chaiscript::fun(&Task::onResume), "onResume");
        a_script.add(chaiscript::fun(&Task::onCancel), "onCancel");
        a_script.add(chaiscript::fun(&Task::onException), "onException");
        
        return a_script;
    }

}

CEREAL_REGISTER_TYPE(MV::BasicAction);

#endif
