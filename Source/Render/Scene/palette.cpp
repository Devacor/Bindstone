#include "palette.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

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
			onColorChange(onColorChangeSignal),
			onSwatchClicked(onSwatchClickedSignal),
			ourMouse(a_mouse),
			hsv(360.0f, 0.0f, 0.0f, 1.0f){

			shaderProgramId = MV::COLOR_PICKER_ID;

			//14 for the color selector, 4 for the hue + saturation,
			//4 for the alpha left, 4 for the alpha right, 4 for the preview.
			//4 for the diamond selected, 3 for the arrow
			points.resize(14 + 4 + 4 + 4 + 4 + 16 + 3);

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


			appendQuadVertexIndices(vertexIndices, 30);
			appendQuadVertexIndices(vertexIndices, 34);
			appendQuadVertexIndices(vertexIndices, 38);
			appendQuadVertexIndices(vertexIndices, 42);

			vertexIndices.push_back(46);
			vertexIndices.push_back(47);
			vertexIndices.push_back(48);

			innerColorAlphaSelectorNoNotification({181, 185, 191}, {65, 68, 77});
		}

		void Palette::initialize() {
			Drawable::initialize();
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
			onSwatchClickedSignal(self);
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
				MV::point(fullBounds.minPoint.x, fullBounds.minPoint.y + mainColorSize.height),
				MV::point(fullBounds.minPoint.x + mainColorSize.width, fullBounds.maxPoint.y)
			};
		}

		BoxAABB<> Palette::currentSideColorBounds() {
			auto fullBounds = bounds();
			auto mainColorSize = bounds().size() * (selectorPercentSize + selectorPercentPadding);
			return {
				MV::point(fullBounds.minPoint.x + mainColorSize.width, fullBounds.minPoint.y),
				MV::point(fullBounds.maxPoint.x, fullBounds.minPoint.y + mainColorSize.height)
			};
		}

		BoxAABB<> Palette::currentSwatchBounds() {
			auto fullBounds = bounds();
			auto mainColorSize = bounds().size() * (selectorPercentSize + selectorPercentPadding);
			return{
				MV::point(fullBounds.minPoint.x + mainColorSize.width, fullBounds.minPoint.y + mainColorSize.height),
				MV::point(fullBounds.maxPoint.x, fullBounds.maxPoint.y)
			};
		}

		void Palette::updateColorForPaletteState() {
			auto currentAlpha = currentColor.A;
			currentColor = mix(mix(topRightColor, Color(1.0f, 1.0f, 1.0f), 1.0f - selectorCursorPercent.x), Color(0.0f, 0.0f, 0.0f), selectorCursorPercent.y);
			currentColor.A = currentAlpha;
			colorInternal(currentColor);
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
				hsv.S = selectorCursorPercent.x;
				updateColorForPaletteState();
			};
			updateMainColor(ourMouse);
			currentDragSignal = ourMouse.onMove.connect(updateMainColor);
		}

		void Palette::acceptSideColorClick() {
			auto updateSideColor = [&](MouseState& a_mouse) {
				auto percentPosition = owner()->screenFromLocal(currentSideColorBounds()).percent(a_mouse.position());
				topRightColor = percentToSliderColor(percentPosition.y);
				hsv.invertedPercentHue(percentPosition.y);
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

		int64_t Palette::globalPriority() const {
			return globalClickPriority;
		}

		std::shared_ptr<Palette> Palette::globalPriority(int64_t a_newPriority) {
			globalClickPriority = a_newPriority;
			return std::static_pointer_cast<Palette>(shared_from_this());
		}

		std::vector<int64_t> Palette::overridePriority() const {
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
			hsv = currentColor.getHsv(hsv);
			topRightColor = percentToSliderColor(hsv.invertedPercentHue());
			selectorCursorPercent.x = hsv.S;
			selectorCursorPercent.y = 1.0f - hsv.V;
			ApplyCurrentColorToPreviewBox();

			auto middlePointX = currentColor.A * (points[25].x - points[19].x);

			points[20].x = middlePointX;
			points[21].x = middlePointX;

			points[22].x = middlePointX;
			points[23].x = middlePointX;

			auto self = std::static_pointer_cast<Palette>(shared_from_this());
			onColorChangeSignal(self);
			notifyParentOfComponentChange();
			return self;
		}

		std::shared_ptr<Palette> Palette::colorInternal(const MV::Color &a_newColor) {
			currentColor = a_newColor;
			hsv = currentColor.getHsv(hsv);
			ApplyCurrentColorToPreviewBox();
			
			auto middlePointX = currentColor.A * (points[25].x - points[19].x);

			points[20].x = middlePointX;
			points[21].x = middlePointX;

			points[22].x = middlePointX;
			points[23].x = middlePointX;

			auto self = std::static_pointer_cast<Palette>(shared_from_this());
			onColorChangeSignal(self);
			notifyParentOfComponentChange();
			return self;
		}

		void Palette::boundsImplementation(const BoxAABB<> &a_bounds) {
			auto self = std::static_pointer_cast<Palette>(shared_from_this());

			auto selectorPixelSize = Size<>(a_bounds.width(), a_bounds.height()) * selectorPercentSize;
			auto selectorPixelPadding = Size<>(a_bounds.width(), a_bounds.height()) * selectorPercentPadding;
			auto colorSelectorWidth = a_bounds.width() - (selectorPixelSize.width + selectorPixelPadding.width);
			auto alphaSelectorHeight = a_bounds.height() - (selectorPixelSize.height + selectorPixelPadding.height);

			PointPrecision segmentHeight = selectorPixelSize.height / static_cast<PointPrecision>(colorBarList.size() - 1);
			for (int i = 0; i < colorBarList.size(); ++i) {
				PointPrecision currentHeight = a_bounds.minPoint.y + segmentHeight * static_cast<PointPrecision>(i);

				points[i * 2] = MV::point(a_bounds.maxPoint.x - colorSelectorWidth, currentHeight);
				points[i * 2 + 1] = MV::point(a_bounds.maxPoint.x, currentHeight);
			}

			points[14] = a_bounds.topLeftPoint();
			points[15] = a_bounds.topLeftPoint() + MV::point(0.0f, selectorPixelSize.height);
			points[16] = a_bounds.topLeftPoint() + toPoint(selectorPixelSize);
			points[17] = a_bounds.topLeftPoint() + MV::point(selectorPixelSize.width, 0.0f);

			auto clippedOffRightX = colorSelectorWidth + selectorPixelPadding.width;
			auto middlePointX = currentColor.A * (a_bounds.width() - clippedOffRightX);
			points[18] = a_bounds.bottomLeftPoint() - MV::point(0.0f, alphaSelectorHeight);
			points[19] = a_bounds.bottomLeftPoint();
			points[20] = a_bounds.bottomLeftPoint() + MV::point(middlePointX, 0.0f);
			points[21] = a_bounds.bottomLeftPoint() + MV::point(middlePointX, -alphaSelectorHeight);

			points[22] = points[21].point();
			points[23] = points[20].point();
			points[24] = a_bounds.bottomRightPoint() - MV::point(clippedOffRightX, 0.0f);
			points[25] = a_bounds.bottomRightPoint() - MV::point(clippedOffRightX, alphaSelectorHeight);

			points[26] = a_bounds.bottomRightPoint() - MV::point(colorSelectorWidth, alphaSelectorHeight);
			points[27] = a_bounds.bottomRightPoint() - MV::point(colorSelectorWidth, 0.0f);
			points[28] = a_bounds.bottomRightPoint();
			points[29] = a_bounds.bottomRightPoint() - MV::point(0.0f, alphaSelectorHeight);

			for (size_t i = 30; i <= 48; ++i) {
				points[i].copyPosition(points[29]);
			}

			refreshBounds();

			ApplyCurrentColorToPreviewBox();
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
			for (int i = 26; i < 30; ++i) {
				points[i] = currentColor;
			}
			for (int i = 14; i < 18; ++i) {
				points[i] = topRightColor;
			}
			auto localBounds = bounds();
			auto selectorPixelSize = Size<>(localBounds.width(), localBounds.height()) * selectorPercentSize;
			auto selectorPixelPadding = Size<>(localBounds.width(), localBounds.height()) * selectorPercentPadding;
			auto colorSelectorWidth = localBounds.width() - (selectorPixelSize.width + selectorPixelPadding.width);
			auto alphaSelectorHeight = localBounds.height() - (selectorPixelSize.height + selectorPixelPadding.height);

			points[46] = MV::point(localBounds.maxPoint.x - colorSelectorWidth, localBounds.minPoint.y + ((1.0f - hsv.percentHue()) * selectorPixelSize.height));
			points[47] = MV::point(localBounds.maxPoint.x - colorSelectorWidth - selectorPixelPadding.width, (localBounds.minPoint.y + ((1.0f - hsv.percentHue()) * selectorPixelSize.height)) - selectorPixelPadding.width / 2.0f);
			points[48] = MV::point(localBounds.maxPoint.x - colorSelectorWidth - selectorPixelPadding.width, (localBounds.minPoint.y + ((1.0f - hsv.percentHue()) * selectorPixelSize.height)) + selectorPixelPadding.width / 2.0f);

			auto colorLocation = MV::point((hsv.S * selectorPixelSize.width) + localBounds.minPoint.x, ((1.0f - hsv.V) * selectorPixelSize.height) + localBounds.minPoint.y);
			points[30] = MV::point(colorLocation.x - 3, colorLocation.y);
			points[31] = MV::point(colorLocation.x, colorLocation.y - 3);
			points[32] = MV::point(colorLocation.x, colorLocation.y - 2);
			points[33] = MV::point(colorLocation.x - 2, colorLocation.y);

			points[34] = MV::point(colorLocation.x - 3, colorLocation.y);
			points[35] = MV::point(colorLocation.x, colorLocation.y + 3);
			points[36] = MV::point(colorLocation.x, colorLocation.y + 2);
			points[37] = MV::point(colorLocation.x - 2, colorLocation.y);

			points[38] = MV::point(colorLocation.x + 3, colorLocation.y);
			points[39] = MV::point(colorLocation.x, colorLocation.y - 3);
			points[40] = MV::point(colorLocation.x, colorLocation.y - 2);
			points[41] = MV::point(colorLocation.x + 2, colorLocation.y);

			points[42] = MV::point(colorLocation.x + 3, colorLocation.y);
			points[43] = MV::point(colorLocation.x, colorLocation.y + 3);
			points[44] = MV::point(colorLocation.x, colorLocation.y + 2);
			points[45] = MV::point(colorLocation.x + 2, colorLocation.y);

			float greyColor = mixOut(0.0f, 1.0f, ((1.0f - hsv.V) + (hsv.S)) / 2.0f, 4.5f);
			Color cursorColor(greyColor, greyColor, greyColor);
			for (int i = 30; i <= 45; ++i) {
				points[i].x = clamp(points[i].x, localBounds.minPoint.x, localBounds.maxPoint.x);
				points[i].y = clamp(points[i].y, localBounds.minPoint.y, localBounds.maxPoint.y);
				points[i] = cursorColor;
			}

			for (int i = 46; i <= 48; ++i) {
				points[i].x = clamp(points[i].x, localBounds.minPoint.x, localBounds.maxPoint.x);
				points[i].y = clamp(points[i].y, localBounds.minPoint.y, localBounds.maxPoint.y);
			}
		}
	}
}
