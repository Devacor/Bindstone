#ifndef _MV_SCENE_PALETTE_H_
#define _MV_SCENE_PALETTE_H_

#include "drawable.h"
#include "MV/Interface/tapDevice.h"

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

			TapDevice& mouse() const;

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

			int64_t globalPriority() const;
			std::shared_ptr<Palette> globalPriority(int64_t a_newPriority);

			std::vector<int64_t> overridePriority() const;
			std::shared_ptr<Palette> overridePriority(const std::vector<int64_t> &a_newPriority) {
				overrideClickPriority = a_newPriority;
				return std::static_pointer_cast<Palette>(shared_from_this());
			}

			void startEatingTouches();
			void stopEatingTouches();
			bool eatingTouches() const;
		protected:
			Palette(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse);

			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override;
			virtual BoxAABB<> boundsImplementation() override { return Drawable::boundsImplementation(); }

			void innerColorAlphaSelectorNoNotification(const Color& a_left, const Color& a_right);

			bool mouseOverMainColor(const TapDevice& a_state);
			bool mouseOverAlpha(const TapDevice& a_state);
			bool mouseOverSideColor(const TapDevice& a_state);
			bool mouseOverSwatch(const TapDevice& a_state);

			BoxAABB<> currentSideColorBounds();
			BoxAABB<> currentAlphaBounds();
			BoxAABB<> currentMainColorBounds();
			BoxAABB<> currentSwatchBounds();

			std::shared_ptr<Palette> colorInternal(const MV::Color &a_newColor);

			void acceptMainColorClick();

			void updateColorForPaletteState();

			void acceptSideColorClick();
			void acceptAlphaClick();
			void acceptSwatchClick();

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
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

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
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

			virtual void initialize() override;

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Palette> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* mouse = services.get<MV::TapDevice>();
				construct(std::shared_ptr<Node>(), *mouse);
				construct->load(archive, version);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Palette>(mouse()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			Color percentToSliderColor(PointPrecision a_percentPosition);
		protected:
			virtual void onOwnerDestroyed() override {
				Drawable::onOwnerDestroyed();
				onLeftMouseDownHandle.reset();
				onLeftMouseUpHandle.reset();
				onMouseMoveHandle.reset();
			}

		private:
			void applyCurrentColorToPreviewBox();

			TapDevice::SignalType onLeftMouseDownHandle;
			TapDevice::SignalType onLeftMouseUpHandle;
			TapDevice::SignalType onMouseMoveHandle;

			size_t globalClickPriority = 100;
			std::vector<int64_t> overrideClickPriority;
			bool eatTouches = true;

			TapDevice& ourMouse;

			Size<> selectorPercentSize = { .85f, 0.85f };
			Size<> selectorPercentPadding = {0.03f, 0.03f};

			Point<> selectorCursorPercent = { 1.0f, 0.0f };
			Color topRightColor = {1.0f, 0.0f, 0.0f, 1.0f};
			Color currentColor = {1.0f, 0.0f, 0.0f, 1.0f};

			MV::TapDevice::SignalType currentDragSignal;
			MV::Color::HSV hsv;
			static const std::vector<Color> colorBarList;
		};

	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenepalette);

#endif
