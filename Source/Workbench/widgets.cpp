#include "widgets.h"
#include "tooltips.h"
#include "MV/Utility/generalUtility.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Workbench {

	std::string formatNumberCompact(float a_value, int a_maximumDecimals) {
		std::ostringstream out;
		out.setf(std::ios::fixed);
		out.precision(a_maximumDecimals);
		out << a_value;
		std::string result = out.str();
		if (result.find('.') != std::string::npos) {
			result.erase(result.find_last_not_of('0') + 1);
			if (result.back() == '.') {
				result.pop_back();
			}
		}
		return result == "-0" ? std::string("0") : result;
	}

	namespace {
		// Drops decimals until the number fits its field.
		std::string formatNumberToFit(float a_value, float a_fieldWidth, const Theme& a_theme, MV::FontDefinition& a_font) {
			const float room = a_fieldWidth - a_theme.px(10.0f);
			for (int decimals = 4; decimals >= 0; --decimals) {
				std::string result = formatNumberCompact(a_value, decimals);
				if (a_font.measure(result).width <= room) {
					return result;
				}
			}
			std::ostringstream out;
			out.precision(3);
			out << a_value;
			return out.str();
		}

		MV::Color axisTint(const std::string& a_axis) {
			if (a_axis == "X" || a_axis == "R") { return MV::Color(0x9c4a4a); }
			if (a_axis == "Y" || a_axis == "G") { return MV::Color(0x4a9c5a); }
			if (a_axis == "Z" || a_axis == "B") { return MV::Color(0x4a6a9c); }
			return MV::Color(0x6a6a72);
		}

		// White rounded-box coverage mask (Inigo Quilez SDF, 1px AA edge). STRAIGHT alpha: the
		// default premultiply shader multiplies rgb by alpha at the end. Power-of-two content so
		// convertToPowerOfTwoSurface never pads (padding would shrink/blur the slice corners).
		SDL_Surface* makeStraightAlphaSurface(int a_width, int a_height) {
			int bitsPerPixel = 0;
			Uint32 redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
			SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_ABGR8888, &bitsPerPixel, &redMask, &greenMask, &blueMask, &alphaMask);
			SDL_Surface* surface = SDL_CreateRGBSurface(0, a_width, a_height, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
			SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
			return surface;
		}

		// how far a shaded surface darkens from its top row to its bottom one
		constexpr float shadeDepth = 0.16f;

		uint32_t shadedWhite(int a_row, int a_rows, uint8_t a_alpha) {
			const auto level = static_cast<uint32_t>(255.0f * (1.0f - shadeDepth * static_cast<float>(a_row) / static_cast<float>(std::max(1, a_rows - 1))) + 0.5f);
			return (uint32_t(a_alpha) << 24) | (level << 16) | (level << 8) | level;
		}

		std::shared_ptr<MV::OwnedSurface> makeShadeSurface() {
			const int width = 4;
			const int height = 32;
			SDL_Surface* surface = makeStraightAlphaSurface(width, height);
			auto* pixels = static_cast<uint32_t*>(surface->pixels);
			const int pitch = surface->pitch / 4;
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					pixels[y * pitch + x] = shadedWhite(y, height, 255);
				}
			}
			return MV::OwnedSurface::make(surface);
		}

		// Solid triangle with 4x4 supersampled edges, in a power-of-two square.
		std::shared_ptr<MV::OwnedSurface> makeTriangleSurface(int a_size, bool a_down) {
			SDL_Surface* surface = makeStraightAlphaSurface(a_size, a_size);
			auto* pixels = static_cast<uint32_t*>(surface->pixels);
			const int pitch = surface->pitch / 4;
			const float size = static_cast<float>(a_size);
			const float ax = (a_down ? 0.18f : 0.30f) * size, ay = (a_down ? 0.32f : 0.18f) * size;
			const float bx = (a_down ? 0.82f : 0.30f) * size, by = (a_down ? 0.32f : 0.82f) * size;
			const float cx = (a_down ? 0.50f : 0.76f) * size, cy = (a_down ? 0.74f : 0.50f) * size;
			auto side = [](float a_px, float a_py, float a_x0, float a_y0, float a_x1, float a_y1) {
				return (a_px - a_x1) * (a_y0 - a_y1) - (a_x0 - a_x1) * (a_py - a_y1);
			};
			for (int y = 0; y < a_size; ++y) {
				for (int x = 0; x < a_size; ++x) {
					int covered = 0;
					for (int sample = 0; sample < 16; ++sample) {
						const float px = x + (sample % 4 + 0.5f) / 4.0f;
						const float py = y + (sample / 4 + 0.5f) / 4.0f;
						const float d1 = side(px, py, ax, ay, bx, by);
						const float d2 = side(px, py, bx, by, cx, cy);
						const float d3 = side(px, py, cx, cy, ax, ay);
						const bool negative = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
						const bool positive = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
						covered += !(negative && positive);
					}
					pixels[y * pitch + x] = (uint32_t(covered * 255 / 16) << 24) | 0x00FFFFFFu;
				}
			}
			return MV::OwnedSurface::make(surface);
		}

		std::shared_ptr<MV::OwnedSurface> makeRoundedRectSurface(int a_radius, bool a_shaded = false) {
			const int contentSize = MV::roundUpPowerOfTwo(2 * a_radius + 2);
			SDL_Surface* surface = makeStraightAlphaSurface(contentSize, contentSize);

			const float radius = static_cast<float>(a_radius);
			const float half = contentSize * 0.5f;
			auto* pixels = static_cast<uint32_t*>(surface->pixels);
			const int pitch = surface->pitch / 4;
			for (int y = 0; y < contentSize; ++y) {
				for (int x = 0; x < contentSize; ++x) {
					const float qx = std::abs((x + 0.5f) - half) - half + radius;
					const float qy = std::abs((y + 0.5f) - half) - half + radius;
					const float mx = std::max(qx, 0.0f);
					const float my = std::max(qy, 0.0f);
					const float distance = std::sqrt(mx * mx + my * my) + std::min(std::max(qx, qy), 0.0f) - radius;
					const float coverage = std::clamp(0.5f - distance, 0.0f, 1.0f);
					const auto alpha = static_cast<uint8_t>(coverage * 255.0f + 0.5f);
					pixels[y * pitch + x] = a_shaded ? shadedWhite(y, contentSize, alpha) : (uint32_t(alpha) << 24) | 0x00FFFFFFu;
				}
			}
			return MV::OwnedSurface::make(surface);
		}

		std::shared_ptr<MV::TextureHandle> roundedRectHandleOfRadius(MV::Services& a_services, int a_radius, bool a_shaded) {
			auto definition = a_services.get<MV::SharedTextures>()->surface(std::string(a_shaded ? "workbenchRoundedShaded_r" : "workbenchRounded_r") + std::to_string(a_radius), [a_radius, a_shaded] {
				return makeRoundedRectSurface(a_radius, a_shaded);
			});
			auto handle = definition->makeHandle();
			const int contentSize = handle->texture()->size().width;
			handle->slice(MV::BoxAABB<int>(MV::point(a_radius, a_radius), MV::point(contentSize - a_radius, contentSize - a_radius)));
			return handle;
		}

		int themeRadius(const Theme& a_theme) {
			return std::max(2, static_cast<int>(a_theme.cornerRadius + 0.5f));
		}
	}

	std::shared_ptr<MV::TextureHandle> roundedRectHandle(MV::Services& a_services, const Theme& a_theme) {
		return roundedRectHandleOfRadius(a_services, themeRadius(a_theme), false);
	}

	MV::BoxAABB<> outlinedFillBounds(const Theme& a_theme, const MV::Size<>& a_size) {
		const float line = a_theme.px(1.0f);
		return MV::BoxAABB<>(MV::point(line, line), MV::size(a_size.width - line * 2.0f, a_size.height - line * 2.0f));
	}

	std::shared_ptr<MV::Scene::Sprite> attachOutlinedRect(const std::shared_ptr<MV::Scene::Node>& a_owner, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_size, const MV::Color& a_fill, const MV::Color& a_outline, bool a_shaded) {
		attachRoundedRect(a_owner, a_services, a_theme, a_size, a_outline);
		const int innerRadius = std::max(2, themeRadius(a_theme) - static_cast<int>(a_theme.px(1.0f)));
		auto fill = a_owner->attach<MV::Scene::Sprite>()->bounds(outlinedFillBounds(a_theme, a_size))->texture(roundedRectHandleOfRadius(a_services, innerRadius, a_shaded))->color(a_fill);
		fill->id("fill");
		return fill;
	}

	std::shared_ptr<MV::TextureHandle> shadeHandle(MV::Services& a_services) {
		return a_services.get<MV::SharedTextures>()->surface("workbenchShade", [] {
			return makeShadeSurface();
		})->makeHandle();
	}

	std::shared_ptr<MV::Scene::Sprite> makeExpanderArrow(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_box, bool a_expanded) {
		const float arrowSize = a_theme.px(12.0f);
		const int surfaceSize = MV::roundUpPowerOfTwo(std::max(8, static_cast<int>(arrowSize)));
		auto handle = a_services.get<MV::SharedTextures>()->surface(std::string(a_expanded ? "workbenchArrowDown_" : "workbenchArrowRight_") + std::to_string(surfaceSize), [surfaceSize, a_expanded] {
			return makeTriangleSurface(surfaceSize, a_expanded);
		})->makeHandle();
		auto arrow = a_parent->make("arrow");
		arrow->position(MV::point(std::round((a_box.width - arrowSize) * 0.5f), std::round((a_box.height - arrowSize) * 0.5f)));
		return arrow->attach<MV::Scene::Sprite>()->bounds(MV::size(arrowSize, arrowSize))->texture(handle)->color(a_theme.glyph());
	}

	std::shared_ptr<MV::Scene::Sprite> attachRoundedRect(const std::shared_ptr<MV::Scene::Node>& a_owner, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_size, const MV::Color& a_color) {
		return a_owner->attach<MV::Scene::Sprite>()->bounds(a_size)->texture(roundedRectHandle(a_services, a_theme))->color(a_color);
	}

	float labelTextWidth(MV::Services& a_services, const Theme& a_theme, const std::string& a_text) {
		auto font = a_services.get<MV::TextLibrary>()->get(a_theme.labelFont);
		return font ? font->measure(a_text).width : 0.0f;
	}

	std::string fitLabelText(MV::Services& a_services, const Theme& a_theme, const std::string& a_text, float a_width) {
		auto font = a_services.get<MV::TextLibrary>()->get(a_theme.labelFont);
		return !font || a_text.find("[[") != std::string::npos ? a_text : font->fit(a_text, a_width);
	}

	std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto text = a_parent->make(a_id)->attach<MV::Scene::Text>(library, a_theme.labelFont);
		text->bounds({ MV::Point<>(), a_size });
		text->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(fitLabelText(a_services, a_theme, a_text, a_size.width));
		return text.self();
	}

	void setLabelText(const std::shared_ptr<MV::Scene::Text>& a_label, MV::Services& a_services, const Theme& a_theme, const std::string& a_text) {
		a_label->text(fitLabelText(a_services, a_theme, a_text, a_label->bounds().width()));
	}

	std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text, std::function<void()> a_onClick) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto button = a_parent->make(a_id)->attach<MV::Scene::Button>(mouse)->bounds(a_size);

		auto activeScene = attachOutlinedRect(button->owner()->make("active"), a_services, a_theme, a_size, a_theme.buttonActive(), a_theme.buttonOutline(), true)->owner();
		auto idleFill = attachOutlinedRect(button->owner()->make("idle"), a_services, a_theme, a_size, a_theme.buttonIdle(), a_theme.buttonOutline(), true);
		auto idleScene = idleFill->owner();
		hoverTint(button, idleFill, a_theme.buttonIdle(), a_theme.buttonHover());
		for (auto&& stateScene : { activeScene, idleScene }) {
			auto stateText = stateScene->attach<MV::Scene::Text>(library, a_theme.buttonFont);
			stateText->bounds({ MV::Point<>(), a_size });
			stateText->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(fitLabelText(a_services, a_theme, a_text, a_size.width - a_theme.px(8.0f)));
		}
		button->activeNode(activeScene);
		button->idleNode(idleScene);
		focusTint(button, idleScene->component<MV::Scene::Sprite>(false, false).self(), a_theme.buttonOutline(), a_theme.accent());
		if (a_onClick) {
			button->onAccept.connect("click", [a_onClick](const std::shared_ptr<MV::Scene::Clickable>&) {
				a_onClick();
			});
		}
		return button;
	}

	std::shared_ptr<MV::Scene::Button> makeIconButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, Icon a_icon, std::function<void()> a_onClick, const std::string& a_tooltip, std::optional<MV::Color> a_idle, std::optional<MV::Color> a_hover) {
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto button = a_parent->make(a_id)->attach<MV::Scene::Button>(mouse)->bounds(a_size);
		const MV::Color idle = a_idle.value_or(a_theme.buttonIdle());
		const MV::Color hover = a_hover.value_or(a_theme.buttonHover());

		auto activeScene = attachOutlinedRect(button->owner()->make("active"), a_services, a_theme, a_size, a_theme.buttonActive(), a_theme.buttonOutline(), true)->owner();
		auto idleFill = attachOutlinedRect(button->owner()->make("idle"), a_services, a_theme, a_size, idle, a_theme.buttonOutline(), true);
		auto idleScene = idleFill->owner();
		const float glyphSize = std::min(a_theme.px(16.0f), std::min(a_size.width, a_size.height) - a_theme.px(4.0f));
		for (auto&& stateScene : { activeScene, idleScene }) {
			auto glyph = stateScene->make("glyph");
			glyph->position(MV::point(std::round((a_size.width - glyphSize) * 0.5f), std::round((a_size.height - glyphSize) * 0.5f)));
			glyph->attach<MV::Scene::Sprite>()->bounds(MV::size(glyphSize, glyphSize))->texture(iconHandle(a_services, a_theme, a_icon))->color(MV::Color(0.88f, 0.88f, 0.88f, 1.0f));
		}
		button->activeNode(activeScene);
		button->idleNode(idleScene);
		focusTint(button, idleScene->component<MV::Scene::Sprite>(false, false).self(), a_theme.buttonOutline(), a_theme.accent());
		hoverTint(button, idleFill, idle, hover);
		if (a_onClick) {
			button->onAccept.connect("click", [a_onClick](const std::shared_ptr<MV::Scene::Clickable>&) {
				a_onClick();
			});
		}
		if (!a_tooltip.empty()) {
			tooltip(a_services, button, a_tooltip);
		}
		return button;
	}

	void setButtonIcon(const std::shared_ptr<MV::Scene::Button>& a_button, MV::Services& a_services, const Theme& a_theme, Icon a_icon) {
		for (auto&& state : { "active", "idle" }) {
			auto stateScene = a_button->owner()->get(state, false);
			auto glyph = stateScene ? stateScene->get("glyph", false) : nullptr;
			if (glyph) {
				glyph->component<MV::Scene::Sprite>(true, false)->texture(iconHandle(a_services, a_theme, a_icon));
			}
		}
	}

	void hoverTint(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::shared_ptr<MV::Scene::Sprite>& a_sprite, const MV::Color& a_idle, const MV::Color& a_hover) {
		std::weak_ptr<MV::Scene::Sprite> weakSprite = a_sprite;
		auto tintTo = [weakSprite](const MV::Color& a_color) {
			return [weakSprite, a_color](const std::shared_ptr<MV::Scene::Clickable>&) {
				if (auto sprite = weakSprite.lock()) {
					sprite->color(a_color);
				}
			};
		};
		a_clickable->onHoverEnter.connect("hoverTint", tintTo(a_hover));
		a_clickable->onHoverLeave.connect("hoverTint", tintTo(a_idle));
	}

	void focusTint(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::shared_ptr<MV::Scene::Sprite>& a_sprite, const MV::Color& a_idle, const MV::Color& a_focused) {
		std::weak_ptr<MV::Scene::Sprite> weakSprite = a_sprite;
		auto tintTo = [weakSprite](const MV::Color& a_color) {
			return [weakSprite, a_color](const std::shared_ptr<MV::Scene::Clickable>&) {
				if (auto sprite = weakSprite.lock()) {
					sprite->color(a_color);
				}
			};
		};
		a_clickable->onFocusEnter.connect("focusTint", tintTo(a_focused));
		a_clickable->onFocusLeave.connect("focusTint", tintTo(a_idle));
	}

	void hoverShow(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::shared_ptr<MV::Scene::Sprite>& a_sprite) {
		std::weak_ptr<MV::Scene::Sprite> weakSprite = a_sprite;
		a_sprite->hide();
		a_clickable->onHoverEnter.connect("hoverShow", [weakSprite](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (auto sprite = weakSprite.lock()) {
				sprite->show();
			}
		});
		a_clickable->onHoverLeave.connect("hoverShow", [weakSprite](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (auto sprite = weakSprite.lock()) {
				sprite->hide();
			}
		});
	}

	void tooltip(MV::Services& a_services, const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::string& a_text) {
		if (auto* tips = a_services.get<Tooltips>(false)) {
			tips->attach(a_clickable, a_text);
		}
	}

	std::shared_ptr<MV::Scene::Text> makeTextField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_initial, std::function<void(const std::string&)> a_onCommit, bool a_enterOnly) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto box = attachOutlinedRect(a_parent->make(a_id), a_services, a_theme, a_size, a_theme.fieldBg(), a_theme.fieldOutline())->owner();
		// what is typed past the field's edge is cut off there
		auto textNode = box->make("text");
		textNode->attach<MV::Scene::Stencil>()->bounds(outlinedFillBounds(a_theme, a_size));
		auto text = textNode->attach<MV::Scene::Text>(library, a_theme.labelFont);
		text->bounds({ MV::point(a_theme.px(5.0f), 0.0f), MV::size(a_size.width - a_theme.px(8.0f), a_size.height) });
		text->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, a_size.width - a_theme.px(8.0f))->minimumLineHeight(a_size.height)->text(a_initial);

		auto clickable = box->attach<MV::Scene::Clickable>(mouse)->bounds(a_size);
		hoverTint(clickable, box->component<MV::Scene::Sprite>(false, false).self(), a_theme.fieldOutline(), a_theme.fieldOutlineHover());
		// Tab reaches a field, and reaching it starts the typing
		clickable->focusable(true)->pressOnFocus(true);
		std::weak_ptr<MV::Scene::Text> weakText = text.self();
		FocusRouter* focus = &a_focus;
		auto commit = [weakText, a_onCommit]() {
			if (auto locked = weakText.lock()) {
				if (a_onCommit) {
					a_onCommit(locked->text());
				}
			}
		};
		clickable->onAccept.connect("focus", [weakText, focus, commit, a_enterOnly](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (auto locked = weakText.lock()) {
				std::function<void()> abandon;
				if (a_enterOnly) {
					abandon = [weakText, shown = std::string(locked->text())]() {
						if (auto text = weakText.lock()) {
							text->text(shown);
						}
					};
				}
				focus->activate(locked, commit, abandon);
			}
		});
		text->onEnter.connect("commit", [focus](std::shared_ptr<MV::Scene::Text>) {
			focus->deactivate();
		});
		return text.self();
	}

	std::shared_ptr<MV::Scene::Node> makeToggle(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, bool a_initial, std::function<void(bool)> a_onChange) {
		auto& mouse = *a_services.get<MV::TapDevice>();
		MV::Size<> boxSize(a_theme.fieldHeight, a_theme.fieldHeight);
		auto node = a_parent->make(a_id);
		auto mark = attachRoundedRect(node->make("mark"), a_services, a_theme, boxSize / 2.0f, a_theme.accent())->owner();
		mark->position(MV::toPoint(boxSize / 4.0f));
		if (!a_initial) {
			mark->hide();
		}
		auto toggleClick = node->attach<MV::Scene::Clickable>(mouse);
		toggleClick->bounds(boxSize);
		toggleClick->texture(roundedRectHandle(a_services, a_theme));
		toggleClick->color(a_theme.fieldBg());
		toggleClick->show();
		hoverTint(toggleClick.self(), toggleClick.self(), a_theme.fieldBg(), a_theme.fieldOutline());
		toggleClick->onAccept.connect("toggle", [mark, a_onChange](const std::shared_ptr<MV::Scene::Clickable>&) {
			bool nowOn = !mark->visible();
			mark->visible(nowOn);
			if (a_onChange) {
				a_onChange(nowOn);
			}
		});
		return node;
	}

	std::shared_ptr<MV::Scene::Text> makeScrubField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, float a_value, std::function<void(float)> a_onChange, std::function<void(float, float)> a_onHistory) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto box = attachOutlinedRect(a_parent->make(a_id), a_services, a_theme, a_size, a_theme.fieldBg(), a_theme.fieldOutline())->owner();
		auto text = box->attach<MV::Scene::Text>(library, a_theme.labelFont);
		text->bounds({ MV::point(a_theme.px(5.0f), 0.0f), MV::size(a_size.width - a_theme.px(8.0f), a_size.height) });
		text->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, a_size.width - a_theme.px(8.0f))->minimumLineHeight(a_size.height)->text(formatNumberToFit(a_value, a_size.width, a_theme, *library.get(a_theme.labelFont)));

		auto clickable = box->attach<MV::Scene::Clickable>(mouse)->bounds(a_size);
		hoverTint(clickable, box->component<MV::Scene::Sprite>(false, false).self(), a_theme.fieldOutline(), a_theme.fieldOutlineHover());
		// Tab reaches a field, and reaching it starts the typing
		clickable->focusable(true)->pressOnFocus(true);
		std::weak_ptr<MV::Scene::Text> weakText = text.self();
		FocusRouter* focus = &a_focus;

		struct ScrubState {
			bool started = false;
			bool suppressAccept = false;
			float committed = 0.0f;
			float live = 0.0f;
			std::string shown;   // the field may show fewer digits than the value has: only edited text is parsed
		};
		auto state = std::make_shared<ScrubState>();
		state->committed = a_value;
		state->live = a_value;
		state->shown = text->text();

		auto commitFromText = [weakText, state, a_onChange, a_onHistory]() {
			if (auto locked = weakText.lock()) {
				if (locked->text() == state->shown) {
					return;
				}
				try {
					float value = std::stof(locked->text());
					if (a_onChange) {
						a_onChange(value);
					}
					if (a_onHistory && value != state->committed) {
						a_onHistory(state->committed, value);
					}
					state->committed = value;
					state->live = value;
					state->shown = locked->text();
				} catch (...) {
					locked->text(state->shown);
				}
			}
		};

		clickable->onDrag.connect("scrub", [weakText, state, a_onChange, fieldWidth = a_size.width, &a_theme, font = library.get(a_theme.labelFont)](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<int>& a_start, const MV::Point<int>&) {
			auto locked = weakText.lock();
			if (!locked) {
				return;
			}
			int totalDelta = a_clickable->mouse().position().x - a_start.x;
			if (!state->started && std::abs(totalDelta) < 3) {
				return;
			}
			state->started = true;
			state->suppressAccept = true;
			float step = std::max(0.05f, std::abs(state->committed) * 0.01f);
			float value = state->committed + static_cast<float>(totalDelta) * step;
			state->live = value;
			state->shown = formatNumberToFit(value, fieldWidth, a_theme, *font);
			locked->text(state->shown);
			if (a_onChange) {
				a_onChange(value);
			}
		});
		clickable->onRelease.connect("scrubEnd", [weakText, state, a_onHistory](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<MV::PointPrecision>&) {
			if (!state->started) {
				return;
			}
			state->started = false;
			if (a_onHistory && state->live != state->committed) {
				a_onHistory(state->committed, state->live);
			}
			state->committed = state->live;
		});
		clickable->onAccept.connect("focus", [weakText, focus, state, commitFromText](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (state->suppressAccept) {
				state->suppressAccept = false;
				return;
			}
			if (auto locked = weakText.lock()) {
				focus->activate(locked, commitFromText);
			}
		});
		text->onEnter.connect("commit", [focus](std::shared_ptr<MV::Scene::Text>) {
			focus->deactivate();
		});
		return text.self();
	}

	void makeVectorRow(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, float a_totalWidth, const std::vector<std::pair<std::string, float>>& a_axes, std::function<void(size_t, float)> a_onChange, std::function<void(size_t, float, float)> a_onHistory) {
		auto& library = *a_services.get<MV::TextLibrary>();
		float axisLabelWidth = a_theme.fieldHeight * 0.75f;
		size_t axisCount = std::max<size_t>(1, a_axes.size());
		float fieldWidth = std::max(24.0f, (a_totalWidth - axisCount * (axisLabelWidth + a_theme.pad)) / axisCount);
		float x = 0.0f;
		for (size_t i = 0; i < a_axes.size(); ++i) {
			auto axisHost = a_parent->make(MV::guid("axis_"));
			axisHost->position(MV::point(x, 0.0f));
			axisHost->attach<MV::Scene::Sprite>()->bounds(MV::size(axisLabelWidth, a_theme.fieldHeight))->color(axisTint(a_axes[i].first));
			auto axisLabel = axisHost->attach<MV::Scene::Text>(library, a_theme.labelFont);
			axisLabel->bounds({ MV::Point<>(), MV::size(axisLabelWidth, a_theme.fieldHeight) });
			axisLabel->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, axisLabelWidth)->minimumLineHeight(a_theme.fieldHeight)->text(a_axes[i].first);
			x += axisLabelWidth;

			auto fieldHost = a_parent->make(MV::guid("axisField_"));
			fieldHost->position(MV::point(x, 0.0f));
			size_t axisIndex = i;
			makeScrubField(fieldHost, a_services, a_theme, a_focus, "value", MV::size(fieldWidth, a_theme.fieldHeight), a_axes[i].second,
				[a_onChange, axisIndex](float a_value) {
					if (a_onChange) {
						a_onChange(axisIndex, a_value);
					}
				},
				[a_onHistory, axisIndex](float a_oldValue, float a_newValue) {
					if (a_onHistory) {
						a_onHistory(axisIndex, a_oldValue, a_newValue);
					}
				});
			x += fieldWidth + a_theme.pad;
		}
	}

	std::shared_ptr<MV::Scene::Button> makeCycleButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, std::vector<std::string> a_labels, int a_initialIndex, std::function<void(int)> a_onChange) {
		int startIndex = a_labels.empty() ? 0 : std::max(0, std::min<int>(a_initialIndex, static_cast<int>(a_labels.size()) - 1));
		auto button = makeButton(a_parent, a_services, a_theme, a_id, a_size, a_labels.empty() ? std::string("--") : a_labels[startIndex], nullptr);
		auto index = std::make_shared<int>(startIndex);
		std::weak_ptr<MV::Scene::Button> weakButton = button;
		for (auto&& label : a_labels) {
			label = fitLabelText(a_services, a_theme, label, a_size.width - a_theme.px(8.0f));
		}
		button->onAccept.connect("cycle", [weakButton, index, a_labels, a_onChange](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (a_labels.empty()) {
				return;
			}
			*index = (*index + 1) % static_cast<int>(a_labels.size());
			if (auto locked = weakButton.lock()) {
				locked->text(a_labels[*index]);
			}
			if (a_onChange) {
				a_onChange(*index);
			}
		});
		return button;
	}

}
