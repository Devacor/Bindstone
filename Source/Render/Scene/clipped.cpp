#include "clipped.h"
#include <memory>
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Clipped);

namespace MV {
	namespace Scene {

		std::shared_ptr<Clipped> Clipped::blockClippedChildTaps(MouseState& a_mouse, int a_globalClickPriority) {
// 			auto clickBlocker = [&, a_globalClickPriority](MouseState& a_mouse) {
// 				auto parentNode = owner();
// 				if (!parentNode->screenFromLocal(bounds()).contains(a_mouse.position())) {
// 					std::vector<size_t> priority;
// 					priority.push_back(a_globalClickPriority);
// 					a_mouse.queueExclusiveAction({ true, priority, [&]() {
// 						std::cout << "Clipped node ate a tap!" << std::endl;
// 					}, []() {}, owner()->id() });
// 				}
// 			};
// 			onLeftMouseDownHandle = a_mouse.onLeftMouseDown.connect(clickBlocker);
// 			onLeftMouseUpHandle = a_mouse.onLeftMouseUp.connect(clickBlocker);
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		std::shared_ptr<Clipped> Clipped::unblockClippedChildTaps() {
			onLeftMouseDownHandle.reset();
			onLeftMouseUpHandle.reset();
		}

		void Clipped::refreshTexture(bool a_forceRefreshEvenIfNotDirty /*= true*/) {
			if (a_forceRefreshEvenIfNotDirty || dirtyTexture) {
				auto originalShaderId = shader();
				SCOPE_EXIT{ dirtyTexture = false;  shader(originalShaderId); };
				shader(refreshShaderId);
				bool emptyCapturedBounds = capturedBounds.empty();
				auto pointAABB = emptyCapturedBounds ? bounds() : capturedBounds;
				pointAABB += capturedOffset;
				auto textureSize = cast<int>(pointAABB.size());
				if (!clippedTexture || clippedTexture->size() != textureSize) {
					clippedTexture = DynamicTextureDefinition::make("", textureSize, { 0.0f, 0.0f, 0.0f, 0.0f });
				}

				texture(clippedTexture->makeHandle(textureSize));
				{
					auto framebuffer = owner()->renderer().makeFramebuffer(cast<int>(pointAABB.minPoint), textureSize, clippedTexture->textureId())->start();

					SCOPE_EXIT{ owner()->renderer().defaultBlendFunction(); };

					owner()->drawChildren(TransformMatrix());
				}
				notifyParentOfComponentChange();
			}
		}

		bool Clipped::preDraw() {
			if (shouldDraw) {
				refreshTexture(false);
				return true;
			}
			return false;
		}

		bool Clipped::postDraw() {
			return !shouldDraw;
		}

		Clipped::Clipped(const std::weak_ptr<Node> &a_owner) :
			Sprite(a_owner),
			refreshShaderId(PREMULTIPLY_ID){
			
			shaderProgramId = DEFAULT_ID;
		}

		std::shared_ptr<Clipped> Clipped::clearCaptureBounds() {
			capturedBounds = BoxAABB<>();
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		std::shared_ptr<Clipped> Clipped::captureBounds(const BoxAABB<> &a_newCapturedBounds) {
			capturedBounds = a_newCapturedBounds;
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		BoxAABB<> Clipped::captureBounds() const {
			return capturedBounds;
		}

		std::shared_ptr<Clipped> MV::Scene::Clipped::clearCaptureOffset() {
			capturedOffset.clear();
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		std::shared_ptr<Clipped> Clipped::captureOffset(const Point<> &a_newCapturedOffset) {
			capturedOffset = a_newCapturedOffset;
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		Point<> Clipped::captureOffset() const{
			return capturedOffset;
		}

		std::shared_ptr<Clipped> Clipped::captureSize(const Size<> &a_size, const Point<> &a_centerPoint) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			Point<> topLeft;
			Point<> bottomRight = toPoint(a_size);

			topLeft -= a_centerPoint;
			bottomRight -= a_centerPoint;
			
			return captureBounds({ topLeft, bottomRight });
		}

		std::shared_ptr<Clipped> Clipped::captureSize(const Size<> &a_size, bool a_center /*= false*/) {
			return captureSize(a_size, (a_center) ? point(a_size.width / 2.0f, a_size.height / 2.0f) : point(0.0f, 0.0f));
		}

		std::shared_ptr<Clipped> Clipped::bounds(const BoxAABB<> &a_bounds) {
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(Sprite::bounds(a_bounds));
		}

		BoxAABB<> Clipped::bounds() {
			return boundsImplementation();
		}

		std::shared_ptr<Clipped> Clipped::size(const Size<> &a_size, const Point<> &a_centerPoint) {
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(Sprite::size(a_size, a_centerPoint));
		}

		std::shared_ptr<Clipped> Clipped::size(const Size<> &a_size, bool a_center /*= false*/) {
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(Sprite::size(a_size, a_center));
		}

		std::shared_ptr<Clipped> Clipped::refreshShader(const std::string &a_refreshShaderId) {
			refreshShaderId = a_refreshShaderId;
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		std::string Clipped::refreshShader() {
			return refreshShaderId;
		}

		void Clipped::initialize() {
			Sprite::initialize();
			dirtyObserveSignal = owner()->onChange.connect([&](const std::shared_ptr<Node>& a_ourOwner) {
				dirtyTexture = true;
			});
		}

		std::shared_ptr<Component> Clipped::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Sprite::cloneHelper(a_clone);
			auto textClone = std::static_pointer_cast<Clipped>(a_clone);
			textClone->refreshShader(refreshShaderId);
			textClone->capturedBounds = capturedBounds;
			textClone->capturedOffset = capturedOffset;
			return a_clone;
		}

		std::shared_ptr<Clipped> Clipped::clone(const std::shared_ptr<Node> &a_parent) {
			return std::static_pointer_cast<Clipped>(cloneImplementation(a_parent));
		}

	}
}
