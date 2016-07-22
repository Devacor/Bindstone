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

		std::shared_ptr<Stencil> Stencil::clearCaptureBounds() {
			capturedBounds = BoxAABB<>();
			return std::static_pointer_cast<Stencil>(shared_from_this());
		}

		std::shared_ptr<Stencil> Stencil::captureBounds(const BoxAABB<> &a_newCapturedBounds) {
			capturedBounds = a_newCapturedBounds;
			return std::static_pointer_cast<Stencil>(shared_from_this());
		}

		BoxAABB<> Stencil::captureBounds() {
			return capturedBounds.empty() ? bounds() : capturedBounds;
		}

		std::shared_ptr<Stencil> MV::Scene::Stencil::clearCaptureOffset() {
			capturedOffset.clear();
			return std::static_pointer_cast<Stencil>(shared_from_this());
		}

		std::shared_ptr<Stencil> Stencil::captureOffset(const Point<> &a_newCapturedOffset) {
			capturedOffset = a_newCapturedOffset;
			return std::static_pointer_cast<Stencil>(shared_from_this());
		}

		Point<> Stencil::captureOffset() const{
			return capturedOffset;
		}

		std::shared_ptr<Stencil> Stencil::captureSize(const Size<> &a_size, const Point<> &a_centerPoint) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			Point<> topLeft;
			Point<> bottomRight = toPoint(a_size);

			topLeft -= a_centerPoint;
			bottomRight -= a_centerPoint;
			
			return captureBounds({ topLeft, bottomRight });
		}

		std::shared_ptr<Stencil> Stencil::captureSize(const Size<> &a_size, bool a_center /*= false*/) {
			return captureSize(a_size, (a_center) ? MV::point(a_size.width / 2.0f, a_size.height / 2.0f) : MV::point(0.0f, 0.0f));
		}

		std::shared_ptr<Stencil> Stencil::size(const Size<> &a_size, const Point<> &a_centerPoint) {
			return std::static_pointer_cast<Stencil>(Sprite::size(a_size, a_centerPoint));
		}

		std::shared_ptr<Stencil> Stencil::size(const Size<> &a_size, bool a_center /*= false*/) {
			return std::static_pointer_cast<Stencil>(Sprite::size(a_size, a_center));
		}

		std::shared_ptr<Stencil> Stencil::refreshShader(const std::string &a_refreshShaderId) {
			refreshShaderId = a_refreshShaderId;
			return std::static_pointer_cast<Stencil>(shared_from_this());
		}

		std::string Stencil::refreshShader() {
			return refreshShaderId;
		}

		void Stencil::initialize() {
			Sprite::initialize();
		}

		std::shared_ptr<Component> Stencil::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Sprite::cloneHelper(a_clone);
			auto textClone = std::static_pointer_cast<Stencil>(a_clone);
			textClone->refreshShader(refreshShaderId);
			textClone->capturedBounds = capturedBounds;
			textClone->capturedOffset = capturedOffset;
			return a_clone;
		}

	}
}
