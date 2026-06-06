#include "clipped.h"
#include <memory>

#include "MV/Utility/scopeGuard.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

// JaiScript binding for Clipped
static jai::registrar<MV::Scene::Clipped, MV::Services> _hookClipped("Clipped",
	[](jai::dynamic_binder<MV::Scene::Clipped>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Sprite>();
	builder.auto_bind();

	// Refresh
	builder.method("refreshTexture", &MV::Scene::Clipped::refreshTexture);
	builder.method("refreshEveryFrame", static_cast<bool(MV::Scene::Clipped::*)() const>(&MV::Scene::Clipped::refreshEveryFrame));
	builder.method("refreshEveryFrame", static_cast<std::shared_ptr<MV::Scene::Clipped>(MV::Scene::Clipped::*)(bool)>(&MV::Scene::Clipped::refreshEveryFrame));

	// Capture bounds
	builder.method("clearCaptureBounds", &MV::Scene::Clipped::clearCaptureBounds);
	builder.method("captureBounds", static_cast<MV::BoxAABB<>(MV::Scene::Clipped::*)()>(&MV::Scene::Clipped::captureBounds));
	builder.method("captureBounds", static_cast<std::shared_ptr<MV::Scene::Clipped>(MV::Scene::Clipped::*)(const MV::BoxAABB<>&)>(&MV::Scene::Clipped::captureBounds));

	// Capture offset
	builder.method("clearCaptureOffset", &MV::Scene::Clipped::clearCaptureOffset);
	builder.method("captureOffset", static_cast<MV::Point<>(MV::Scene::Clipped::*)() const>(&MV::Scene::Clipped::captureOffset));
	builder.method("captureOffset", static_cast<std::shared_ptr<MV::Scene::Clipped>(MV::Scene::Clipped::*)(const MV::Point<>&)>(&MV::Scene::Clipped::captureOffset));

	// Capture size
	builder.method("captureSize", static_cast<std::shared_ptr<MV::Scene::Clipped>(MV::Scene::Clipped::*)(const MV::Size<>&, bool)>(&MV::Scene::Clipped::captureSize));

	// Refresh shader
	builder.method("refreshShader", static_cast<std::string(MV::Scene::Clipped::*)()>(&MV::Scene::Clipped::refreshShader));
	builder.method("refreshShader", static_cast<std::shared_ptr<MV::Scene::Clipped>(MV::Scene::Clipped::*)(const std::string&)>(&MV::Scene::Clipped::refreshShader));
});

CEREAL_REGISTER_TYPE(MV::Scene::Clipped);
CEREAL_REGISTER_DYNAMIC_INIT(mv_sceneclipped);
CEREAL_CLASS_VERSION(MV::Scene::Clipped, 1);

namespace MV {
	namespace Scene {

		void Clipped::refreshTexture(bool a_forceRefreshEvenIfNotDirty /*= true*/) {
			if (owner()->renderer().headless()) { return; }

			if (a_forceRefreshEvenIfNotDirty || dirtyTexture) {
				auto originalShaderId = shader();
				SCOPE_EXIT{ dirtyTexture = false;  shader(originalShaderId); };
				shader(refreshShaderId);
				bool emptyCapturedBounds = capturedBounds->empty();
				auto pointAABB = emptyCapturedBounds ? bounds() : *capturedBounds;
				pointAABB += capturedOffset;
				auto textureSize = round<int>(pointAABB.size());
				if (!clippedTexture || clippedTexture->size() != textureSize) {
					clippedTexture = DynamicTextureDefinition::make("", textureSize, { 0.0f, 0.0f, 0.0f, 0.0f });
					// Device-back before the first load so the FBO attachment and the sampled texture are one object.
					clippedTexture->renderDevice(owner()->renderer().device());
				}

				texture(clippedTexture->makeHandle(textureSize));
				{
					//todo: Why doesn't the framebuffer work with non-zero x, y values?
					auto framebuffer = owner()->renderer().makeFramebuffer(MV::Point<int>(), textureSize, clippedTexture->textureId())->start();

					SCOPE_EXIT{ owner()->renderer().defaultBlendFunction(); };

					TransformMatrix renderOrigin;
					renderOrigin.translate(pointAABB.minPoint * -1.0f);
					owner()->drawChildren(renderOrigin);
				}
				notifyParentOfComponentChange();
			}
		}

		bool Clipped::preDraw() {
			if (shouldDraw) {
				refreshTexture(forceRefreshEveryFrame);
				return true;
			}
			return false;
		}

		bool Clipped::postDraw() {
			return !shouldDraw;
		}

		Clipped::Clipped(const std::weak_ptr<Node> &a_owner) :
			jai::property_owner<Clipped, Sprite>(a_owner){

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

		BoxAABB<> Clipped::captureBounds() {
			return capturedBounds->empty() ? bounds() : capturedBounds;
		}

		std::shared_ptr<Clipped> MV::Scene::Clipped::clearCaptureOffset() {
			capturedOffset->clear();
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
			Point<> topLeft;
			Point<> bottomRight = toPoint(a_size);

			topLeft -= a_centerPoint;
			bottomRight -= a_centerPoint;
			
			return captureBounds({ topLeft, bottomRight });
		}

		std::shared_ptr<Clipped> Clipped::captureSize(const Size<> &a_size, bool a_center /*= false*/) {
			return captureSize(a_size, (a_center) ? MV::point(a_size.width / 2.0f, a_size.height / 2.0f) : MV::point(0.0f, 0.0f));
		}

		void Clipped::boundsImplementation(const BoxAABB<> &a_bounds) {
			dirtyTexture = true;
			Sprite::boundsImplementation(a_bounds);
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
			auto clippedClone = std::static_pointer_cast<Clipped>(a_clone);
			clippedClone->refreshShader(refreshShaderId);
			return a_clone;
		}

	}
}
