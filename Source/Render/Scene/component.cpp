#include "component.h"
#include "node.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Component);

namespace MV {
	namespace Scene {
		/*Vertex Indices*\
		     0     3

		     1     2
		\*Vertex Indices*/
		void appendQuadVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset) {
			std::vector<GLuint> quadIndices{
				a_pointOffset, a_pointOffset + 1, a_pointOffset + 2,
				a_pointOffset + 2, a_pointOffset + 3, a_pointOffset
			};
			a_indices.insert(a_indices.end(), quadIndices.begin(), quadIndices.end());
		}

		/*Vertex Indices*\
			0 15  14  3
			8  4   7 13
			9  5   6 12
			1 10  11  2
		\*Vertex Indices*/
		void appendNineSliceVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset) {
			std::vector<GLuint> quadIndices{
				a_pointOffset, a_pointOffset + 8, a_pointOffset + 4,
				a_pointOffset + 4, a_pointOffset + 15, a_pointOffset,

				a_pointOffset + 15, a_pointOffset + 4, a_pointOffset + 7,
				a_pointOffset + 7, a_pointOffset + 14, a_pointOffset + 15,

				a_pointOffset + 14, a_pointOffset + 7, a_pointOffset + 13,
				a_pointOffset + 13, a_pointOffset + 3, a_pointOffset + 14,

				a_pointOffset + 8, a_pointOffset + 9, a_pointOffset + 5,
				a_pointOffset + 5, a_pointOffset + 4, a_pointOffset + 8,

				a_pointOffset + 4, a_pointOffset + 5, a_pointOffset + 6,
				a_pointOffset + 6, a_pointOffset + 7, a_pointOffset + 4,

				a_pointOffset + 7, a_pointOffset + 6, a_pointOffset + 12,
				a_pointOffset + 12, a_pointOffset + 13, a_pointOffset + 7,

				a_pointOffset + 9, a_pointOffset + 1, a_pointOffset + 10,
				a_pointOffset + 10, a_pointOffset + 5, a_pointOffset + 9,

				a_pointOffset + 5, a_pointOffset + 10, a_pointOffset + 11,
				a_pointOffset + 11, a_pointOffset + 6, a_pointOffset + 5,

				a_pointOffset + 6, a_pointOffset + 11, a_pointOffset + 2,
				a_pointOffset + 2, a_pointOffset + 12, a_pointOffset + 6
			};
			a_indices.insert(a_indices.end(), quadIndices.begin(), quadIndices.end());
		}

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
			} catch (MV::PointerException &) {
			}
		}

		std::shared_ptr<Node> Component::owner() const {
			auto lockedComponentOwner = componentOwner.lock();
			MV::require<MV::PointerException>(lockedComponentOwner != nullptr, "Component owner has expired! You are storing a reference to the component, but not the node that owns it!");
			return lockedComponentOwner;
		}

		Component::Component(const std::weak_ptr<Node> &a_owner) :
			componentOwner(a_owner) {
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
			if (ownerIsAlive()) {
				owner()->detach(shared_from_this());
			}
		}

		void Component::attach(const std::shared_ptr<Node> &a_parent) {
			a_parent->attach(shared_from_this());
		}

		void Component::reattached(const std::shared_ptr<Node> &a_parent) {
			componentOwner = a_parent;
			reattachImplementation();
		}

	}
}
