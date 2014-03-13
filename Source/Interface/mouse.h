#ifndef _MV_MOUSE_H_
#define _MV_MOUSE_H_

#include <SDL.h>
#include <memory>
#include "Render/points.h"
#include "Utility/signal.hpp"
#include "Render/scene.h"

namespace MV{
	class MouseState {
	public:
		MouseState();

		typedef void CallbackSignature(MouseState&);
		typedef std::shared_ptr<Signal<CallbackSignature>> SignalType;

		void update();

		MV::Point<int> position() const;
		MV::Point<int> oldPosition() const;

		bool leftDown() const;
		bool rightDown() const;
		bool middleDown() const;
		Slot<CallbackSignature> onMouseButtonBeginSlot;
	private:
		bool left = false;
		bool middle = false;
		bool right = false;
		MV::Point<int> mousePosition;
		MV::Point<int> oldMousePosition;

		void updateButtonState(bool &oldState, bool newState, Slot<CallbackSignature> &onDown, Slot<CallbackSignature> &onUp);
		
		//Slot<CallbackSignature> onMouseButtonBeginSlot;
		Slot<CallbackSignature> onMouseButtonEndSlot;

		Slot<CallbackSignature> onLeftMouseDownSlot;
		Slot<CallbackSignature> onLeftMouseUpSlot;

		Slot<CallbackSignature> onRightMouseDownSlot;
		Slot<CallbackSignature> onRightMouseUpSlot;

		Slot<CallbackSignature> onMiddleMouseDownSlot;
		Slot<CallbackSignature> onMiddleMouseUpSlot;

		Slot<CallbackSignature> onMoveSlot;
	public:
		SlotRegister<CallbackSignature> onMouseButtonBegin;
		SlotRegister<CallbackSignature> onMouseButtonEnd;

		SlotRegister<CallbackSignature> onLeftMouseDown;
		SlotRegister<CallbackSignature> onLeftMouseUp;

		SlotRegister<CallbackSignature> onRightMouseDown;
		SlotRegister<CallbackSignature> onRightMouseUp;

		SlotRegister<CallbackSignature> onMiddleMouseDown;
		SlotRegister<CallbackSignature> onMiddleMouseUp;

		SlotRegister<CallbackSignature> onMove;
	};
}

#endif
