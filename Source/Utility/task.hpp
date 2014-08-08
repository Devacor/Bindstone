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

namespace MV {

	class Task {
	private:
		Slot<void(const Task&)> onStartSlot;
		Slot<void(const Task&)> onFinishAllSlot;
		Slot<void(const Task&)> onFinishTaskSlot;

	public:
		Task():
			Task("root", [](double){return true; }){
		}

		Task(const std::string &a_name, std::function<bool(double)> a_task, bool a_blocking = true, bool a_blockParentCompletion = true):
			taskName(a_name),
			task(a_task),
			block(a_blocking),
			blockParentCompletion(a_blockParentCompletion),
			totalTime(0.0),
			ourTaskComplete(false),
			forceCompleteFlag(false),
			onStart(onStartSlot),
			onFinishAll(onFinishAllSlot),
			onFinishTask(onFinishTaskSlot){
		}

		static Task make(const std::string &a_name, std::function<bool(double)> a_task, bool a_blocking = true, bool a_blockParentCompletion = true){
			return Task(a_name, a_task, a_blocking, a_blockParentCompletion);
		}

		bool update(double a_dt){
			if(a_dt > 0.0f){
				updateLocalTask(a_dt);
				updateParallelTasks(a_dt);
				if(ourTaskComplete && updateChildTasks(a_dt)){
					onFinishAllSlot(*this);
					return true;
				}
			}
			return ourTaskComplete && noChildrenBlockingCompletion();
		}

		double elapsed() const{
			return totalTime;
		}

		void forceComplete(){
			forceCompleteFlag = true;
		}

		bool forceCompleted() const{
			return forceCompleteFlag;
		}

		bool blocking() const{
			return block;
		}

		Task& then(const std::string &a_name, std::function<bool(double)> a_task, bool a_blockParentCompletion = true) {
			sequentialTasks.emplace_back(std::make_shared<Task>(a_name, a_task, true, a_blockParentCompletion));
			return *this;
		}

		Task& thenAlso(const std::string &a_name, std::function<bool(double)> a_task, bool a_blockParentCompletion = true) {
			sequentialTasks.emplace_back(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
			return *this;
		}

		Task& also(const std::string &a_name, std::function<bool(double)> a_task, bool a_blockParentCompletion = true) {
			parallelTasks.emplace_back(std::make_shared<Task>(a_name, a_task, false, a_blockParentCompletion));
			return *this;
		}

		Task& get(const std::string &a_name){
			auto foundInSequentials = std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInSequentials != sequentialTasks.end()){
				return **foundInSequentials;
			}
			auto foundInParallels = std::find_if(parallelTasks.begin(), parallelTasks.end(), [&](const std::shared_ptr<Task> &a_task){
				return a_task->taskName == a_name;
			});
			if(foundInSequentials != sequentialTasks.end()){
				return **foundInParallels;
			}
			require<ResourceException>(false, "Failed to find: [", a_name, "] in task: [", taskName, "]");
		}

		SlotRegister<void(const Task&)> onStart;
		SlotRegister<void(const Task&)> onFinishTask;
		SlotRegister<void(const Task&)> onFinishAll;
	private:

		void updateLocalTask(double a_dt){
			if(totalTime == 0.0f){
				onStartSlot(*this);
			}
			totalTime += a_dt * static_cast<int>(forceCompleteFlag);
			if(!ourTaskComplete && (forceCompleteFlag || task(a_dt))){
				ourTaskComplete = true;
				onFinishTaskSlot(*this);
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
				return a_task->update(a_dt);
			}), parallelTasks.end());
		}

		bool noChildrenBlockingCompletion(){
			return 
				(std::find_if(parallelTasks.begin(), parallelTasks.end(), [](const std::shared_ptr<Task> &a_task){
					return a_task->blockParentCompletion;
				}) == parallelTasks.end()) &&
				(std::find_if(sequentialTasks.begin(), sequentialTasks.end(), [](const std::shared_ptr<Task> &a_task){
					return a_task->blockParentCompletion;
				}) == sequentialTasks.end());
		}

		void finishAllChildTasks(){
			for(std::shared_ptr<Task> &task : parallelTasks){
				if(!task->ourTaskComplete){
					task->onFinishTaskSlot(*task);
				}
				task->onFinishAllSlot(*task);
			}
			for(std::shared_ptr<Task> &task : sequentialTasks){
				if(!task->ourTaskComplete){
					task->onFinishTaskSlot(*task);
				}
				task->onFinishAllSlot(*task);
			}
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

		std::function<bool(double)> task;

		std::vector<std::shared_ptr<Task>> parallelTasks;
		std::deque<std::shared_ptr<Task>> sequentialTasks;

		bool forceCompleteFlag;
		std::string taskName;
		double totalTime;

		bool block;
		bool ourTaskComplete;

		bool blockParentCompletion;
	};
}

#endif
