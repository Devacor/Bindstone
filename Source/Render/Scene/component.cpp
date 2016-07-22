#include "component.h"
#include "node.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Component);
CEREAL_CLASS_VERSION(MV::Scene::Component, 1);

namespace MV {
	namespace Scene {
		void Component::notifyParentOfBoundsChange() {
			if (ownerIsAlive()) {
				owner()->markBoundsDirty();
			}
		}

		bool Component::ownerIsAlive() const {
			return !componentOwner.expired();
		}

		void Component::notifyParentOfComponentChange() {
			try {
				owner()->onComponentUpdateSignal(shared_from_this());
			} catch (PointerException &) {
			}
		}

		std::shared_ptr<Node> Component::owner() const {
			require<PointerException>(!componentOwner.expired(), "Component owner has expired! You are storing a reference to the component, but not the node that owns it!");
			return componentOwner.lock();
		}

		Component::Component(const std::weak_ptr<Node> &a_owner) :
			componentOwner(a_owner),
			ourAnchors(this) {
		}

		BoxAABB<int> Component::screenBounds() {
			return owner()->screenFromLocal(boundsImplementation());
		}

		BoxAABB<> Component::worldBounds() {
			return owner()->worldFromLocal(boundsImplementation());
		}

		std::shared_ptr<Component> Component::worldBounds(const BoxAABB<> &a_worldBounds) {
			auto self = shared_from_this();
			boundsImplementation(owner()->localFromWorld(a_worldBounds));
			return self;
		}

		std::shared_ptr<Component> Component::screenBounds(const BoxAABB<int> &a_screenBounds) {
			auto self = shared_from_this();
			boundsImplementation(owner()->localFromScreen(a_screenBounds));
			return self;
		}

		std::shared_ptr<Component> Component::cloneImplementation(const std::shared_ptr<Node> &a_parent) {
			return cloneHelper(a_parent->attach<Component>().self());
		}

		std::shared_ptr<Component> Component::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			return a_clone;
		}

		void Component::detach() {
			owner()->detach(shared_from_this());
		}

		Anchors::Anchors(Component *a_self) :
			selfReference(a_self) {
		}

		Anchors::~Anchors() {
			removeFromParent();
		}

		Anchors& Anchors::parent(const std::weak_ptr<Component> &a_parent) {
			removeFromParent();
			parentReference = a_parent;
			registerWithParent();
			return *this;
		}

		Anchors& Anchors::apply() {
			if (!applying && !parentReference.expired()) {
				applying = true;
				SCOPE_EXIT{ applying = false; };
				auto previousBounds = selfReference->worldBounds();
				auto parentBounds = parentReference.lock()->worldBounds();

				selfReference->worldBounds();
			}
			return *this;
		}

		void Anchors::removeFromParent() {
			if (!parentReference.expired()) {
				auto parentShared = parentReference.lock();
				auto position = std::find(parentShared->childAnchors.begin(), parentShared->childAnchors.end(), this);
				if (position != parentShared->childAnchors.end()) {
					parentShared->childAnchors.erase(position);
				}
			}
		}

		void Anchors::registerWithParent() {
			if (!parentReference.expired()) {
				parentReference.lock()->childAnchors.push_back(this);
			}
		}
	}
}
