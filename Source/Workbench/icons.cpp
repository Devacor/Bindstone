#include "icons.h"
#include "widgets.h"
#include "MV/Interface/tapDevice.h"
#include "MV/Utility/generalUtility.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace Workbench {

	namespace {
		using Vertex = std::pair<float, float>;

		bool nearSegment(float a_x, float a_y, float a_ax, float a_ay, float a_bx, float a_by, float a_halfWidth) {
			const float dx = a_bx - a_ax, dy = a_by - a_ay;
			const float lengthSquared = dx * dx + dy * dy;
			const float along = lengthSquared > 0.0f ? std::clamp(((a_x - a_ax) * dx + (a_y - a_ay) * dy) / lengthSquared, 0.0f, 1.0f) : 0.0f;
			const float px = a_ax + along * dx - a_x, py = a_ay + along * dy - a_y;
			return px * px + py * py <= a_halfWidth * a_halfWidth;
		}

		bool nearRing(float a_x, float a_y, float a_cx, float a_cy, float a_radius, float a_halfWidth) {
			return std::abs(std::hypot(a_x - a_cx, a_y - a_cy) - a_radius) <= a_halfWidth;
		}

		bool inPolygon(float a_x, float a_y, std::initializer_list<Vertex> a_points) {
			bool result = false;
			const Vertex* previous = a_points.end() - 1;
			for (const Vertex* current = a_points.begin(); current != a_points.end(); previous = current++) {
				if ((current->second > a_y) != (previous->second > a_y) &&
					a_x < (previous->first - current->first) * (a_y - current->second) / (previous->second - current->second) + current->first) {
					result = !result;
				}
			}
			return result;
		}

		bool inBox(float a_x, float a_y, float a_left, float a_top, float a_right, float a_bottom) {
			return a_x >= a_left && a_x <= a_right && a_y >= a_top && a_y <= a_bottom;
		}

		// Each icon as a shape over the unit square, y down.
		bool iconCovers(Icon a_icon, float a_x, float a_y) {
			bool result = false;
			switch (a_icon) {
				case Icon::Select:
					result = inPolygon(a_x, a_y, { {0.24f, 0.10f}, {0.24f, 0.80f}, {0.41f, 0.64f}, {0.53f, 0.90f}, {0.65f, 0.84f}, {0.53f, 0.59f}, {0.77f, 0.59f} });
				break;
				case Icon::Move:
					result = nearSegment(a_x, a_y, 0.20f, 0.50f, 0.80f, 0.50f, 0.045f) || nearSegment(a_x, a_y, 0.50f, 0.20f, 0.50f, 0.80f, 0.045f) ||
						inPolygon(a_x, a_y, { {0.96f, 0.50f}, {0.76f, 0.35f}, {0.76f, 0.65f} }) || inPolygon(a_x, a_y, { {0.04f, 0.50f}, {0.24f, 0.35f}, {0.24f, 0.65f} }) ||
						inPolygon(a_x, a_y, { {0.50f, 0.04f}, {0.35f, 0.24f}, {0.65f, 0.24f} }) || inPolygon(a_x, a_y, { {0.50f, 0.96f}, {0.35f, 0.76f}, {0.65f, 0.76f} });
				break;
				case Icon::Rotate: {
					const float angle = std::atan2(a_y - 0.54f, a_x - 0.50f) * 57.29578f;
					const bool inGap = angle > -85.0f && angle < -15.0f;
					result = (nearRing(a_x, a_y, 0.50f, 0.54f, 0.31f, 0.05f) && !inGap) || inPolygon(a_x, a_y, { {0.74f, 0.24f}, {0.48f, 0.07f}, {0.48f, 0.41f} });
				}
				break;
				case Icon::Scale:
					result = inBox(a_x, a_y, 0.12f, 0.54f, 0.46f, 0.88f) ||
						nearSegment(a_x, a_y, 0.12f, 0.12f, 0.88f, 0.12f, 0.04f) || nearSegment(a_x, a_y, 0.88f, 0.12f, 0.88f, 0.88f, 0.04f) ||
						nearSegment(a_x, a_y, 0.12f, 0.12f, 0.12f, 0.40f, 0.04f) || nearSegment(a_x, a_y, 0.60f, 0.88f, 0.88f, 0.88f, 0.04f) ||
						nearSegment(a_x, a_y, 0.48f, 0.52f, 0.66f, 0.34f, 0.045f) || inPolygon(a_x, a_y, { {0.76f, 0.24f}, {0.52f, 0.30f}, {0.70f, 0.48f} });
				break;
				case Icon::Snap: {
					const bool arch = nearRing(a_x, a_y, 0.50f, 0.52f, 0.25f, 0.095f) && a_y >= 0.52f;
					const bool poles = (std::abs(a_x - 0.25f) <= 0.095f || std::abs(a_x - 0.75f) <= 0.095f) && a_y >= 0.12f && a_y <= 0.52f;
					const bool tipGap = a_y > 0.27f && a_y < 0.33f;
					result = (arch || poles) && !tipGap;
				}
				break;
				case Icon::Grid:
					for (float line : { 0.16f, 0.50f, 0.84f }) {
						result = result || nearSegment(a_x, a_y, line, 0.12f, line, 0.88f, 0.035f) || nearSegment(a_x, a_y, 0.12f, line, 0.88f, line, 0.035f);
					}
				break;
				case Icon::Frame:
					for (float sx : { 0.14f, 0.86f }) {
						for (float sy : { 0.14f, 0.86f }) {
							const float inwardX = sx < 0.5f ? 0.24f : -0.24f, inwardY = sy < 0.5f ? 0.24f : -0.24f;
							result = result || nearSegment(a_x, a_y, sx, sy, sx + inwardX, sy, 0.045f) || nearSegment(a_x, a_y, sx, sy, sx, sy + inwardY, 0.045f);
						}
					}
					result = result || std::hypot(a_x - 0.5f, a_y - 0.5f) <= 0.11f;
				break;
				case Icon::History: {
					const float angle = std::atan2(a_y - 0.54f, a_x - 0.50f) * 57.29578f;
					const bool inGap = angle > -165.0f && angle < -95.0f;
					result = (nearRing(a_x, a_y, 0.50f, 0.54f, 0.31f, 0.045f) && !inGap) || inPolygon(a_x, a_y, { {0.26f, 0.24f}, {0.52f, 0.07f}, {0.52f, 0.41f} }) ||
						nearSegment(a_x, a_y, 0.50f, 0.56f, 0.50f, 0.38f, 0.04f) || nearSegment(a_x, a_y, 0.50f, 0.56f, 0.63f, 0.62f, 0.04f);
				}
				break;
			}
			return result;
		}

		std::shared_ptr<MV::OwnedSurface> makeIconSurface(Icon a_icon, int a_size) {
			int bitsPerPixel = 0;
			Uint32 redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
			SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_ABGR8888, &bitsPerPixel, &redMask, &greenMask, &blueMask, &alphaMask);
			SDL_Surface* surface = SDL_CreateRGBSurface(0, a_size, a_size, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
			SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
			auto* pixels = static_cast<uint32_t*>(surface->pixels);
			const int pitch = surface->pitch / 4;
			for (int y = 0; y < a_size; ++y) {
				for (int x = 0; x < a_size; ++x) {
					int covered = 0;
					for (int sample = 0; sample < 16; ++sample) {
						covered += iconCovers(a_icon, (x + (sample % 4 + 0.5f) / 4.0f) / a_size, (y + (sample / 4 + 0.5f) / 4.0f) / a_size);
					}
					pixels[y * pitch + x] = (uint32_t(covered * 255 / 16) << 24) | 0x00FFFFFFu;
				}
			}
			return MV::OwnedSurface::make(surface);
		}
	}

	std::shared_ptr<MV::TextureHandle> iconHandle(MV::Services& a_services, const Theme& a_theme, Icon a_icon) {
		const int surfaceSize = MV::roundUpPowerOfTwo(std::max(16, static_cast<int>(a_theme.px(16.0f))));
		return a_services.get<MV::SharedTextures>()->surface("workbenchIcon_" + std::to_string(static_cast<int>(a_icon)) + "_" + std::to_string(surfaceSize), [a_icon, surfaceSize] {
			return makeIconSurface(a_icon, surfaceSize);
		})->makeHandle();
	}

	IconButton::IconButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, Icon a_icon, std::function<void()> a_onClick) :
		theme(a_theme) {

		MV::Size<> buttonSize(size(a_theme), size(a_theme));
		buttonNode = a_parent->make(a_id);
		fill = attachRoundedRect(buttonNode, a_services, a_theme, buttonSize, a_theme.accent());
		fill->hide();

		const float glyphSize = a_theme.px(16.0f);
		auto glyphNode = buttonNode->make("glyph");
		glyphNode->position(MV::point(std::round((buttonSize.width - glyphSize) * 0.5f), std::round((buttonSize.height - glyphSize) * 0.5f)));
		glyph = glyphNode->attach<MV::Scene::Sprite>()->bounds(MV::size(glyphSize, glyphSize))->texture(iconHandle(a_services, a_theme, a_icon))->color(a_theme.glyph());

		auto& mouse = *a_services.get<MV::TapDevice>();
		buttonNode->attach<MV::Scene::Clickable>(mouse)->bounds(buttonSize)->onAccept.connect("click", [a_onClick](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (a_onClick) {
				a_onClick();
			}
		});
	}

	void IconButton::active(bool a_active) {
		isActive = a_active;
		if (isActive) {
			fill->show();
		} else {
			fill->hide();
		}
		glyph->color(isActive ? MV::Color(1.0f, 1.0f, 1.0f, 1.0f) : theme.glyph());
	}

}
