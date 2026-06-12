#include "stencil.h"
#include <jaiscript/signals/signal_impl.hpp>
#include <memory>

namespace MV {
	namespace Scene {

		void Stencil::drawStencil() {
			// Mask geometry writes stencil only (no color) via a colorWriteMask=0 pipeline.
			devicePipelineColorWriteMask = 0;
			Drawable::defaultDrawImplementation();
			devicePipelineColorWriteMask = 0xF;
		}

 		bool Stencil::preDraw() {
			if (shouldDraw) { owner()->renderer().stencil().push([this]{ drawStencil(); }); }
			return false;
		}

		bool Stencil::postDraw() {
			return true;
		}

		void Stencil::endDraw() {
			if (shouldDraw) { owner()->renderer().stencil().pop([this]{ drawStencil(); }); }
		}

		void Stencil::defaultDrawImplementation() {
			//Drawable::defaultDrawImplementation();
		}

		Stencil::Stencil(const std::weak_ptr<Node> &a_owner) :
			Sprite(a_owner) {
			//shaderProgramId = ALPHA_FILTER_ID;
			auto newColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
			for (auto&& point : points) {
				point = newColor;
			}
		}

		void Stencil::initialize() {
			Sprite::initialize();
		}

		std::shared_ptr<Component> Stencil::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Sprite::cloneHelper(a_clone);
			auto textClone = std::static_pointer_cast<Stencil>(a_clone);
			return a_clone;
		}

	}
}
