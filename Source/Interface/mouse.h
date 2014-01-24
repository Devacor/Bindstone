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
			friend cereal::access;
			friend cereal::allocate<Clickable>;
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
			SlotRegister<MouseState::CallbackSignature>::SharedSignalType onMouseDownHandle;
			SlotRegister<MouseState::CallbackSignature>::SharedSignalType onMouseUpHandle;

			SlotRegister<MouseState::CallbackSignature>::SharedSignalType onMouseMoveHandle;

			SlotRegister<MouseState::CallbackSignature>::SharedSignalType onMouseButtonBeginHandle;
			SlotRegister<MouseState::CallbackSignature>::SharedSignalType onMouseButtonEndHandle;

			void hookUpSlots();

			void blockInput();
			void unblockInput();

			virtual void handleBegin(std::shared_ptr<BlockInteraction>);
			virtual void handleEnd(std::shared_ptr<BlockInteraction>){}

			bool mouseInBounds(const MouseState& a_state);

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<> objectLocationBeforeDrag;
			bool isInPressEvent;
			MouseState *mouse;

			bool eatTouches;
			bool shouldUseChildrenInHitDetection;

			int ourId;
			static int counter;

			template <class Archive>
			void serialize(Archive & archive){
				archive(CEREAL_NVP(eatTouches), CEREAL_NVP(shouldUseChildrenInHitDetection), CEREAL_NVP(ourId),
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(this)));
			}

			template <class Archive>
			static void load_and_allocate(Archive & archive, cereal::allocate<Clickable> &allocate){
				MouseState mouse;
				allocate(nullptr, mouse);
				archive(
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(allocate.get())),
					cereal::make_nvp("eatTouches", allocate->eatTouches),
					cereal::make_nvp("shouldUseChildrenInHitDetection", allocate->shouldUseChildrenInHitDetection),
					cereal::make_nvp("ourId", allocate->ourId)
				);
			}
		};

		struct ClickableSignals {
			Slot<Clickable::ButtonSlotSignature>::SharedSignalType press;
			Slot<Clickable::ButtonSlotSignature>::SharedSignalType release;

			//called when release happens over top of the current node.
			Slot<Clickable::ButtonSlotSignature>::SharedSignalType accept;
			//called when release happens outside of the current node.
			Slot<Clickable::ButtonSlotSignature>::SharedSignalType cancel;

			Slot<Clickable::DragSlotSignature>::SharedSignalType drag;
		};
	}
}

#endif
