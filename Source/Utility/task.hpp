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
		Slot<void(const Task&)> onStartSlot;
		Slot<void(const Task&)> onFinishSlot;
		Slot<void(const Task&)> onFinishAllSlot;
		Slot<void()> onCancelSlot;

		Slot<void(const Task&, std::exception &)> onExceptionSlot;

	public:
		Task():
			Task("root", [](const Task&, double){return true; }){
		}

		Task(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blocking = true, bool a_blockParentCompletion = true):
			taskName(a_name),
			task(a_task),
			block(a_blocking),
			blockParentCompletion(a_blockParentCompletion),
			totalTime(0.0),
			minimumInterval(0.0),
			lastCalledInterval(0.0),
			ourTaskComplete(false),
			forceCompleteFlag(false),
			onStart(onStartSlot),
			onFinishAll(onFinishAllSlot),
			onFinish(onFinishSlot),
			onCancel(onCancelSlot),
			onException(onExceptionSlot){
		}

		~Task() {
			if (!ourTaskComplete && !cancelled) {
				onCancelSlot();
			}
		}

		static Task make(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blocking = true, bool a_blockParentCompletion = true){
			return Task(a_name, a_task, a_blocking, a_blockParentCompletion);
		}

		public Task interval(double a_dt) {
			minimumInterval = a_dt;
			return this;
		}

		std::string name() const{
			return taskName;
		}

		bool update(double a_dt){
			if (!cancelled) {
				if (a_dt > 0.0f) {
					updateLocalTask(a_dt);
					updateParallelTasks(a_dt);
					if (ourTaskComplete && updateChildTasks(a_dt)) {
						try { onFinishAllSlot(*this); } catch (std::exception &a_e) {
							if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
							onExceptionSlot(*this, a_e);
						}
						onFinishAllSlot.block();
						return true;
					}
				}
			}
			return finished();
		}

		bool finished() {
			return cancelled || ourTaskComplete && noChildrenBlockingCompletion();
		}

		double elapsed() const{
			return lastCalledInterval;
		}

		void forceComplete(){
			forceCompleteFlag = true;
		}

		void cancel() {
			cancelled = true;
			try { onCancelSlot(); } catch (std::exception &a_e) {
				if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
				onExceptionSlot(*this, a_e);
			}
		}

		bool forceCompleted() const{
			return forceCompleteFlag;
		}

		bool blocking() const{
			return block;
		}

		Task& then(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			sequentialTasks.emplace_back(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
			onFinishAllSlot.unblock();
			return *this;
		}

		Task& thenAlso(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			sequentialTasks.emplace_back(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
			onFinishAllSlot.unblock();
			return *this;
		}

		Task& also(const std::string &a_name, std::function<bool(const Task&, double)> a_task, bool a_blockParentCompletion = true) {
			parallelTasks.emplace_back(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
			onFinishAllSlot.unblock();
			return *this;
		}

		Task* get(const std::string &a_name, bool a_throwOnNotFound = true){
			auto foundInSequentials = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInSequentials != sequentialTasks.end()){
				return &(**foundInSequentials);
			}
			auto foundInParallels = std::find_if(parallelTasks.begin(), parallelTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInParallels != parallelTasks.end()){
				return &(**foundInParallels);
			}
			require<ResourceException>(!a_throwOnNotFound, "Failed to find: [", a_name, "] in task: [", taskName, "]");
			return nullptr;
		}

		SlotRegister<void(const Task&)> onStart;
		SlotRegister<void(const Task&)> onFinish;
		SlotRegister<void(const Task&)> onFinishAll;
		SlotRegister<void()> onCancel;
		SlotRegister<void(const Task&, std::exception &)> onException;
	private:
		bool cancelled = false;

		void updateLocalTask(double a_dt){
			if (!ourTaskComplete)
			{
				if (totalTime == 0.0f) {
					try { onStartSlot(*this); } catch (std::exception &a_e) {
						if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
						onExceptionSlot(*this, a_e);
					}
				}
				totalTime += a_dt * static_cast<int>(!forceCompleteFlag);

				if (minimumInterval > 0) {
					LocalTaskUpdateIntervals();
				} else {
					lastCalledInterval = totalTime;
					LocalTaskUpdateStep(a_dt);
				}
			}
		}

		void LocalTaskUpdateIntervals() {
			int steps = (int)((totalTime - lastCalledInterval) / minimumInterval);
			for (int i = 0; i < steps; ++i)
			{
				lastCalledInterval += minimumInterval;
				LocalTaskUpdateStep(minimumInterval);
			}
		}

		void LocalTaskUpdateStep(double a_dt) {
			if (!ourTaskComplete) {
				try {
					if (forceCompleteFlag || task(*this, a_dt)) {
						forceCompleteFlag = false;
						ourTaskComplete = true;
						try { onFinishSlot(*this); } catch (std::exception &a_e) {
							if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
							onExceptionSlot(*this, a_e);
						}
						onFinishSlot.block();
					}
				} catch (std::exception &a_e) {
					if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
					onExceptionSlot(*this, a_e);
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
			if(!sequentialTasks.empty() && sequentialTasks.front()->update(a_dt)){
				sequentialTasks.pop_front();
			}
		}

		void updateParallelTasks(double a_dt){
			parallelTasks.erase(std::remove_if(parallelTasks.begin(), parallelTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				try { return a_task->update(a_dt); } catch (std::exception &a_e) {
					if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
					onExceptionSlot(*this, a_e);
					return true;
				}
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

		void finishOurTaskAndChildren() {
			bool allFinished = finished();
			if (!ourTaskComplete) {
				ourTaskComplete = true;
				try { onFinishSlot(*this); } catch (std::exception &a_e) {
					if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
					onExceptionSlot(*this, a_e);
				}
			}
			finishAllChildTasks();
			if (!allFinished) {
				try { onFinishAllSlot(*this); } catch (std::exception &a_e) {
					if (onExceptionSlot.cullDeadObservers() == 0) { throw; }
					onExceptionSlot(*this, a_e);
				}
			}
		}

		void finishAllChildTasks(){
			for(std::shared_ptr<Task> &task : parallelTasks){
				task->finishOurTaskAndChildren();
			}
			parallelTasks.clear();
			for(std::shared_ptr<Task> &task : sequentialTasks){
				task->finishOurTaskAndChildren();
			}
			sequentialTasks.clear();
		}

		bool cleanupChildTasks(){
			if(noChildrenBlockingCompletion()){
				finishAllChildTasks();
				return true;
			}
			return false;
		}

		bool updateChildTasks(double a_dt){
			updateSequentialTasks(a_dt);

			return cleanupChildTasks();
		}

		std::function<bool(const Task&, double)> task;

		std::vector<std::shared_ptr<Task>> parallelTasks;
		std::deque<std::shared_ptr<Task>> sequentialTasks;

		bool forceCompleteFlag;
		std::string taskName;
		double totalTime;
		double minimumInterval;
		double lastCalledInterval;

		bool block;
		bool ourTaskComplete;

		bool blockParentCompletion;
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
