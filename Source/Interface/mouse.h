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
		
		Slot<CallbackSignature> onMouseButtonBeginSlot;
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

	namespace Scene {
		struct BlockInteraction : public MV::Message {
			static std::shared_ptr<BlockInteraction> make(std::shared_ptr<Node> a_sender){ return std::make_shared<BlockInteraction>(a_sender); }
			BlockInteraction(std::shared_ptr<Node> a_sender):sender(a_sender){}
			std::shared_ptr<Node> sender;
		};

		class Clickable :
			public Rectangle,
			public MessageHandler<BlockInteraction>{
		public:
			typedef void ButtonSlotSignature(std::shared_ptr<Clickable>);
			typedef void DragSlotSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &currentPosition);
		private:

			Slot<ButtonSlotSignature> onPressSlot;
			Slot<ButtonSlotSignature> onReleaseSlot;

			//called when release happens over top of the current node.
			Slot<ButtonSlotSignature> onAcceptSlot;
			//called when release happens outside of the current node.
			Slot<ButtonSlotSignature> onCancelSlot;

			Slot<DragSlotSignature> onDragSlot;

		public:
			SCENE_MAKE_FACTORY_METHODS

			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState &a_mouse);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState &a_mouse, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState &a_mouse, const Point<> &a_topLeft, const Point<> &a_bottomRight);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState &a_mouse, const Point<> &a_point, Size<> &a_size, bool a_center);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState &a_mouse, const Size<> &a_size);

			void includeChildrenForHitDetection(){
				shouldUseChildrenInHitDetection = true;
			}
			void ignoreChildrenForHitDetection(){
				shouldUseChildrenInHitDetection = false;
			}

			bool inPressEvent() const;

			void startEatingTouches();
			void stopEatingTouches();
			bool isEatingTouches() const;

			const MouseState& getMouse() const;

			Point<> locationBeforeDrag() const;

			SlotRegister<ButtonSlotSignature> onPress;
			SlotRegister<ButtonSlotSignature> onRelease;
			SlotRegister<ButtonSlotSignature> onAccept;
			SlotRegister<ButtonSlotSignature> onCancel;

			SlotRegister<DragSlotSignature> onDrag;

		protected:
			Clickable(Draw2D *a_renderer, MouseState &a_mouse);
		private:
			SlotRegister<MouseState::CallbackSignature>::SignalType onMouseDownHandle;
			SlotRegister<MouseState::CallbackSignature>::SignalType onMouseUpHandle;

			SlotRegister<MouseState::CallbackSignature>::SignalType onMouseMoveHandle;

			SlotRegister<MouseState::CallbackSignature>::SignalType onMouseButtonBeginHandle;
			SlotRegister<MouseState::CallbackSignature>::SignalType onMouseButtonEndHandle;

			void hookUpSlots();

			void blockInput();
			void unblockInput();

			virtual void handleBegin(std::shared_ptr<BlockInteraction>);
			virtual void handleEnd(std::shared_ptr<BlockInteraction>){}

			bool mouseInBounds(const MouseState& a_state);

			std::weak_ptr<Clickable> thisSharedPtr;
			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<> objectLocationBeforeDrag;
			bool eatTouches;
			bool isInPressEvent;
			bool shouldUseChildrenInHitDetection;
			MouseState &mouse;

			int ourId;
			static int counter;
		};

		struct ClickableSignals {
			Slot<Clickable::ButtonSlotSignature>::SignalType press;
			Slot<Clickable::ButtonSlotSignature>::SignalType release;

			//called when release happens over top of the current node.
			Slot<Clickable::ButtonSlotSignature>::SignalType accept;
			//called when release happens outside of the current node.
			Slot<Clickable::ButtonSlotSignature>::SignalType cancel;

			Slot<Clickable::DragSlotSignature>::SignalType drag;
		};
	}
}

#endif
