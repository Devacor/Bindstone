#ifndef _WORKBENCH_COMMANDS_H_
#define _WORKBENCH_COMMANDS_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Workbench {

	// A command refers to the objects it edits by pointer. That holds because nothing leaves the scene except through
	// a command that keeps the removed object and puts the same object back: one that removes must own what it removed.
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

	// Several commands as one step: redone in the order recorded, undone in reverse.
	class CommandGroup : public Command {
	public:
		explicit CommandGroup(std::string a_label) : commandLabel(std::move(a_label)) {}

		void add(std::unique_ptr<Command> a_command) { steps.push_back(std::move(a_command)); }
		bool empty() const { return steps.empty(); }

		void execute() override {
			for (auto&& step : steps) {
				step->execute();
			}
		}

		void undo() override {
			for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
				(*step)->undo();
			}
		}

		const std::string& label() const override { return commandLabel; }

	private:
		std::string commandLabel;
		std::vector<std::unique_ptr<Command>> steps;
	};

	class CommandHistory {
	public:
		// While one lives, every record() joins a single step under the label of the outermost.
		class Group {
		public:
			Group(CommandHistory& a_history, std::string a_label) : history(&a_history) {
				history->openGroup(std::move(a_label));
			}
			~Group() {
				if (history) {
					history->closeGroup();
				}
			}
			Group(Group&& a_other) noexcept : history(a_other.history) { a_other.history = nullptr; }
			Group(const Group&) = delete;
			Group& operator=(const Group&) = delete;
			Group& operator=(Group&&) = delete;

		private:
			CommandHistory* history;
		};

		[[nodiscard]] Group group(std::string a_label) { return Group(*this, std::move(a_label)); }

		// Records an already-applied edit (the UI performed it live); execute() runs only on redo.
		void record(std::unique_ptr<Command> a_command) {
			if (pendingGroup) {
				pendingGroup->add(std::move(a_command));
				return;
			}
			undoStack.push_back({ std::move(a_command), ++lastSerial });
			if (undoStack.size() > maximumDepth) {
				baseSerial = undoStack.front().serial;
				undoStack.pop_front();
			}
			redoStack.clear();
			notifyMoved();
		}

		void record(std::string a_label, std::function<void()> a_execute, std::function<void()> a_undo) {
			record(std::make_unique<LambdaCommand>(std::move(a_label), std::move(a_execute), std::move(a_undo)));
		}

		bool undo() {
			if (undoStack.empty() || pendingGroup) {
				return false;
			}
			auto step = std::move(undoStack.back());
			undoStack.pop_back();
			step.command->undo();
			redoStack.push_back(std::move(step));
			notifyReplayed();
			return true;
		}

		bool redo() {
			if (redoStack.empty() || pendingGroup) {
				return false;
			}
			auto step = std::move(redoStack.back());
			redoStack.pop_back();
			step.command->execute();
			undoStack.push_back(std::move(step));
			notifyReplayed();
			return true;
		}

		bool canUndo() const { return !undoStack.empty(); }
		bool canRedo() const { return !redoStack.empty(); }
		// Empty when there is nothing to undo or redo.
		const std::string& undoLabel() const { return undoStack.empty() ? noLabel : undoStack.back().command->label(); }
		const std::string& redoLabel() const { return redoStack.empty() ? noLabel : redoStack.back().command->label(); }

		// The document on disk matches the state the history is at now.
		void markSaved() {
			savedSerial = currentSerial();
			notifyMoved();
		}

		// False again once undo or redo returns to the saved state; a saved state no undo can reach stays modified.
		bool modified() const { return currentSerial() != savedSerial; }

		// A new document: no steps, and its state is the saved one.
		void clear() {
			undoStack.clear();
			redoStack.clear();
			baseSerial = ++lastSerial;
			savedSerial = baseSerial;
			notifyMoved();
		}

		// Fires after any undo/redo so the shell can refresh panels showing stale values.
		std::function<void()> onAfterChange;
		// Fires whenever the position in the history, or the saved mark, moves.
		std::function<void()> onMoved;

	private:
		void openGroup(std::string a_label) {
			if (groupDepth++ == 0) {
				pendingGroup = std::make_unique<CommandGroup>(std::move(a_label));
			}
		}

		void closeGroup() {
			if (--groupDepth == 0) {
				auto finished = std::move(pendingGroup);
				if (!finished->empty()) {
					record(std::move(finished));
				}
			}
		}

		// Names the state the document is in: the last step applied, or the base no step can undo past.
		std::uint64_t currentSerial() const { return undoStack.empty() ? baseSerial : undoStack.back().serial; }

		void notifyMoved() {
			if (onMoved) {
				onMoved();
			}
		}

		void notifyReplayed() {
			if (onAfterChange) {
				onAfterChange();
			}
			notifyMoved();
		}

		static constexpr size_t maximumDepth = 200;
		static inline const std::string noLabel;
		struct Step {
			std::unique_ptr<Command> command;
			std::uint64_t serial;
		};

		std::deque<Step> undoStack;
		std::deque<Step> redoStack;
		std::uint64_t lastSerial = 0;
		std::uint64_t baseSerial = 0;
		std::uint64_t savedSerial = 0;

		std::unique_ptr<CommandGroup> pendingGroup;
		int groupDepth = 0;
	};

}

#endif
