#ifndef _MV_MOUSE_H_
#define _MV_MOUSE_H_

#include <SDL.h>
#include <memory>
#include "Render/points.h"
#include "Utility/signal.hpp"

namespace MV{
	class MouseState {
	public:
		MouseState();
			

		typedef void CallbackSignature(const MouseState&);

		void update();

		MV::Point<int> position() const;
		MV::Point<int> oldPosition() const;

		bool leftDown() const;
		bool rightDown() const;
		bool middleDown() const;

	private:
		bool left;
		bool middle;
		bool right;
		MV::Point<int> mousePosition;
		MV::Point<int> oldMousePosition;

		void updateButtonState(bool &oldState, bool newState, Slot<CallbackSignature> &onDown, Slot<CallbackSignature> &onUp);

		Slot<CallbackSignature> onLeftMouseDownSlot;
		Slot<CallbackSignature> onLeftMouseUpSlot;

		Slot<CallbackSignature> onRightMouseDownSlot;
		Slot<CallbackSignature> onRightMouseUpSlot;

		Slot<CallbackSignature> onMiddleMouseDownSlot;
		Slot<CallbackSignature> onMiddleMouseUpSlot;

		Slot<CallbackSignature> onMoveSlot;
	public:
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
