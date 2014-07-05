#ifndef _MV_SCENE_BUTTON_H_
#define _MV_SCENE_BUTTON_H_

#include "Interface/mouse.h"
#include "Render/Scene/primitives.h"

namespace MV {
	namespace Scene {
		struct BlockInteraction : public MV::Message {
			static std::shared_ptr<BlockInteraction> make(std::shared_ptr<Node> a_sender){ return std::make_shared<BlockInteraction>(a_sender); }
			BlockInteraction(std::shared_ptr<Node> a_sender):sender(a_sender){}
			std::shared_ptr<Node> sender;
		};

		class Button;
		class Clickable :
			public Rectangle,
			public MessageHandler<BlockInteraction>{

			friend cereal::access;
			friend Button;
		public:
			typedef void ButtonSlotSignature(std::shared_ptr<Clickable>);
			typedef void DragSlotSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition);
		private:

			Slot<ButtonSlotSignature> onPressSlot;
			Slot<ButtonSlotSignature> onReleaseSlot;

			//called when release happens over top of the current node.
			Slot<ButtonSlotSignature> onAcceptSlot;
			//called when release happens outside of the current node.
			Slot<ButtonSlotSignature> onCancelSlot;

			Slot<DragSlotSignature> onDragSlot;

		public:
			typedef Slot<Clickable::ButtonSlotSignature>::SharedSignalType Click;
			typedef Slot<Clickable::DragSlotSignature>::SharedSignalType Drag;

			SCENE_MAKE_FACTORY_METHODS(Clickable)
			RECTANGLE_OVERRIDES(Clickable)

			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB);

			SlotRegister<ButtonSlotSignature> onPress;
			SlotRegister<ButtonSlotSignature> onRelease;
			SlotRegister<ButtonSlotSignature> onAccept;
			SlotRegister<ButtonSlotSignature> onCancel;

			SlotRegister<DragSlotSignature> onDrag;

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

			MouseState* getMouse() const;

			Point<> locationBeforeDrag() const;

			virtual ~Clickable();

		protected:
			Clickable(Draw2D *a_renderer, MouseState *a_mouse);

		private:
			MouseState::SignalType onMouseDownHandle;
			MouseState::SignalType onMouseUpHandle;

			MouseState::SignalType onMouseMoveHandle;

			MouseState::SignalType onMouseButtonBeginHandle;
			MouseState::SignalType onMouseButtonEndHandle;

			std::shared_ptr<Clickable> hookUpSlots();
			void clearSlots();

			void blockInput();
			void unblockInput();

			virtual void handleBegin(std::shared_ptr<BlockInteraction>);
			virtual void handleEnd(std::shared_ptr<BlockInteraction>){}

			bool mouseInBounds(const MouseState& a_state);

			virtual void drawImplementation();

			Point<int> dragStartPosition;
			Point<int> priorMousePosition;
			Point<> objectLocationBeforeDrag;
			bool isInPressEvent;
			bool hookedUp;
			MouseState *mouse;

			bool eatTouches;
			bool shouldUseChildrenInHitDetection;

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					CEREAL_NVP(eatTouches),
					CEREAL_NVP(shouldUseChildrenInHitDetection),
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clickable> &construct){
				MouseState* mouse = nullptr;
				archive.extract(
					cereal::make_nvp("mouse", mouse)
				);
				require(mouse != nullptr, MV::PointerException("Error: Failed to load a mouse handle for Clickable node."));
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Clickable node."));
				construct(renderer, mouse);
				archive(
					cereal::make_nvp("eatTouches", construct->eatTouches),
					cereal::make_nvp("shouldUseChildrenInHitDetection", construct->shouldUseChildrenInHitDetection),
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(construct.ptr()))
				);
			}
		};

		class Button : public Node {
			friend cereal::access;
		public:
			typedef void ButtonSlotSignature(std::shared_ptr<Clickable>);
			typedef void DragSlotSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &deltaPosition);

		private:
			std::shared_ptr<Clickable> clickable; //must appear before the SlotRegisters

		public:
			SCENE_MAKE_FACTORY_METHODS(Button)

			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse);
			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB);

			SlotRegister<ButtonSlotSignature> onPress;
			SlotRegister<ButtonSlotSignature> onRelease;
			SlotRegister<ButtonSlotSignature> onAccept;
			SlotRegister<ButtonSlotSignature> onCancel;

			SlotRegister<DragSlotSignature> onDrag;

			MouseState* getMouse() const;

			std::shared_ptr<Node> activeScene() const;
			std::shared_ptr<Node> idleScene() const;

			std::shared_ptr<Button> activeScene(std::shared_ptr<Node> a_mouseUp);
			std::shared_ptr<Button> idleScene(std::shared_ptr<Node> a_mouseDown);

			std::shared_ptr<Button> clickSize(const Size<> &a_size, bool a_center = false);
			std::shared_ptr<Button> clickSize(const Size<> &a_size, const Point<>& a_centerPoint);
			
			std::shared_ptr<Button> clickBounds(const BoxAABB &a_boxAABB);

			virtual BoxAABB worldAABBImplementation(bool a_includeChildren, bool a_nestedCall);
			virtual BoxAABB screenAABBImplementation(bool a_includeChildren, bool a_nestedCall);
			virtual BoxAABB localAABBImplementation(bool a_includeChildren, bool a_nestedCall);
			virtual BoxAABB basicAABBImplementation() const;

		protected:
			Button(Draw2D *a_renderer, MouseState *a_mouse);

		private:
			std::shared_ptr<Node> idleSceneNode;
			std::shared_ptr<Node> activeSceneNode;

			virtual void drawImplementation();

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					CEREAL_NVP(clickable),
					cereal::make_nvp("idleScene", idleSceneNode),
					cereal::make_nvp("activeScene", activeSceneNode),
					cereal::make_nvp("node", cereal::base_class<Node>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Button> &construct){
				MouseState* mouse = nullptr;
				archive.extract(
					cereal::make_nvp("mouse", mouse)
				);
				require(mouse != nullptr, MV::PointerException("Error: Failed to load a mouse handle for Button node."));
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Button node."));
				construct(renderer, mouse);
				archive(
					cereal::make_nvp("clickable", construct->clickable),
					cereal::make_nvp("idleScene", construct->idleSceneNode),
					cereal::make_nvp("activeScene", construct->activeSceneNode),
					cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr()))
				);
			}
		};
	}
}

#endif
