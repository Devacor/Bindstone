#ifndef _MV_MOUSE_H_
#define _MV_MOUSE_H_

#include <SDL.h>
#include <memory>
#include "Render/points.h"
#include "Utility/signal.hpp"

namespace MV{
	struct ExclusiveMouseAction{
		ExclusiveMouseAction(bool a_exclusive, const std::vector<size_t> &a_fitness, const std::function<void()> &a_enabledCallback, const std::function<void()> &a_disableCallback, const std::string &a_name = ""):
			exclusive(a_exclusive),
			fitness(a_fitness),
			disabled(a_disableCallback),
			enabled(a_enabledCallback),
			name(a_name){
		}

		bool operator<(const ExclusiveMouseAction &a_rhs) const{
			for(size_t i = 0; i < fitness.size() && i < a_rhs.fitness.size(); ++i){
				if(fitness[i] < a_rhs.fitness[i]){
					return true;
				} else if(fitness[i] > a_rhs.fitness[i]){
					return false;
				}
			}
			return fitness.size() < a_rhs.fitness.size();
		}

		std::string name;
		bool exclusive;
		std::vector<size_t> fitness;
		std::function<void()> disabled;
		std::function<void()> enabled;
	};

	class MouseState {
	public:
		MouseState();

		typedef void CallbackSignature(MouseState&);
		typedef std::shared_ptr<Receiver<CallbackSignature>> SignalType;

		void update();

		MV::Point<int> position() const;
		MV::Point<int> oldPosition() const;

		bool leftDown() const;
		bool rightDown() const;
		bool middleDown() const;

		void queueExclusiveAction(const ExclusiveMouseAction &a_node);

		void runExclusiveActions();
	private:
		std::vector<ExclusiveMouseAction> nodesToExecute;
		bool left = false;
		bool middle = false;
		bool right = false;
		MV::Point<int> mousePosition;
		MV::Point<int> oldMousePosition;

		void updateButtonState(bool &oldState, bool newState, Signal<CallbackSignature> &onDown, Signal<CallbackSignature> &onUp, Signal<CallbackSignature> &onDownEnd, Signal<CallbackSignature> &onUpEnd);

		Signal<CallbackSignature> onLeftMouseDownSignal;
		Signal<CallbackSignature> onLeftMouseUpSignal;

		Signal<CallbackSignature> onLeftMouseDownEndSignal;
		Signal<CallbackSignature> onLeftMouseUpEndSignal;

		Signal<CallbackSignature> onRightMouseDownSignal;
		Signal<CallbackSignature> onRightMouseUpSignal;

		Signal<CallbackSignature> onRightMouseDownEndSignal;
		Signal<CallbackSignature> onRightMouseUpEndSignal;

		Signal<CallbackSignature> onMiddleMouseDownSignal;
		Signal<CallbackSignature> onMiddleMouseUpSignal;

		Signal<CallbackSignature> onMiddleMouseDownEndSignal;
		Signal<CallbackSignature> onMiddleMouseUpEndSignal;

		Signal<CallbackSignature> onMoveSignal;
	public:
		SignalRegister<CallbackSignature> onLeftMouseDown;
		SignalRegister<CallbackSignature> onLeftMouseUp;

		SignalRegister<CallbackSignature> onLeftMouseDownEnd;
		SignalRegister<CallbackSignature> onLeftMouseUpEnd;

		SignalRegister<CallbackSignature> onRightMouseDown;
		SignalRegister<CallbackSignature> onRightMouseUp;

		SignalRegister<CallbackSignature> onRightMouseDownEnd;
		SignalRegister<CallbackSignature> onRightMouseUpEnd;

		SignalRegister<CallbackSignature> onMiddleMouseDown;
		SignalRegister<CallbackSignature> onMiddleMouseUp;

		SignalRegister<CallbackSignature> onMiddleMouseDownEnd;
		SignalRegister<CallbackSignature> onMiddleMouseUpEnd;

		SignalRegister<CallbackSignature> onMove;
	};
}

#endif
