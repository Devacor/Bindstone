#include "controllerCursor.h"
#include <cmath>
#include "MV/Render/sharedTextures.h"
#include "MV/Render/textures.h"

namespace MV {
	namespace Scene {

		namespace {
			const int DOT_SURFACE_SIZE = 64;
			// drawn over everything else its parent holds
			const PointPrecision CURSOR_DEPTH = 1000000.0f;

			// white disc with an antialiased edge; straight alpha, as the default shader multiplies it in
			std::shared_ptr<OwnedSurface> makeDotSurface() {
				int bitsPerPixel = 0;
				Uint32 redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
				SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_ABGR8888, &bitsPerPixel, &redMask, &greenMask, &blueMask, &alphaMask);
				SDL_Surface* surface = SDL_CreateRGBSurface(0, DOT_SURFACE_SIZE, DOT_SURFACE_SIZE, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
				SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
				auto* pixels = static_cast<uint32_t*>(surface->pixels);
				const int pitch = surface->pitch / 4;
				const float half = DOT_SURFACE_SIZE * 0.5f;
				const float radius = half - 1.0f;
				for (int y = 0; y < DOT_SURFACE_SIZE; ++y) {
					for (int x = 0; x < DOT_SURFACE_SIZE; ++x) {
						const float dx = (x + 0.5f) - half;
						const float dy = (y + 0.5f) - half;
						const float coverage = std::min(1.0f, std::max(0.0f, radius - std::sqrt(dx * dx + dy * dy) + 0.5f));
						pixels[y * pitch + x] = (static_cast<uint32_t>(coverage * 255.0f + 0.5f) << 24) | 0x00FFFFFFu;
					}
				}
				return OwnedSurface::make(surface);
			}
		}

		ControllerCursor::ControllerCursor(const std::shared_ptr<Node>& a_parent, ControllerDevice& a_controller, SharedTextures& a_textures) :
			controller(a_controller),
			textures(a_textures) {

			cursorNode = a_parent->make(MV::guid("controllerCursor_"));
			cursorNode->serializable(false);
			cursorNode->depth(CURSOR_DEPTH);
			cursorNode->hide();
			dot(14.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
		}

		ControllerCursor::~ControllerCursor() {
			if (cursorNode) {
				cursorNode->removeFromParent();
			}
		}

		ControllerCursor& ControllerCursor::dot(PointPrecision a_diameter, const Color& a_color) {
			if (artNode) {
				artNode->removeFromParent();
				artNode.reset();
			}
			makeArt = [this, a_diameter, a_color]() {
				artNode = cursorNode->make("art");
				auto disc = [this]() {
					return textures.surface("controllerCursorDot", [] { return makeDotSurface(); })->makeHandle();
				};
				const PointPrecision rim = std::max(1.0f, a_diameter * 0.15f);
				const PointPrecision outer = a_diameter + rim * 2.0f;
				artNode->attach<Sprite>()->bounds(BoxAABB<>(point(-outer * 0.5f, -outer * 0.5f), size(outer, outer)))->texture(disc())->color(Color(0.0f, 0.0f, 0.0f, 0.75f));
				artNode->attach<Sprite>()->bounds(BoxAABB<>(point(-a_diameter * 0.5f, -a_diameter * 0.5f), size(a_diameter, a_diameter)))->texture(disc())->color(a_color);
			};
			return *this;
		}

		ControllerCursor& ControllerCursor::image(const std::shared_ptr<TextureHandle>& a_image, const Size<>& a_size, const Point<>& a_hotspot) {
			if (artNode) {
				artNode->removeFromParent();
				artNode.reset();
			}
			makeArt = [this, a_image, a_size, a_hotspot]() {
				artNode = cursorNode->make("art");
				artNode->attach<Sprite>()->bounds(BoxAABB<>(point(-a_hotspot.x, -a_hotspot.y), a_size))->texture(a_image);
			};
			return *this;
		}

		void ControllerCursor::update() {
			if (!controller.cursorShown()) {
				cursorNode->hide();
				return;
			}
			if (!artNode && makeArt) {
				makeArt();
			}
			auto parent = cursorNode->parent();
			auto at = parent ? parent->localFromScreen(controller.cursorPosition()) : Point<>();
			cursorNode->position(point(at.x, at.y));
			cursorNode->show();
		}

	}
}
