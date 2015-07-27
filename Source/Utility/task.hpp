#ifndef _MV_TASK_H_
#define _MV_TASK_H_

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include "Utility/require.hpp"
#include "Utility/signal.hpp"
#include "Utility/optionalCalls.hpp"

namespace MV {

	class Task {
	private:
		Signal<void(const Task&)> onStartSignal;
		Signal<void(const Task&)> onFinishSignal;
		Signal<void(const Task&)> onFinishAllSignal;
		Signal<void(const Task&)> onSuspendSignal;
		Signal<void(const Task&)> onResumeSignal;
		Signal<void()> onCancelSignal;

		Signal<void(const Task&, std::exception &)> onExceptionSignal;

	public:
		Task(bool a_infinite = false, bool a_blocking = true, bool a_blockParentCompletion = true):
			Task("root", [a_infinite](const Task&, double){return !a_infinite;}, a_blocking, a_blockParentCompletion){
			if (a_infinite){
				unblockChildren();
			}
		}

		Task(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blocking = true, bool a_blockParentCompletion = true):
			taskName(a_name),
			task(a_task),
			block(a_blocking),
			blockParentCompletion(a_blockParentCompletion),
			alwaysRunChildren(false),
			totalTime(0.0),
			localDeltaInterval(0.0),
			lastCalledLocalInterval(0.0),
			deltaInterval(0.0),
			lastCalledInterval(0.0),
			ourTaskComplete(false),
			forceCompleteFlag(false),
			suspended(false),
			onStart(onStartSignal),
			onFinishAll(onFinishAllSignal),
			onFinish(onFinishSignal),
			onCancel(onCancelSignal),
			onSuspend(onSuspendSignal),
			onResume(onResumeSignal),
			onException(onExceptionSignal),
			mostRecentCreated(nullptr){
		}

		~Task() {
			if (!ourTaskComplete && !cancelled) {
				onCancelSignal();
			}
		}

		static Task make(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blocking = true, bool a_blockParentCompletion = true){
			return Task(a_name, a_task, a_blocking, a_blockParentCompletion);
		}

		std::shared_ptr<Task> last() {
			return mostRecentCreated;
		}

		Task& interval(double a_dt) {
			localDeltaInterval = a_dt;
			return *this;
		}

		std::string name() const{
			return taskName;
		}

		bool update(double a_dt){
			if (!finished()) {
				try {
					unsuspend();
				} catch (std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
					onExceptionSignal(*this, a_e);
				}
				
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
		
		Task& finish() {
			forceCompleteFlag = true;
			for(auto&& task : parallelTasks){
				task->finish();
			}
			for(auto&& task : sequentialTasks){
				task->finish();
			}
			return *this;
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

		Task& finishLocal(){
			forceCompleteFlag = true;
			return *this;
		}

		Task& cancel() {
			cancelAllChildren();
			if(!cancelled){
				cancelled = true;
				if(!ourTaskComplete){
					ourTaskComplete = true;
					try { onCancelSignal(); } catch (std::exception &a_e) {
						if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
						onExceptionSignal(*this, a_e);
					}
				}
			}
			return *this;
		}

		bool forceCompleted() const{
			return forceCompleteFlag;
		}

		bool blocking() const{
			return block;
		}

		Task& now(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			if(!sequentialTasks.empty()){
				sequentialTasks[0]->suspend();
			}
			
			sequentialTasks.emplace_front(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
			mostRecentCreated = sequentialTasks.front();
			onFinishAllSignal.unblock();
			return *this;
		}
		
		Task& now(const std::string &a_name, bool a_blockParentCompletion = true){
			return now(a_name, [](const Task&, double){return true;}, a_blockParentCompletion);
		}

		Task& then(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			sequentialTasks.emplace_back(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
			onFinishAllSignal.unblock();
			mostRecentCreated = sequentialTasks.back();
			return *this;
		}
		
		Task& then(const std::string &a_name, bool a_blockParentCompletion = true){
			return then(a_name, [](const Task&, double){return true;}, a_blockParentCompletion);
		}

		Task& thenAlso(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			sequentialTasks.emplace_back(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
			onFinishAllSignal.unblock();
			mostRecentCreated = sequentialTasks.back();
			return *this;
		}

		Task& thenAlso(const std::string &a_name, bool a_infinite = false, bool a_blockParentCompletion = true) {
			thenAlso(a_name, [a_infinite](const Task&, double){return !a_infinite;}, a_blockParentCompletion);
			if (a_infinite) {
				last()->unblockChildren();
			}
			return *this;
		}

		Task& also(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			parallelTasks.emplace_back(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
			onFinishAllSignal.unblock();
			mostRecentCreated = parallelTasks.back();
			return *this;
		}
		
		Task& also(const std::string &a_name, bool a_infinite = false, bool a_blockParentCompletion = true) {
			also(a_name, [a_infinite](const Task&, double){return !a_infinite;}, a_blockParentCompletion);
			if (a_infinite) {
				last()->unblockChildren();
			}
			return *this;
		}

		std::shared_ptr<Task> get(const std::string &a_name, bool a_throwOnNotFound = true){
			auto foundInSequentials = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInSequentials != sequentialTasks.end()){
				return (*foundInSequentials);
			}
			for(auto&& taskList : temporarySequentialTasks){
				foundInSequentials = std::find_if(taskList.begin(), taskList.end(), [&](const std::shared_ptr<Task> &a_task){
					return a_task->taskName == a_name;
				});
				if(foundInSequentials != taskList.end()){
					return (*foundInSequentials);
				}
			}

			auto foundInParallels = std::find_if(parallelTasks.begin(), parallelTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInParallels != parallelTasks.end()){
				return (*foundInParallels);
			}
			for(auto&& taskList : temporaryParallelTasks){
				foundInParallels = std::find_if(taskList.begin(), taskList.end(), [&](const std::shared_ptr<Task> &a_task){
					return a_task->taskName == a_name;
				});
				if(foundInParallels != taskList.end()){
					return (*foundInParallels);
				}
			}
			
			require<ResourceException>(!a_throwOnNotFound, "Failed to find: [", a_name, "] in task: [", taskName, "]");
			return std::shared_ptr<Task>();
		}

		SignalRegister<void(const Task&)> onStart;
		SignalRegister<void(const Task&)> onFinish;
		SignalRegister<void(const Task&)> onFinishAll;
		SignalRegister<void(const Task&)> onSuspend;
		SignalRegister<void(const Task&)> onResume;
		SignalRegister<void()> onCancel;
		SignalRegister<void(const Task&, std::exception &)> onException;
	private:
		bool cancelled = false;

		void totalUpdateIntervals() {
			size_t steps = static_cast<size_t>((totalTime - lastCalledInterval) / deltaInterval);
			for (size_t i = 0; i < steps && !suspended && !finished(); ++i) {
				lastCalledInterval += deltaInterval;
				totalUpdateStep(deltaInterval);
			}
		}
		
		void totalUpdateStep(double a_dt){
			try {
				if (!cancelled) {
					if (a_dt > 0.0) {
						updateLocalTask(a_dt);
						updateParallelTasks(a_dt);
						if ((ourTaskComplete || alwaysRunChildren) && updateChildTasks(a_dt)) {
							onFinishAllSignal(*this);
							onFinishAllSignal.block();
						}
					}
				}
			} catch (std::exception &a_e) {
				if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
				onExceptionSignal(*this, a_e);
			}
		}

		void localTaskUpdateIntervals() {
			size_t steps = static_cast<size_t>((totalTime - lastCalledLocalInterval) / localDeltaInterval);
			for (size_t i = 0; i < steps; ++i) {
				lastCalledLocalInterval += localDeltaInterval;
				localTaskUpdateStep(localDeltaInterval);
			}
		}

		void updateLocalTask(double a_dt){
			if (!ourTaskComplete) {
				if (totalTime == 0.0f) {
					try { onStartSignal(*this); } catch (std::exception &a_e) {
						if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
						onExceptionSignal(*this, a_e);
					}
				}
				totalTime += a_dt * static_cast<int>(!forceCompleteFlag);

				if (localDeltaInterval > 0) {
					localTaskUpdateIntervals();
				} else {
					lastCalledLocalInterval = totalTime;
					localTaskUpdateStep(a_dt);
				}
			}
		}

		void localTaskUpdateStep(double a_dt) {
			if (!ourTaskComplete) {
				try {
					if (forceCompleteFlag || task(*this, a_dt)) {
						forceCompleteFlag = false;
						ourTaskComplete = true;
						try { onFinishSignal(*this); } catch (std::exception &a_e) {
							if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
							onExceptionSignal(*this, a_e);
						}
						onFinishSignal.block();
					}
				} catch (std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
					onExceptionSignal(*this, a_e);
				}
			}
		}

		void updateSequentialTasks(double a_dt){
			auto firstBlockingTask = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [](const std::shared_ptr<Task> &a_task){
				return a_task->block;
			});
			if(firstBlockingTask != sequentialTasks.begin()){
				parallelTasks.insert(parallelTasks.end(), make_move_iterator(sequentialTasks.begin()), make_move_iterator(firstBlockingTask));
				sequentialTasks.erase(sequentialTasks.begin(), firstBlockingTask);
			}
			if(!sequentialTasks.empty()){
				auto currentSequentialTask = sequentialTasks.begin();
				try{
					if((*currentSequentialTask)->update(a_dt)){
						sequentialTasks.erase(currentSequentialTask);
					}
				} catch(std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) {
						sequentialTasks.erase(currentSequentialTask); 
						throw; 
					}

					onExceptionSignal(*this, a_e);
					sequentialTasks.erase(currentSequentialTask);
				}
			}
		}

		void updateParallelTasks(double a_dt){
			auto temporaryParallel = parallelTasks;
			temporaryParallelTasks.push_back(temporaryParallel);
			SCOPE_EXIT{ temporaryParallelTasks.pop_back(); };
			parallelTasks.clear();

			temporaryParallel.erase(std::remove_if(temporaryParallel.begin(), temporaryParallel.end(), [&](const std::shared_ptr<Task> &a_task){
				try { return a_task->update(a_dt); } catch (std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
					onExceptionSignal(*this, a_e);
					return true;
				}
			}), temporaryParallel.end());

			if(parallelTasks.empty()){
				parallelTasks = temporaryParallel;
			}else{
				auto accumulatedTasks = parallelTasks;
				parallelTasks = temporaryParallel;
				for(auto&& task : accumulatedTasks){
					parallelTasks.push_back(task);
				}
			}
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
			temporarySequentialTasks.push_back(sequentialTasksToCancel);
			SCOPE_EXIT{ temporarySequentialTasks.pop_back(); };
			sequentialTasks.clear();
			for(auto&& task = sequentialTasksToCancel.rbegin();task != sequentialTasksToCancel.rend();++task){
				(*task)->cancel();
			}
			auto parallelTasksToCancel = parallelTasks;
			temporaryParallelTasks.push_back(parallelTasksToCancel);
			SCOPE_EXIT{ temporaryParallelTasks.pop_back(); };
			parallelTasks.clear();
			for(auto&& task = parallelTasksToCancel.rbegin();task != parallelTasksToCancel.rend();++task){
				(*task)->cancel();
			}
		}

		void finishOurTaskAndChildren() {
			bool allFinished = finished();
			if (!ourTaskComplete) {
				ourTaskComplete = true;
				if(totalTime == 0){
					cancel();
				} else {
					try { onFinishSignal(*this); } catch (std::exception &a_e) {
						if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
						onExceptionSignal(*this, a_e);
					}
					onFinishSignal.block();
				}
			}
			finishAllChildTasks();
			if (!allFinished && finished()) {
				try { onFinishAllSignal(*this); } catch (std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
					onExceptionSignal(*this, a_e);
				}
				onFinishAllSignal.block();
			}
		}

		void finishAllChildTasks(){
			auto parallelTasksToFinish = parallelTasks;
			temporaryParallelTasks.push_back(parallelTasksToFinish);
			SCOPE_EXIT{ temporaryParallelTasks.pop_back(); };
			parallelTasks.clear();
			for(std::shared_ptr<Task> &task : parallelTasksToFinish){
				task->finishOurTaskAndChildren();
			}
			auto sequentialTasksToFinish = sequentialTasks;
			temporarySequentialTasks.push_back(sequentialTasksToFinish);
			SCOPE_EXIT{ temporarySequentialTasks.pop_back(); };
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
				try { onResumeSignal(*this); } catch (std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
					onExceptionSignal(*this, a_e);
				}
			}
		}

		void suspend() {
			if (!suspended && totalTime > 0) {
				suspended = true;
				try { onSuspendSignal(*this); } catch (std::exception &a_e) {
					if (onExceptionSignal.cullDeadObservers() == 0) { throw; }
					onExceptionSignal(*this, a_e);
				}
			}
		}

		std::function<bool(const Task&, double)> task;

		std::deque<std::shared_ptr<Task>> parallelTasks;
		std::deque<std::shared_ptr<Task>> sequentialTasks;

		std::vector<std::deque<std::shared_ptr<Task>>> temporaryParallelTasks;
		std::vector<std::deque<std::shared_ptr<Task>>> temporarySequentialTasks;

		bool forceCompleteFlag;
		std::string taskName;
		double totalTime;
		double deltaInterval;
		double localDeltaInterval;
		double lastCalledInterval;
		double lastCalledLocalInterval;

		bool block;
		bool suspended;
		bool ourTaskComplete;

		bool blockParentCompletion;
		bool alwaysRunChildren;

		std::shared_ptr<Task> mostRecentCreated;
	};

	#define CREATE_HOOK_UP_TASK_ACTION(member) \
	CREATE_HAS_MEMBER(member, (const Task&))\
	template <typename T, bool b>\
	struct HookUpTaskAction_##member { \
		static void apply(Task &a_root, const std::shared_ptr<T> &a_action) {\
			a_root.member.connect(#member , [=](const Task& a_self){\
				a_action->member(a_self);\
			});\
		}\
	};\
	template <typename T>\
	struct HookUpTaskAction_##member <T, false> {\
		static void apply(Task &a_root, const std::shared_ptr<T> &a_action) {}\
	};

	CREATE_HOOK_UP_TASK_ACTION(onStart);
	CREATE_HOOK_UP_TASK_ACTION(onFinish);
	CREATE_HOOK_UP_TASK_ACTION(onFinishChildren);

	template <typename T>
	Task& ParallelTask(Task &a_root, const T &a_action, bool a_blockParentCompletion = true){
		return ParallelTask(guid("Par_"), a_root, a_action, a_blockParentCompletion);
	}

	template <typename T>
	Task& ParallelTask(const std::string &a_id, Task &a_root, const T &a_action, bool a_blockParentCompletion = true){
		std::shared_ptr<T> sharedAction = std::make_shared<T>(a_action);
		a_root.also(a_id, [=](const Task& a_task, double a_dt){return sharedAction->update(a_task, a_dt); }, a_blockParentCompletion);
		auto& task = a_root.get(a_id);
		HookUpTaskAction_onStart<T, has_onStart<T>::value>::apply(task, sharedAction);
		HookUpTaskAction_onFinish<T, has_onFinish<T>::value>::apply(task, sharedAction);
		HookUpTaskAction_onFinishChildren<T, has_onFinishChildren<T>::value>::apply(task, sharedAction);
		return task;
	}

	template <typename T>
	Task& SequentialTask(Task &a_root, const T &a_action, bool a_blockParentCompletion = true){
		return SequentialTask(guid("SeqTask_"), a_root, a_action, a_blockParentCompletion);
	}

	template <typename T>
	Task& SequentialTask(const std::string &a_id, Task &a_root, const T &a_action, bool a_blockParentCompletion = true){
		std::shared_ptr<T> sharedAction = std::make_shared<T>(a_action);
		a_root.then(a_id, [=](const Task& a_task, double a_dt){return sharedAction->update(a_task, a_dt); }, a_blockParentCompletion);
		auto& task = a_root.get(a_id);
		HookUpTaskAction_onStart<T, has_onStart<T>::value>::apply(task, sharedAction);
		HookUpTaskAction_onFinish<T, has_onFinish<T>::value>::apply(task, sharedAction);
		HookUpTaskAction_onFinishChildren<T, has_onFinishChildren<T>::value>::apply(task, sharedAction);
		return task;
	}

	template <typename T>
	Task& SequentialNonBlockingTask(Task &a_root, const T &a_action, bool a_blockParentCompletion = true){
		return SequentialNonBlockingTask(guid("SeqTask_"), a_root, a_action, a_blockParentCompletion);
	}

	template <typename T>
	Task& SequentialNonBlockingTask(const std::string &a_id, Task &a_root, const T &a_action, bool a_blockParentCompletion = true){
		std::shared_ptr<T> sharedAction = std::make_shared<T>(a_action);
		a_root.thenAlso(a_id, [=](const Task& a_task, double a_dt){return sharedAction->update(a_task, a_dt); }, a_blockParentCompletion);
		auto& task = a_root.get(a_id);
		HookUpTaskAction_onStart<T, has_onStart<T>::value>::apply(task, sharedAction);
		HookUpTaskAction_onFinish<T, has_onFinish<T>::value>::apply(task, sharedAction);
		HookUpTaskAction_onFinishChildren<T, has_onFinishChildren<T>::value>::apply(task, sharedAction);
		return task;
	}
}

#endif