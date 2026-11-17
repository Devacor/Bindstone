#ifndef _MV_SCENE_CONTROLLERCURSOR_H_
#define _MV_SCENE_CONTROLLERCURSOR_H_

#include <functional>
#include <memory>
#include "MV/Interface/controllerDevice.h"
#include "MV/Render/Scene/node.h"
#include "MV/Render/Scene/sprite.h"

namespace MV {
	class SharedTextures;

	namespace Scene {
		// The pointer a controller moves in cursor mode (ControllerDevice::cursor), drawn on screen: a dot unless
		// given an image. It shows only while that pointer is the one in use. update() once a frame, after the
		// controller's navigate.
		class ControllerCursor {
		public:
			ControllerCursor(const std::shared_ptr<Node>& a_parent, ControllerDevice& a_controller, SharedTextures& a_textures);
			~ControllerCursor();
			ControllerCursor(const ControllerCursor&) = delete;
			ControllerCursor& operator=(const ControllerCursor&) = delete;

			// A filled circle with a dark rim, its middle on the pointer. Sizes are in the parent's units.
			ControllerCursor& dot(PointPrecision a_diameter, const Color& a_color);
			// An image instead: a_hotspot is the point of it, from its top left, that sits on the pointer.
			ControllerCursor& image(const std::shared_ptr<TextureHandle>& a_image, const Size<>& a_size, const Point<>& a_hotspot = Point<>());

			void update();

			bool visible() const { return cursorNode->visible(); }
			std::shared_ptr<Node> node() const { return cursorNode; }

		private:
			ControllerDevice& controller;
			SharedTextures& textures;
			std::shared_ptr<Node> cursorNode;
			// made the first time the cursor shows, so a controller that never points costs no texture
			std::shared_ptr<Node> artNode;
			std::function<void()> makeArt;
		};
	}
}

#endif
