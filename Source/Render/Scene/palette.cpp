#include "palette.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Palette);

namespace MV {
	namespace Scene {

		const std::vector<Color> Palette::colorBarList = {
			{ 1.0f, 0.0f, 0.0f },
			{ 1.0f, 0.0f, 1.0f },
			{ 0.0f, 0.0f, 1.0f },
			{ 0.0f, 1.0f, 1.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 1.0f, 1.0f, 0.0f },
			{ 1.0f, 0.0f, 0.0f }
		};

		Palette::Palette(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse) :
			Drawable(a_owner),
			onColorChange(onColorChangeSlot),
			onSwatchClicked(onSwatchClickedSlot),
			ourMouse(a_mouse){

			shaderProgramId = MV::COLOR_PICKER_ID;

			//14 for the color selector, 4 for the hue + saturation,
			//4 for the alpha left, 4 for the alpha right, 4 for the preview.
			points.resize(14 + 4 + 4 + 4 + 4);

			for (int i = 0; i < colorBarList.size(); ++i) {
				points[i * 2] = colorBarList[i];
				points[i * 2 + 1] = colorBarList[i];
				if (i < colorBarList.size() - 1) {
					vertexIndices.push_back(static_cast<GLuint>((i * 2) + 0));
					vertexIndices.push_back(static_cast<GLuint>((i * 2) + 2));
					vertexIndices.push_back(static_cast<GLuint>((i * 2) + 3));
					vertexIndices.push_back(static_cast<GLuint>((i * 2) + 3));
					vertexIndices.push_back(static_cast<GLuint>((i * 2) + 1));
					vertexIndices.push_back(static_cast<GLuint>((i * 2) + 0));
				}
			}

			for (auto&& p : points) {
				p = TexturePoint(1.0f, 0.0f); //this is pure vertex color in the palette shader, 0, 0 is white, 0, 1 and 1, 1 are black.
			}

			points[14] = Color(1.0f, 0.0f, 0.0f);
			points[15] = Color(1.0f, 0.0f, 0.0f);
			points[16] = Color(1.0f, 0.0f, 0.0f);
			points[17] = Color(1.0f, 0.0f, 0.0f);

			points[14] = TexturePoint(0.0f, 0.0f);
			points[15] = TexturePoint(0.0f, 1.0f);
			points[16] = TexturePoint(1.0f, 1.0f);
			points[17] = TexturePoint(1.0f, 0.0f);

			appendQuadVertexIndices(vertexIndices, 14);
			appendQuadVertexIndices(vertexIndices, 18);
			appendQuadVertexIndices(vertexIndices, 22);
			appendQuadVertexIndices(vertexIndices, 26);
			innerColorAlphaSelectorNoNotification({181.0f / 255.0f, 185.0f / 255.0f, 191.0f / 255.0f}, {65.0f / 255.0f, 68.0f / 255.0f, 77.0f / 255.0f});
		}

		void Palette::initialize() {
			onLeftMouseDownHandle = ourMouse.onLeftMouseDown.connect([&](MouseState& a_mouse) {
				if (mouseOverMainColor(a_mouse)) {
					a_mouse.queueExclusiveAction({ eatTouches, (overrideClickPriority.empty() ? owner()->parentIndexList(globalClickPriority) : overrideClickPriority), [&]() {
						acceptMainColorClick();
					}, []() {}, owner()->id() });
				} else if (mouseOverSideColor(a_mouse)) {
					a_mouse.queueExclusiveAction({ eatTouches, (overrideClickPriority.empty() ? owner()->parentIndexList(globalClickPriority) : overrideClickPriority), [&]() {
						acceptSideColorClick();
					}, []() {}, owner()->id() });
				} else if (mouseOverAlpha(a_mouse)) {
					a_mouse.queueExclusiveAction({ eatTouches, (overrideClickPriority.empty() ? owner()->parentIndexList(globalClickPriority) : overrideClickPriority), [&]() {
						acceptAlphaClick();
					}, []() {}, owner()->id() });
				}
			});

			onLeftMouseUpHandle = ourMouse.onLeftMouseUp.connect([&](MouseState& a_mouse) {
				currentDragSignal.reset();
				if (mouseOverSwatch(a_mouse)) {
					a_mouse.queueExclusiveAction({ eatTouches, (overrideClickPriority.empty() ? owner()->parentIndexList(globalClickPriority) : overrideClickPriority), [&]() {
						acceptSwatchClick();
					}, []() {}, owner()->id() });
				}
			});
		}

		void Palette::acceptSwatchClick() {
			auto self = std::static_pointer_cast<Palette>(shared_from_this());
			onSwatchClickedSlot(self);
		}

		bool Palette::mouseOverMainColor(const MouseState& a_state) {
			if (owner()->active()) {
				return owner()->screenFromLocal(currentMainColorBounds()).contains(a_state.position());
			} else {
				return false;
			}
		}
		bool Palette::mouseOverAlpha(const MouseState& a_state) {
			if (owner()->active()) {
				return owner()->screenFromLocal(currentAlphaBounds()).contains(a_state.position());
			} else {
				return false;
			}
		}
		bool Palette::mouseOverSideColor(const MouseState& a_state) {
			if (owner()->active()) {
				return owner()->screenFromLocal(currentSideColorBounds()).contains(a_state.position());
			} else {
				return false;
			}
		}
		bool Palette::mouseOverSwatch(const MouseState& a_state) {
			if (owner()->active()) {
				return owner()->screenFromLocal(currentSwatchBounds()).contains(a_state.position());
			} else {
				return false;
			}
		}

		BoxAABB<> Palette::currentMainColorBounds() {
			auto fullBounds = bounds();
			auto mainColorSize = bounds().size() * selectorPercentSize;
			return { fullBounds.minPoint, fullBounds.minPoint + toPoint(mainColorSize) };
		}

		BoxAABB<> Palette::currentAlphaBounds() {
			auto fullBounds = bounds();
			auto mainColorSize = bounds().size() * (selectorPercentSize + selectorPercentPadding);
			return {
				point(fullBounds.minPoint.x, fullBounds.minPoint.y + mainColorSize.height),
				point(fullBounds.minPoint.x + mainColorSize.width, fullBounds.maxPoint.y)
			};
		}

		BoxAABB<> Palette::currentSideColorBounds() {
			auto fullBounds = bounds();
			auto mainColorSize = bounds().size() * (selectorPercentSize + selectorPercentPadding);
			return {
				point(fullBounds.minPoint.x + mainColorSize.width, fullBounds.minPoint.y),
				point(fullBounds.maxPoint.x, fullBounds.minPoint.y + mainColorSize.height)
			};
		}

		BoxAABB<> Palette::currentSwatchBounds() {
			auto fullBounds = bounds();
			auto mainColorSize = bounds().size() * (selectorPercentSize + selectorPercentPadding);
			return{
				point(fullBounds.minPoint.x + mainColorSize.width, fullBounds.minPoint.y + mainColorSize.height),
				point(fullBounds.maxPoint.x, fullBounds.maxPoint.y)
			};
		}

		void Palette::updateColorForPaletteState() {
			auto currentAlpha = currentColor.A;
			currentColor = mix(mix(topRightColor, Color(1, 1, 1), 1.0f - selectorCursorPercent.x), Color(0, 0, 0), selectorCursorPercent.y);
			currentColor.A = currentAlpha;
			color(currentColor);
		}

		Color Palette::percentToSliderColor(PointPrecision a_percentPosition) {
			PointPrecision floatIndex = a_percentPosition * (colorBarList.size() - 1);
			size_t index = static_cast<size_t>(floatIndex);
			PointPrecision indexPercent = floatIndex - static_cast<PointPrecision>(index);
			if (index == colorBarList.size() - 1) {
				return colorBarList.back();
			} else {
				return mix(colorBarList[index], colorBarList[index + 1], indexPercent);
			}
		}

		void Palette::acceptMainColorClick() {
			auto updateMainColor = [&](MouseState& a_mouse){
				selectorCursorPercent = owner()->screenFromLocal(currentMainColorBounds()).percent(a_mouse.position());
				std::cout << "PERCENT: " << selectorCursorPercent << std::endl;
				updateColorForPaletteState();
			};
			updateMainColor(ourMouse);
			currentDragSignal = ourMouse.onMove.connect(updateMainColor);
		}

		void Palette::acceptSideColorClick() {
			auto updateSideColor = [&](MouseState& a_mouse) {
				auto percentPosition = owner()->screenFromLocal(currentSideColorBounds()).percent(a_mouse.position());
				topRightColor = percentToSliderColor(percentPosition.y);
				for (int i = 14; i < 18; ++i) {
					points[i] = topRightColor;
				}
				updateColorForPaletteState();
			};
			updateSideColor(ourMouse);
			currentDragSignal = ourMouse.onMove.connect(updateSideColor);
		}

		void Palette::acceptAlphaClick() {
			auto updateAlpha = [&](MouseState& a_mouse) {
				auto percentPosition = owner()->screenFromLocal(currentAlphaBounds()).percent(a_mouse.position());
				currentColor.A = percentPosition.x;
				updateColorForPaletteState();
			};
			updateAlpha(ourMouse);
			currentDragSignal = ourMouse.onMove.connect(updateAlpha);
		}

		void Palette::innerColorAlphaSelectorNoNotification(const Color& a_left, const Color& a_right) {
			for (int i = 18; i < 22; ++i) {
				points[i] = a_left;
				points[i].A = 1.0f;
			}
			for (int i = 22; i < 26; ++i) {
				points[i] = a_right;
				points[i].A = 1.0f;
			}
		}

		std::shared_ptr<Palette> Palette::colorAlphaSelector(const Color& a_left, const Color& a_right) {
			auto self = std::static_pointer_cast<Palette>(shared_from_this());

			innerColorAlphaSelectorNoNotification(a_left, a_right);

			notifyParentOfComponentChange();
			return self;
		}

		MouseState& Palette::mouse() const {
			return ourMouse;
		}

		size_t Palette::globalPriority() const {
			return globalClickPriority;
		}

		std::shared_ptr<Palette> Palette::globalPriority(size_t a_newPriority) {
			globalClickPriority = a_newPriority;
			return std::static_pointer_cast<Palette>(shared_from_this());
		}

		std::vector<size_t> Palette::overridePriority() const {
			return overrideClickPriority;
		}

		bool Palette::eatingTouches() const {
			return eatTouches;
		}

		void Palette::stopEatingTouches() {
			eatTouches = false;
		}

		void Palette::startEatingTouches() {
			eatTouches = true;
		}

		std::shared_ptr<Palette> Palette::color(const MV::Color &a_newColor) {
			currentColor = a_newColor;
			ApplyCurrentColorToPreviewBox();
			
			auto middlePointX = currentColor.A * (points[25].x - points[19].x);

			points[20].x = middlePointX;
			points[21].x = middlePointX;

			points[22].x = middlePointX;
			points[23].x = middlePointX;

			auto self = std::static_pointer_cast<Palette>(shared_from_this());
			onColorChangeSlot(self);
			notifyParentOfComponentChange();
			return self;
		}

		std::shared_ptr<Palette> Palette::bounds(const BoxAABB<> &a_bounds) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = std::static_pointer_cast<Palette>(shared_from_this());

			auto selectorPixelSize = Size<>(a_bounds.width(), a_bounds.height()) * selectorPercentSize;
			auto selectorPixelPadding = Size<>(a_bounds.width(), a_bounds.height()) * selectorPercentPadding;
			auto colorSelectorWidth = a_bounds.width() - (selectorPixelSize.width + selectorPixelPadding.width);
			auto alphaSelectorHeight = a_bounds.height() - (selectorPixelSize.height + selectorPixelPadding.height);

			PointPrecision segmentHeight = selectorPixelSize.height / static_cast<PointPrecision>(colorBarList.size() - 1);
			for (int i = 0; i < colorBarList.size(); ++i) {
				PointPrecision currentHeight = a_bounds.minPoint.y + segmentHeight * static_cast<PointPrecision>(i);

				points[i * 2] = point(a_bounds.maxPoint.x - colorSelectorWidth, currentHeight);
				points[i * 2 + 1] = point(a_bounds.maxPoint.x, currentHeight);
			}

			points[14] = a_bounds.topLeftPoint();
			points[15] = a_bounds.topLeftPoint() + point(0.0f, selectorPixelSize.height);
			points[16] = a_bounds.topLeftPoint() + toPoint(selectorPixelSize);
			points[17] = a_bounds.topLeftPoint() + point(selectorPixelSize.width, 0.0f);

			auto clippedOffRightX = colorSelectorWidth + selectorPixelPadding.width;
			auto middlePointX = currentColor.A * (a_bounds.width() - clippedOffRightX);
			points[18] = a_bounds.bottomLeftPoint() - point(0.0f, alphaSelectorHeight);
			points[19] = a_bounds.bottomLeftPoint();
			points[20] = a_bounds.bottomLeftPoint() + point(middlePointX, 0.0f);
			points[21] = a_bounds.bottomLeftPoint() + point(middlePointX, -alphaSelectorHeight);

			points[22] = points[21].point();
			points[23] = points[20].point();
			points[24] = a_bounds.bottomRightPoint() - point(clippedOffRightX, 0.0f);
			points[25] = a_bounds.bottomRightPoint() - point(clippedOffRightX, alphaSelectorHeight);

			points[26] = a_bounds.bottomRightPoint() - point(colorSelectorWidth, alphaSelectorHeight);
			points[27] = a_bounds.bottomRightPoint() - point(colorSelectorWidth, 0.0f);
			points[28] = a_bounds.bottomRightPoint();
			points[29] = a_bounds.bottomRightPoint() - point(0.0f, alphaSelectorHeight);

			ApplyCurrentColorToPreviewBox();

			refreshBounds();
			return self;
		}

		std::shared_ptr<Component> Palette::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto paletteClone = std::static_pointer_cast<Palette>(a_clone);
			paletteClone->selectorPercentSize = selectorPercentSize;
			paletteClone->selectorPercentPadding = selectorPercentPadding;
			paletteClone->currentColor = currentColor;
			return a_clone;
		}

		void Palette::ApplyCurrentColorToPreviewBox() {
			points[26] = currentColor;
			points[27] = currentColor;
			points[28] = currentColor;
			points[29] = currentColor;
		}
	}
}