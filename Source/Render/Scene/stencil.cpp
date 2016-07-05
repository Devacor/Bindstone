#include "stencil.h"
#include <memory>

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Stencil);

namespace MV {
	namespace Scene {

// 		void Stencil::refreshTexture(bool a_forceRefreshEvenIfNotDirty /*= true*/) {
// 			if (owner()->renderer().headless()) { return; }
// 
// 			if (a_forceRefreshEvenIfNotDirty || dirtyTexture) {
// 				auto originalShaderId = shader();
// 				SCOPE_EXIT{ dirtyTexture = false;  shader(originalShaderId); };
// 				shader(refreshShaderId);
// 				bool emptyCapturedBounds = capturedBounds.empty();
// 				auto pointAABB = emptyCapturedBounds ? bounds() : capturedBounds;
// 				pointAABB += capturedOffset;
// 				auto textureSize = round<int>(pointAABB.size());
// 				if (!clippedTexture || clippedTexture->size() != textureSize) {
// 					clippedTexture = DynamicTextureDefinition::make("", textureSize, { 0.0f, 0.0f, 0.0f, 0.0f });
// 				}
// 
// 				texture(clippedTexture->makeHandle(textureSize));
// 				{
// 					auto framebuffer = owner()->renderer().makeFramebuffer(round<int>(pointAABB.minPoint), textureSize, clippedTexture->textureId())->start();
// 
// 					SCOPE_EXIT{ owner()->renderer().defaultBlendFunction(); };
// 
// 					owner()->drawChildren(TransformMatrix());
// 				}
// 				notifyParentOfComponentChange();
// 			}
// 		}

		void Stencil::drawStencil() {

		}

		int Stencil::totalFramebuffers = 0;

		bool Stencil::preDraw() {
			if (shouldDraw) {
				if (totalFramebuffers++ == 0)
					glEnable(GL_STENCIL_TEST);

				glColorMask(false, false, false, false);
				glDepthMask(false);
				glStencilFunc(GL_ALWAYS, 0, 0);
				glStencilOp(GL_INCR, GL_INCR, GL_INCR);

				drawStencil();

				glColorMask(true, true, true, true);
				glDepthMask(true);
				glStencilFunc(GL_EQUAL, 1, 0xff);
				glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

				return true;
			}
			return false;
		}

		bool Stencil::postDraw() {
			if (shouldDraw) {
				glColorMask(false, false, false, false);
				glDepthMask(false);
				glStencilFunc(GL_ALWAYS, 1, 0xff);
				glStencilOp(GL_DECR, GL_DECR, GL_DECR);

				drawStencil();

				glColorMask(true, true, true, true);
				glDepthMask(true);

				if (--totalFramebuffers == 0)
					glDisable(GL_STENCIL_TEST);
			}
			return !shouldDraw;
		}

		Stencil::Stencil(const std::weak_ptr<Node> &a_owner) :
			Sprite(a_owner) {
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

		std::shared_ptr<Stencil> Stencil::bounds(const BoxAABB<> &a_bounds) {
			return std::static_pointer_cast<Stencil>(Sprite::bounds(a_bounds));
		}

		BoxAABB<> Stencil::bounds() {
			return boundsImplementation();
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
