#ifndef _MV_COMPOSITESCENE_H_
#define _MV_COMPOSITESCENE_H_

#include "Render/scene.h"
#include "Interface/mouse.h"

namespace MV {
	namespace Scene {
		class Clipped :
			public Rectangle,
			public MessageHandler<VisualChange>{

			friend cereal::access;
			friend cereal::construct<Clipped>;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS

			static std::shared_ptr<Clipped> make(Draw2D* a_renderer);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_bottomRight);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Point<> &a_point, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Size<> &a_size);

			virtual ~Clipped(){}

			void refreshTexture(bool a_forceRefresh = false);

		protected:
			Clipped(Draw2D *a_renderer):
				Rectangle(a_renderer),
				dirtyTexture(true){
			}

		private:
			virtual bool preDraw();

			virtual void handleBegin(std::shared_ptr<VisualChange>){
				dirtyTexture = true;
			}
			virtual void handleEnd(std::shared_ptr<VisualChange>){
			}

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clipped> &construct){
				construct(nullptr);
				archive(
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(construct.ptr()))
				);
			}

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;

			bool dirtyTexture;
		};

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
			friend cereal::construct<Clickable>;
			friend Button;
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

			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_topLeft, const Point<> &a_bottomRight);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_point, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Clickable> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size);

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

			void hookUpSlots();
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
				construct(nullptr, mouse);
				archive(
					cereal::make_nvp("eatTouches", construct->eatTouches),
					cereal::make_nvp("shouldUseChildrenInHitDetection", construct->shouldUseChildrenInHitDetection),
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(construct.ptr()))
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

		class Button : public Node {
			friend cereal::access;
			friend cereal::construct<Clickable>;
		public:
			typedef void ButtonSlotSignature(std::shared_ptr<Clickable>);
			typedef void DragSlotSignature(std::shared_ptr<Clickable>, const Point<int> &startPosition, const Point<int> &currentPosition);

		private:
			std::shared_ptr<Clickable> clickable; //must appear before the SlotRegisters

		public:
			SCENE_MAKE_FACTORY_METHODS

			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse);
			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_topLeft, const Point<> &a_bottomRight);
			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_point, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Button> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size);
			
			SlotRegister<ButtonSlotSignature> onPress;
			SlotRegister<ButtonSlotSignature> onRelease;
			SlotRegister<ButtonSlotSignature> onAccept;
			SlotRegister<ButtonSlotSignature> onCancel;

			SlotRegister<DragSlotSignature> onDrag;

			MouseState* getMouse() const;

			std::shared_ptr<Node> activeScene() const;
			std::shared_ptr<Node> idleScene() const;

			std::shared_ptr<Node> activeScene(std::shared_ptr<Node> a_mouseUp);
			std::shared_ptr<Node> idleScene(std::shared_ptr<Node> a_mouseDown);

			void setClickableSize(const Size<> &a_size);

			void setClickableTwoCorners(const Point<> &a_topLeft, const Point<> &a_bottomRight);
			void setClickableTwoCorners(const BoxAABB &a_bounds);

			void setClickableSizeAndCenterPoint(const Point<> &a_centerPoint, const Size<> &a_size);
			void setClickableSizeAndCornerPoint(const Point<> &a_cornerPoint, const Size<> &a_size);
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
				construct(nullptr, mouse);
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
