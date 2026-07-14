#ifndef _WORKBENCH_COMMANDS_H_
#define _WORKBENCH_COMMANDS_H_

#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace Workbench {

	class Command {
	public:
		virtual ~Command() = default;
		virtual void execute() = 0;
		virtual void undo() = 0;
		virtual const std::string& label() const = 0;
	};

	// Concrete command over captured closures: most editor edits are a typed old/new pair
	// applied through one chokepoint, so the closures carry the state GoF puts in members.
	class LambdaCommand : public Command {
	public:
		LambdaCommand(std::string a_label, std::function<void()> a_execute, std::function<void()> a_undo) :
			commandLabel(std::move(a_label)),
			executeAction(std::move(a_execute)),
			undoAction(std::move(a_undo)) {
		}

		void execute() override { if (executeAction) { executeAction(); } }
		void undo() override { if (undoAction) { undoAction(); } }
		const std::string& label() const override { return commandLabel; }

	private:
		std::string commandLabel;
		std::function<void()> executeAction;
		std::function<void()> undoAction;
	};

	class CommandHistory {
	public:
		// Records an already-applied edit (the UI performed it live); execute() runs only on redo.
		void record(std::unique_ptr<Command> a_command) {
			undoStack.push_back(std::move(a_command));
			if (undoStack.size() > maximumDepth) {
				undoStack.pop_front();
			}
			redoStack.clear();
		}

		void record(std::string a_label, std::function<void()> a_execute, std::function<void()> a_undo) {
			record(std::make_unique<LambdaCommand>(std::move(a_label), std::move(a_execute), std::move(a_undo)));
		}

		bool undo() {
			if (undoStack.empty()) {
				return false;
			}
			auto command = std::move(undoStack.back());
			undoStack.pop_back();
			command->undo();
			redoStack.push_back(std::move(command));
			notifyChanged();
			return true;
		}

		bool redo() {
			if (redoStack.empty()) {
				return false;
			}
			auto command = std::move(redoStack.back());
			redoStack.pop_back();
			command->execute();
			undoStack.push_back(std::move(command));
			notifyChanged();
			return true;
		}

		bool canUndo() const { return !undoStack.empty(); }
		bool canRedo() const { return !redoStack.empty(); }

		void clear() {
			undoStack.clear();
			redoStack.clear();
		}

		// Fires after any undo/redo so the shell can refresh panels showing stale values.
		std::function<void()> onAfterChange;

	private:
		void notifyChanged() {
			if (onAfterChange) {
				onAfterChange();
			}
		}

		static constexpr size_t maximumDepth = 200;
		std::deque<std::unique_ptr<Command>> undoStack;
		std::deque<std::unique_ptr<Command>> redoStack;
	};

}

#endif
