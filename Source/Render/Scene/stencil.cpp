#include "stencil.h"
#include <memory>

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Stencil);

namespace MV {
	namespace Scene {

		void Stencil::drawStencil() {
// 			shaderUpdater = [&](MV::Shader* a_shader) {
// 				a_shader->set("alphaFilter", 0.01f);
// 			};
			Drawable::defaultDrawImplementation();
		}

		int Stencil::totalStencilDepth = 0;

 		bool Stencil::preDraw() {
			if (shouldDraw) {
				glStencilMask(0xFF);
				if (totalStencilDepth++ == 0) {
					glEnable(GL_STENCIL_TEST);
					glClear(GL_STENCIL_BUFFER_BIT);
				}

				glStencilFunc(GL_ALWAYS, totalStencilDepth, totalStencilDepth);  //Always fail the stencil test
				glStencilOp(GL_INCR, GL_INCR, GL_INCR);   //Set the pixels which failed to 1
				
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				drawStencil();
				glStencilMask(0x00);
				glColorMask(true, true, true, true);
				glStencilFunc(GL_EQUAL, totalStencilDepth, totalStencilDepth);   //Only pass the stencil test where the pixel is 1 in the stencil buffer
				glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);   //Don't change the stencil buffer any further
			}
			return false;
		}

		bool Stencil::postDraw() {
			return true;
		}

		//TODO: Cache the stencil properly instead of assuming nothing changed in this node and naively drawing it again (may cause artifacts!)
		void Stencil::endDraw() {
			if (shouldDraw) {
				glColorMask(false, false, false, false);
				glStencilFunc(GL_ALWAYS, totalStencilDepth, totalStencilDepth);
				glStencilOp(GL_DECR, GL_DECR, GL_DECR);
				glStencilMask(0xFF);
				drawStencil();
				glStencilMask(0x00);
				glColorMask(true, true, true, true);

				if (--totalStencilDepth == 0) {
					glDisable(GL_STENCIL_TEST);
				}
			}
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
