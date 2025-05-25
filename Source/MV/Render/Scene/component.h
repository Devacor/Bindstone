#ifndef _MV_SCENE_COMPONENT_H_
#define _MV_SCENE_COMPONENT_H_

#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <map>

#include "MV/Render/render.h"
#include "MV/Render/textures.h"
#include "MV/Render/points.h"
#include "MV/Render/boxaabb.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/signal.hpp"
#include "MV/Utility/task.h"
#include "MV/Utility/visitor.hpp"
#include "MV/Utility/services.hpp"
#include "MV/Utility/properties.hpp"

#define ComponentDerivedAccessors(ComponentType) \
MV::Scene::SafeComponent<ComponentType> clone(const std::shared_ptr<MV::Scene::Node> &a_parent) { \
	return MV::Scene::SafeComponent<ComponentType>(a_parent, std::static_pointer_cast<ComponentType>(cloneImplementation(a_parent))); \
} \
MV::Scene::SafeComponent<ComponentType> safe() { \
	return MV::Scene::SafeComponent<ComponentType>(owner(), std::static_pointer_cast<ComponentType>(shared_from_this())); \
} \
std::string id() const { \
	return MV::Scene::Component::id(); \
} \
std::shared_ptr<ComponentType> id(const std::string &a_id) { \
	return std::static_pointer_cast<ComponentType>(MV::Scene::Component::id(a_id)); \
} \
std::shared_ptr<ComponentType> serializable(bool a_allow) { \
	return std::static_pointer_cast<ComponentType>(MV::Scene::Component::serializable(a_allow)); \
} \
std::shared_ptr<ComponentType> bounds(const MV::BoxAABB<> &a_localBounds){ \
	return std::static_pointer_cast<ComponentType>(MV::Scene::Component::bounds(a_localBounds)); \
} \
MV::BoxAABB<> bounds(){ \
	return MV::Scene::Component::bounds(); \
} \
std::shared_ptr<ComponentType> screenBounds(const MV::BoxAABB<int> &a_screenBounds){ \
	return std::static_pointer_cast<ComponentType>(MV::Scene::Component::screenBounds(a_screenBounds)); \
} \
MV::BoxAABB<int> screenBounds() { \
	return MV::Scene::Component::screenBounds(); \
} \
std::shared_ptr<ComponentType> worldBounds(const MV::BoxAABB<> &a_worldBounds){ \
	return std::static_pointer_cast<ComponentType>(MV::Scene::Component::worldBounds(a_worldBounds)); \
} \
MV::BoxAABB<> worldBounds() { \
	return MV::Scene::Component::worldBounds(); \
}

namespace cereal {
	class PortableBinaryInputArchive;
	class JSONInputArchive;
}

namespace MV {

	namespace Scene {
		void appendQuadVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset);
		void appendNineSliceVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset);

		class Node;
		class Component;

		template <typename T>
		class SafeComponent {
		public:
			SafeComponent(const std::shared_ptr<const Node> &a_node = nullptr, const std::shared_ptr<T> &a_component = nullptr) :
				wrappedNode(a_node),
				wrappedComponent(a_component) {
			}

			void operator=(const SafeComponent<T> &a_other) {
				wrappedNode = a_other.wrappedNode;
				wrappedComponent = a_other.wrappedComponent;
			}

			void operator=(const std::shared_ptr<T> &a_component) {
				wrappedNode = a_component->owner();
				wrappedComponent = a_component;
			}

			std::shared_ptr<T> operator->() const {
				return wrappedComponent;
			}

			operator bool() const {
				return wrappedComponent && wrappedNode;
			}

			bool operator!() const {
				return !static_cast<bool>(*this);
			}

			bool operator==(const SafeComponent& a_other) const {
				return a_other.wrappedComponent == wrappedComponent;
			}

			bool operator!=(const SafeComponent& a_other) const {
				return a_other.wrappedComponent != wrappedComponent;
			}

			bool operator==(const std::shared_ptr<T> &a_other) const {
				return a_other == wrappedComponent;
			}

			bool operator!=(const std::shared_ptr<T> &a_other) const {
				return a_other != wrappedComponent;
			}

			std::shared_ptr<T> self() const {
				return wrappedComponent;
			}

			std::shared_ptr<T> get() const {
				return self();
			}

			std::shared_ptr<Node> owner() const {
				return wrappedComponent->owner();
			}

			template<typename NewType>
			SafeComponent<NewType> cast() const {
				return SafeComponent<NewType>(wrappedNode, std::static_pointer_cast<NewType>(wrappedComponent));
			}

			void reset() {
				wrappedComponent.reset();
				wrappedNode.reset();
			}

		private:
			std::shared_ptr<const Node> wrappedNode;
			std::shared_ptr<T> wrappedComponent;
		};

		class Component : public std::enable_shared_from_this<Component> {
			friend Node;
			friend cereal::access;

		public:
			virtual ~Component() {detachImplementation();}

			virtual bool draw() { return true; }
			virtual void endDraw() { }

			void update(double a_delta) {
				accumulatedDelta += a_delta;
				updateImplementation(a_delta);
				if (rootTask) {
					if (!rootTask->update(a_delta)) {
						rootTask.reset();
					}
				}
			}

			std::shared_ptr<Component> bounds(const BoxAABB<> &a_localBounds) {
				auto self = shared_from_this();
				boundsImplementation(a_localBounds);
				return self;
			}

			std::shared_ptr<Component> screenBounds(const BoxAABB<int> &a_localBounds);
			std::shared_ptr<Component> worldBounds(const BoxAABB<> &a_worldBounds);

			BoxAABB<> bounds() {
				return boundsImplementation();
			}
			BoxAABB<int> screenBounds();
			BoxAABB<> worldBounds();

			std::shared_ptr<Node> owner() const;

			SafeComponent<Component> clone(const std::shared_ptr<Node> &a_parent) {
				auto result = SafeComponent<Component>(a_parent, cloneImplementation(a_parent));
				properties.cloneToTarget(result->properties);
				return result;
			}

			SafeComponent<Component const> safe() const {
				return SafeComponent<Component const>(owner(), shared_from_this());
			}

			SafeComponent<Component> safe() {
				return SafeComponent<Component>(owner(), shared_from_this());
			}

			void detach();
			void attach(const std::shared_ptr<Node> &a_parent);

			std::string id() const {
				return componentId;
			}

			std::shared_ptr<Component> id(const std::string &a_id) {
				componentId = a_id;
				return shared_from_this();
			}

			Task& task() {
				if (!rootTask) {
					rootTask = std::make_unique<Task>();
				}
				return *rootTask;
			}

			std::shared_ptr<Component> serializable(bool a_serializable) {
				allowSerialize = a_serializable;
				return shared_from_this();
			}

			bool serializable() const {
				return allowSerialize;
			}

			//owner death *can* occur before node death in cases where a button deletes itself.
			//In known cases where callback order can cause this to occur it's best we have an explicit query.
			bool ownerIsAlive() const;

		protected:
			void reattached(const std::shared_ptr<Node> &a_parent);
			virtual void detachImplementation() {}
			virtual void reattachImplementation() {}
			virtual void postLoadInitialize() {}

			virtual void onOwnerDestroyed() {}

			Component(const std::weak_ptr<Node> &a_owner);

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent);

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			void notifyParentOfBoundsChange();
			void notifyParentOfComponentChange();
			virtual BoxAABB<> boundsImplementation() {
				return BoxAABB<>();
			}

			virtual void boundsImplementation(const BoxAABB<> &) {
				//requires implementation in child class
			}
			
			virtual void initialize() {} //called after creation by node

			Component(const Component& a_rhs) = delete;
			Component& operator=(const Component& a_rhs) = delete;

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				properties.save(archive);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version == 0) { //Prior to property system
					properties.load(archive, {"componentId", "componentOwner"});
				} else { //Now all properties can just be loaded.
					properties.load(archive);
				}
				if (accumulatedDelta == 0.0) {
					accumulatedDelta = MV::randomNumber(0.0f, 1.0f); //avoid awkward synchronization
				}
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Component> &construct, std::uint32_t const version) {
				construct(std::shared_ptr<Node>());
				construct->load(archive, version);
				construct->initialize();
			}

			virtual void updateImplementation(double a_delta) {}
			double accumulatedDelta = 0.0;

			PropertyRegistry properties;
		private:
			bool allowSerialize = true;

			std::unique_ptr<Task> rootTask;
			MV_PROPERTY((std::string), componentId);
			//does not clone.
			MV_PROPERTY((std::weak_ptr<Node>), componentOwner, {}, [](Property<std::weak_ptr<Node>> &, Property<std::weak_ptr<Node>>&){});
		};
	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenecomponent);

#endif
