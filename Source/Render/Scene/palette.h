#ifndef _MV_SCENE_PALETTE_H_
#define _MV_SCENE_PALETTE_H_

#include "drawable.h"
#include "Interface/mouse.h"

namespace MV {
	namespace Scene {

		class Palette : public Drawable {
			friend cereal::access;
			friend Node;
		public:
			typedef void PaletteSignalSignture(std::shared_ptr<Palette>);

		private:
			Signal<PaletteSignalSignture> onColorChangeSignal;
			Signal<PaletteSignalSignture> onSwatchClickedSignal;

		public:
			//void (std::shared_ptr<Palette>)
			SignalRegister<PaletteSignalSignture> onColorChange;
			//void (std::shared_ptr<Palette>)
			SignalRegister<PaletteSignalSignture> onSwatchClicked;

			DrawableDerivedAccessorsNoColor(Palette)

			MouseState& mouse() const;

			std::shared_ptr<Palette> color(const MV::Color &a_newColor);
			MV::Color color() const {
				return currentColor;
			}

			std::shared_ptr<Palette> padding(const Size<> &a_padding) {
				selectorPercentPadding = a_padding;
			}

			std::shared_ptr<Palette> selectorPercent(MV::Size<> a_newSize) {
				selectorPercentSize = a_newSize;
			}

			std::shared_ptr<Palette> colorAlphaSelector(const Color& a_left, const Color& a_right);

			BoxAABB<> bounds() {
				return boundsImplementation();
			}

			std::shared_ptr<Palette> bounds(const BoxAABB<> &a_bounds);

			std::shared_ptr<Palette> size(const Size<> &a_size, const Point<> &a_centerPoint) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				Point<> topLeft;
				Point<> bottomRight = toPoint(a_size);

				topLeft -= a_centerPoint;
				bottomRight -= a_centerPoint;

				return bounds({ topLeft, bottomRight });
			}

			std::shared_ptr<Palette> size(const Size<> &a_size, bool a_center = false) {
				return size(a_size, (a_center) ? MV::point(a_size.width / 2.0f, a_size.height / 2.0f) : MV::point(0.0f, 0.0f));
			}

			size_t globalPriority() const;
			std::shared_ptr<Palette> globalPriority(size_t a_newPriority);

			std::vector<size_t> overridePriority() const;
			std::shared_ptr<Palette> overridePriority(const std::vector<size_t> &a_newPriority) {
				overrideClickPriority = a_newPriority;
				return std::static_pointer_cast<Palette>(shared_from_this());
			}

			void startEatingTouches();
			void stopEatingTouches();
			bool eatingTouches() const;
		protected:
			Palette(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse);

			void innerColorAlphaSelectorNoNotification(const Color& a_left, const Color& a_right);

			bool mouseOverMainColor(const MouseState& a_state);
			bool mouseOverAlpha(const MouseState& a_state);
			bool mouseOverSideColor(const MouseState& a_state);
			bool mouseOverSwatch(const MouseState& a_state);

			BoxAABB<> currentSideColorBounds();
			BoxAABB<> currentAlphaBounds();
			BoxAABB<> currentMainColorBounds();
			BoxAABB<> currentSwatchBounds();

			void acceptMainColorClick();

			void updateColorForPaletteState();

			void acceptSideColorClick();
			void acceptAlphaClick();
			void acceptSwatchClick();

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("color", currentColor),
					cereal::make_nvp("selectorPercentSize", selectorPercentSize),
					cereal::make_nvp("selectorPercentPadding", selectorPercentPadding),
					cereal::make_nvp("eatTouches", eatTouches),
					cereal::make_nvp("globalClickPriority", globalClickPriority),
					cereal::make_nvp("overrideClickPriority", overrideClickPriority),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			virtual void initialize();

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Palette> &construct) {
				MouseState *mouse = nullptr;
				archive.extract(cereal::make_nvp("mouse", mouse));
				MV::require<PointerException>(mouse != nullptr, "Null mouse in Button::load_and_construct.");
				construct(std::shared_ptr<Node>(), *mouse);
				archive(
					cereal::make_nvp("color", construct->currentColor),
					cereal::make_nvp("selectorPercentSize", construct->selectorPercentSize),
					cereal::make_nvp("selectorPercentPadding", construct->selectorPercentPadding),
					cereal::make_nvp("eatTouches", construct->eatTouches),
					cereal::make_nvp("globalClickPriority", construct->globalClickPriority),
					cereal::make_nvp("overrideClickPriority", construct->overrideClickPriority),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Palette>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			Color percentToSliderColor(PointPrecision a_percentPosition);
		protected:
			virtual void onOwnerDestroyed() {
				onLeftMouseDownHandle.reset();
				onLeftMouseUpHandle.reset();
				onMouseMoveHandle.reset();
			}

		private:
			void ApplyCurrentColorToPreviewBox();

			MouseState::SignalType onLeftMouseDownHandle;
			MouseState::SignalType onLeftMouseUpHandle;
			MouseState::SignalType onMouseMoveHandle;

			size_t globalClickPriority = 100;
			std::vector<size_t> overrideClickPriority;
			bool eatTouches = true;

			MouseState& ourMouse;

			Size<> selectorPercentSize = { .85f, 0.85f };
			Size<> selectorPercentPadding = {0.03f, 0.03f};

			Point<> selectorCursorPercent = { 1.0f, 0.0f };
			Color topRightColor = {1.0f, 0.0f, 0.0f, 1.0f};
			Color currentColor = {1.0f, 0.0f, 0.0f, 1.0f};

			MV::MouseState::SignalType currentDragSignal;
			MV::Color::HSV hsv;
			static const std::vector<Color> colorBarList;
		};

	}
}

#endif
