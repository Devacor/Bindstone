#include "parallax.h"
#include <cmath>

#include "MV/Utility/log.h"

#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {
		void Parallax::jai_auto_bind(jai::dynamic_binder<Parallax>& a_builder) {
			a_builder.method("translateRatio", static_cast<Point<>(Parallax::*)() const>(&Parallax::translateRatio));
			a_builder.method("translateRatio", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::translateRatio));

			a_builder.method("localOffset", static_cast<Point<>(Parallax::*)() const>(&Parallax::localOffset));
			a_builder.method("localOffset", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::localOffset));

			a_builder.method("worldZoomOffset", static_cast<Point<>(Parallax::*)() const>(&Parallax::worldZoomOffset));
			a_builder.method("worldZoomOffset", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::worldZoomOffset));
			a_builder.method("zoomRatio", static_cast<Point<>(Parallax::*)() const>(&Parallax::zoomRatio));
			a_builder.method("zoomRatio", static_cast<std::shared_ptr<Parallax>(Parallax::*)(const Point<>&)>(&Parallax::zoomRatio));

			a_builder.method("enabled", static_cast<bool(Parallax::*)() const>(&Parallax::enabled));
			a_builder.method("enabled", static_cast<std::shared_ptr<Parallax>(Parallax::*)(bool)>(&Parallax::enabled), jai::serial);
			a_builder.method("enable", &Parallax::enable);
			a_builder.method("disable", &Parallax::disable, jai::serial);
		}

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
					bool zooms = ourZoomRatio->x != 1.0f || ourZoomRatio->y != 1.0f;
					if (zooms) {
						if (!authoredScaleKnown) { authoredScale = owner()->scale(); authoredScaleKnown = true; }
						auto parentScale = ownerParent->worldScale();
						owner()->scale(Scale(authoredScale.x * std::pow(parentScale.x, ourZoomRatio->x - 1.0f), authoredScale.y * std::pow(parentScale.y, ourZoomRatio->y - 1.0f), authoredScale.z));
					}
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
