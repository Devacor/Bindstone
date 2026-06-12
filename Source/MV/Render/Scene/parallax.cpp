#include "parallax.h"

#include "MV/Utility/log.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

// JaiScript binding for Parallax
static jai::registrar<MV::Scene::Parallax, MV::Services> _hookParallax("Parallax",
	[](jai::dynamic_binder<MV::Scene::Parallax>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();
	builder.auto_bind();

	// Translate ratio
	builder.method("translateRatio", static_cast<MV::Point<>(MV::Scene::Parallax::*)() const>(&MV::Scene::Parallax::translateRatio));
	builder.method("translateRatio", static_cast<std::shared_ptr<MV::Scene::Parallax>(MV::Scene::Parallax::*)(const MV::Point<>&)>(&MV::Scene::Parallax::translateRatio));

	// Local offset
	builder.method("localOffset", static_cast<MV::Point<>(MV::Scene::Parallax::*)() const>(&MV::Scene::Parallax::localOffset));
	builder.method("localOffset", static_cast<std::shared_ptr<MV::Scene::Parallax>(MV::Scene::Parallax::*)(const MV::Point<>&)>(&MV::Scene::Parallax::localOffset));

	// World zoom offset
	builder.method("worldZoomOffset", static_cast<MV::Point<>(MV::Scene::Parallax::*)() const>(&MV::Scene::Parallax::worldZoomOffset));
	builder.method("worldZoomOffset", static_cast<std::shared_ptr<MV::Scene::Parallax>(MV::Scene::Parallax::*)(const MV::Point<>&)>(&MV::Scene::Parallax::worldZoomOffset));

	// Enable/disable
	builder.method("enabled", static_cast<bool(MV::Scene::Parallax::*)() const>(&MV::Scene::Parallax::enabled));
	builder.method("enabled", static_cast<std::shared_ptr<MV::Scene::Parallax>(MV::Scene::Parallax::*)(bool)>(&MV::Scene::Parallax::enabled));
	builder.method("enable", &MV::Scene::Parallax::enable);
	builder.method("disable", &MV::Scene::Parallax::disable);
});

namespace MV {
	namespace Scene {
		Parallax::Parallax(const std::weak_ptr<Node>& a_owner) :
			jai::property_owner<Parallax, Component>(a_owner) {
		}

		void Parallax::initialize() {
			reattachImplementation();
		}

		void Parallax::postLoadInitialize() {
			// reattachImplementation() fires during reattached() when the component
			// is set on its owner node, but at that point the parent node is not yet
			// in the tree (fixChildOwnership runs later in postLoadStep). Re-run it
			// here so the parentObserver and cameraObserver signals connect correctly
			// after the full tree is assembled.
			reattachImplementation();
		}

		std::shared_ptr<Component> Parallax::cloneHelper(const std::shared_ptr<Component>& a_clone) {
			Component::cloneHelper(a_clone);
			auto parallaxClone = std::static_pointer_cast<Parallax>(a_clone);
			return a_clone;
		}

		Point<> Parallax::absolutePosition() const {
			auto current = owner();
			Point<> result;
			while (current = current->parent()) {
				result += current->position();
			}
			return result;
		}

		void Parallax::reattachImplementation() {
			if (ownerIsAlive()) {
				if (auto ownerParent = owner()->parent()) {
					parentObserver = ownerParent->onMatrixDirty.connect([=](const std::shared_ptr<Node>&) {
						needsUpdate = true;
					});
					cameraObserver = owner()->renderer().onCameraUpdated.connect([=](int32_t a_cameraId) {
						if (owner() && owner()->cameraId() == a_cameraId) {
							needsUpdate = true;
						}
					});
				}
			}
			needsUpdate = true;
		}

		void Parallax::updateImplementation(double) {
			if (needsUpdate && isEnabled && owner()) {
				if (auto ownerParent = owner()->parent()) {
					needsUpdate = false;
					auto ourPositionOffset = owner()->position(*ourLocalOffset)->worldPosition();
					auto parentPosition = ownerParent->worldPosition() + *ourZoomOffset;
					owner()->worldPosition((parentPosition * -1.0f) + (parentPosition * ourTranslateRatio.get()) + ourPositionOffset);
				}
			}
		}

		void Parallax::detachImplementation() {
			parentObserver.reset();
		}

	}
}
