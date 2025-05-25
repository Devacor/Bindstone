#include "parallax.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include "MV/Utility/log.h"

CEREAL_REGISTER_TYPE(MV::Scene::Parallax);
CEREAL_CLASS_VERSION(MV::Scene::Parallax, 3);
CEREAL_REGISTER_DYNAMIC_INIT(mv_sceneparallax);

namespace MV {
	namespace Scene {
		Parallax::Parallax(const std::weak_ptr<Node>& a_owner) :
			Component(a_owner) {
		}

		void Parallax::initialize() {
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
