#ifndef _MV_SCENE_NODE_H_
#define _MV_SCENE_NODE_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <boost/lexical_cast.hpp>

#include "cereal/cereal.hpp"
#include "cereal/access.hpp"

#include "Render/render.h"
#include "Render/textures.h"
#include "Render/points.h"
#include "Render/boxaabb.h"
#include "Utility/package.h"

#define ComponentDerivedAccessors(ComponentType) \
MV::Scene::SafeComponent<ComponentType> clone(const std::shared_ptr<MV::Scene::Node> &a_parent) { \
	return MV::Scene::SafeComponent<ComponentType>(a_parent, std::static_pointer_cast<ComponentType>(cloneImplementation(a_parent))); \
} \
MV::Scene::SafeComponent<ComponentType> safe() { \
	return MV::Scene::SafeComponent<ComponentType>(owner(), std::static_pointer_cast<ComponentType>(shared_from_this())); \
} \
std::shared_ptr<ComponentType> id(const std::string &a_id) { \
	return std::static_pointer_cast<ComponentType>(MV::Scene::Component::id(a_id)); \
}

namespace MV {

	namespace Scene {
		void appendQuadVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset);


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

			std::shared_ptr<Node> owner() const {
				return wrappedComponent->owner();
			}

			void reset() {
				wrappedComponent.reset();
				wrappedNode.reset();
			}

			Task& task() {
				return rootTask;
			}
		private:
			Task rootTask;
			std::shared_ptr<const Node> wrappedNode;
			std::shared_ptr<T> wrappedComponent;
		};

		class Component : public std::enable_shared_from_this<Component> {
			friend Node;
			friend cereal::access;

		public:
			virtual ~Component() {}

			virtual bool draw() { return true; }
			virtual void update(double a_delta){}

			BoxAABB<> bounds() {
				return boundsImplementation();
			}

			BoxAABB<int> screenBounds();

			BoxAABB<> worldBounds();

			std::shared_ptr<Node> owner() const;

			SafeComponent<Component> clone(const std::shared_ptr<Node> &a_parent) {
				return SafeComponent<Component>(a_parent, cloneImplementation(a_parent));
			}

			SafeComponent<Component> safe() {
				return SafeComponent<Component>(owner(), shared_from_this());
			}

			std::string id() const {
				return componentId;
			}

			std::shared_ptr<Component> id(const std::string &a_id) {
				componentId = a_id;
				return shared_from_this();
			}

		protected:
			//owner death *can* occur before node death in cases where a button deletes itself.
			//In known cases where callback order can cause this to occur it's best we have an explicit query.
			bool ownerIsAlive() const;

			Component(const std::weak_ptr<Node> &a_owner);

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent);

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			void notifyParentOfBoundsChange();
			void notifyParentOfComponentChange();
			virtual BoxAABB<> boundsImplementation() {
				return BoxAABB<>();
			}

			mutable std::recursive_mutex lock;

			virtual void initialize() {} //called after creation by node

			Component(const Component& a_rhs) = delete;
			Component& operator=(const Component& a_rhs) = delete;

			virtual void onRemoved(){}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(componentId),
					CEREAL_NVP(componentOwner)
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Component> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("componentId", construct->componentId),
					cereal::make_nvp("componentOwner", construct->componentOwner)
				);
				construct->initialize();
			}

		private:
			std::string componentId;
			std::weak_ptr<Node> componentOwner;
		};

		class Node : public std::enable_shared_from_this<Node> {
			friend cereal::access;
			friend Component;

		public:
			typedef void ParentInteractionSignature(const std::shared_ptr<Node> &a_parent, const std::shared_ptr<Node> &a_child);
			typedef void BasicSignature(const std::shared_ptr<Node> &a_this);
			typedef void ComponentSignature(const std::shared_ptr<Component> &a_this);

			typedef Slot<BasicSignature>::WeakSignalType BasicWeakSignalType;
			typedef Slot<BasicSignature>::SharedSignalType BasicSharedSignalType;
			typedef Slot<BasicSignature>::SignalType BasicSignalType;
			
			typedef Slot<ParentInteractionSignature>::WeakSignalType ParentInteractionWeakSignalType;
			typedef Slot<ParentInteractionSignature>::SharedSignalType ParentInteractionSharedSignalType;
			typedef Slot<ParentInteractionSignature>::SignalType ParentInteractionSignalType;

			typedef Slot<ComponentSignature>::WeakSignalType ComponentWeakSignalType;
			typedef Slot<ComponentSignature>::SharedSignalType ComponentSharedSignalType;
			typedef Slot<ComponentSignature>::SignalType ComponentSignalType;

			class Quiet {
			public:
				Quiet(const std::shared_ptr<Node> &a_node) : 
					node(a_node) {
					node->silenceInternal();
				}

				Quiet(const Quiet && a_other) : 
					node(std::move(a_other.node)) {
				}

				~Quiet() {
					if (node) {
						node->unsilenceInternal();
					}
				}

				std::shared_ptr<Node> operator->() const{
					return node;
				}

			private:
				Quiet(const Quiet &) = delete;
				std::shared_ptr<Node> node;
			};

		private:
			friend Quiet;

			Slot<BasicSignature> onChildAddSlot;
			Slot<ParentInteractionSignature> onChildRemoveSlot;
			Slot<BasicSignature> onAddSlot;
			Slot<BasicSignature> onRemoveSlot;

			Slot<BasicSignature> onEnableSlot;
			Slot<BasicSignature> onDisableSlot;
			Slot<BasicSignature> onPauseSlot;
			Slot<BasicSignature> onResumeSlot;
			Slot<BasicSignature> onShowSlot;
			Slot<BasicSignature> onHideSlot;

			Slot<BasicSignature> onBoundsRequestSlot;

			Slot<BasicSignature> onTransformChangeSlot;
			Slot<BasicSignature> onLocalBoundsChangeSlot;
			Slot<BasicSignature> onOrderChangeSlot;
			Slot<BasicSignature> onAlphaChangeSlot;
			
			Slot<ComponentSignature> onComponentUpdateSlot;

			Slot<BasicSignature> onChangeSlot;

			Slot<BasicSignature>::SharedSignalType onParentAlphaChangeSignal;

			class ReSort {
			public:
				ReSort(const std::shared_ptr<Node> &a_self);
				~ReSort();

			private:
				std::shared_ptr<Node> self;
			};

			friend ReSort;

		public:
			~Node();

			SlotRegister<BasicSignature> onChildAdd;
			SlotRegister<ParentInteractionSignature> onChildRemove;
			SlotRegister<BasicSignature> onAdd;
			SlotRegister<BasicSignature> onRemove;

			SlotRegister<BasicSignature> onEnable;
			SlotRegister<BasicSignature> onDisable;
			SlotRegister<BasicSignature> onPause;
			SlotRegister<BasicSignature> onResume;
			SlotRegister<BasicSignature> onShow;
			SlotRegister<BasicSignature> onHide;

			SlotRegister<BasicSignature> onBoundsRequest;

			SlotRegister<BasicSignature> onTransformChange;
			SlotRegister<BasicSignature> onLocalBoundsChange;
			SlotRegister<BasicSignature> onOrderChange;
			SlotRegister<BasicSignature> onAlphaChange;

			SlotRegister<BasicSignature> onChange;

			SlotRegister<ComponentSignature> onComponentUpdate;

			void draw();
			void drawChildren();
			void update(double a_delta = 0.0f);
			void drawUpdate(double a_delta = 0.0f);

			void draw(const TransformMatrix &a_overrideParentMatrix);
			void drawChildren(const TransformMatrix &a_overrideParentMatrix);

			Quiet silence() {
				return Quiet(shared_from_this());
			}

			static std::shared_ptr<Node> make(Draw2D& a_draw2d, const std::string &a_id);
			static std::shared_ptr<Node> make(Draw2D& a_draw2d);

			std::shared_ptr<Node> make(const std::string &a_id);
			std::shared_ptr<Node> make();

			std::shared_ptr<Node> makeOrGet(const std::string &a_id);

			template<typename ComponentType>
			SafeComponent<ComponentType> attach() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(shared_from_this()));
				childComponents.push_back(newComponent);
				newComponent->initialize();
				return SafeComponent<ComponentType>(shared_from_this(), newComponent);
			}

			template<typename ComponentType, typename... Args>
			SafeComponent<ComponentType> attach(Args&&... a_arguments){
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(shared_from_this(), std::forward<Args>(a_arguments)...));
				childComponents.push_back(newComponent);
				newComponent->initialize();
				return SafeComponent<ComponentType>(shared_from_this(), newComponent);
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(bool a_exactType = true, bool a_throwIfNotFound = true) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, a_throwIfNotFound);
				if (foundComponent != childComponents.end()) {
					(*foundComponent)->onRemoved();
					childComponents.erase(foundComponent);
				}
				return shared_from_this();
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(std::shared_ptr<ComponentType> a_component) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				a_component->onRemoved();
				auto found = std::find(childComponents.begin(), childComponents.end(), a_component);
				if (found != childComponents.end()) {
					childComponents.erase(found);
				}
				return shared_from_this();
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(SafeComponent<ComponentType> a_component) {
				return detach(a_component.self());
			}

			std::shared_ptr<Node> detach(const std::string &a_componentId, bool a_throwIfNotFound = true) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto found = std::find_if(childComponents.begin(), childComponents.end(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				});
				if (found != childComponents.end()) {
					childComponents.erase(found);
				} else if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_componentId, "] not found in node [", id(), "]");
				}
				return shared_from_this();
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> component(bool a_exactType = true, bool a_throwIfNotFound = true) const {
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, a_throwIfNotFound);
				if (foundComponent != childComponents.end()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*foundComponent));
				}
				return SafeComponent<ComponentType>(std::shared_ptr<Node>(), std::shared_ptr<ComponentType>());
			}

			//stop the bool overload from stealing raw strings.
			template<typename ComponentType>
			SafeComponent<ComponentType> component(const char *a_componentId, bool a_throwIfNotFound = true) const {
				return component<ComponentType>(std::string(a_componentId), a_throwIfNotFound);
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> component(const std::string &a_componentId, bool a_throwIfNotFound = true) const {
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto found = std::find_if(childComponents.cbegin(), childComponents.cend(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				});
				if (found != childComponents.cend()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*found));
				} else if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_componentId, "] not found in node [", id(), "]");
				}
				return SafeComponent<ComponentType>(std::shared_ptr<Node>(), std::shared_ptr<ComponentType>());
			}

			template<typename ComponentType>
			std::vector<SafeComponent<ComponentType>> components(bool exactType = true) const {
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::vector<std::shared_ptr<ComponentType>> matchingComponents;
				if (exactType) {
					for (auto&& currentComponent : childComponents) {
						if (typeid(currentComponent.get()) == typeid(ComponentType)) {
							matchingComponents.push_back(SafeComponent<ComponentType>(shared_from_this(), std::static_pointer_cast<ComponentType>(currentComponent)));
						}
					}
				} else {
					for (auto&& currentComponent : childComponents) {
						auto castComponent = std::dynamic_pointer_cast<ComponentType>(currentComponent);
						if (castComponent) {
							matchingComponents.push_back(SafeComponent<ComponentType>(shared_from_this(), castComponent));
						}
					}
				}
				return matchingComponents;
			}

			template<typename ComponentType>
			std::vector<SafeComponent<ComponentType>> componentsInChildren(bool includeComponentsInThis = false, bool exactType = true) const {
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::vector<SafeComponent<ComponentType>> matchingComponents;
				componentsInChildrenInternal(exactType, matchingComponents, includeComponentsInThis);
				return matchingComponents;
			}

			std::shared_ptr<Node> add(const std::shared_ptr<Node> &a_child, bool a_overrideSortDepth = true);

			Draw2D& renderer() const{
				return draw2d;
			}

			std::shared_ptr<Node> parent(const std::shared_ptr<Node> &a_parent);

			std::shared_ptr<Node> parent() const{
				return (myParent) ? myParent->shared_from_this(): nullptr;
			}

			std::shared_ptr<Node> removeFromParent();

			std::shared_ptr<Node> remove(const std::string &a_id, bool a_throw = true);
			std::shared_ptr<Node> remove(const std::shared_ptr<Node> &a_child, bool a_throw = true);

			std::shared_ptr<Node> clear();

			std::shared_ptr<Node> root();

			std::shared_ptr<Node> get(const std::string &a_id, bool a_throw = true);

			std::shared_ptr<Node> operator[](size_t a_index) const{
				require<RangeException>(a_index < childNodes.size(), "Failed to get node at index: [", a_index, "] from parent node: [", nodeId, "]");
				return childNodes[a_index];
			}

			size_t size() const{
				return childNodes.size();
			}

			bool empty() const {
				return childNodes.empty();
			}

			std::vector<std::shared_ptr<Node>>::iterator begin(){
				return childNodes.begin();
			}

			std::vector<std::shared_ptr<Node>>::iterator end(){
				return childNodes.end();
			}

			std::vector<std::shared_ptr<Node>>::const_iterator cbegin() const{
				return childNodes.cbegin();
			}

			std::vector<std::shared_ptr<Node>>::const_iterator cend() const{
				return childNodes.cend();
			}

			std::string id() const{
				return nodeId;
			}

			std::shared_ptr<Node> id(const std::string a_id);

			std::shared_ptr<Node> normalizeDepth();

			PointPrecision depth() const {
				return sortDepth;
			}

			std::shared_ptr<Node> depth(PointPrecision a_newDepth);

			Point<> position() const{
				return translateTo;
			}
			Point<> worldPosition(){
				return worldFromLocal(Point<>());
			}
			Point<int> screenPosition(){
				return screenFromLocal(Point<>());
			}

			std::shared_ptr<Node> position(const Point<> &a_newPosition);
			std::shared_ptr<Node> translate(const Point<> &a_newPosition);
			std::shared_ptr<Node> worldPosition(const Point<> &a_newPosition);
			std::shared_ptr<Node> screenPosition(const Point<int> &a_newPosition);
			std::shared_ptr<Node> nodePosition(const std::shared_ptr<Node> &a_newPosition);

			AxisAngles rotation() const{
				return rotateTo;
			}
			std::shared_ptr<Node> rotation(const AxisAngles &a_newRotation);
			std::shared_ptr<Node> addRotation(const AxisAngles &a_incrementRotation){
				return rotation(rotateTo + a_incrementRotation);
			}

			Scale scale() const{
				return scaleTo;
			}
			Scale worldScale() const;
			std::shared_ptr<Node> scale(const Scale &a_newScale);
			std::shared_ptr<Node> addScale(const Scale &a_incrementScale){
				return scale(scaleTo + a_incrementScale);
			}

			std::shared_ptr<Node> show();
			std::shared_ptr<Node> hide();

			bool visible() const {
				return allowDraw;
			}

			std::shared_ptr<Node> pause();
			std::shared_ptr<Node> resume();

			bool updating() const {
				return allowUpdate;
			}

			std::shared_ptr<Node> disable();
			std::shared_ptr<Node> enable();

			bool active() const {
				return allowDraw && allowUpdate;
			}

			PointPrecision alpha() const{
				return nodeAlpha;
			}
			std::shared_ptr<Node> alpha(PointPrecision a_alpha);

			PointPrecision worldAlpha() const{
				return parentAccumulatedAlpha;
			}

			TransformMatrix localTransform(){
				recalculateMatrix();
				return localMatrixTransform;
			}

			TransformMatrix worldTransform(){
				if (usingTemporaryMatrix) {
					return temporaryWorldMatrixTransform;
				} else {
					recalculateMatrix();
					return worldMatrixTransform;
				}
			}

			size_t indexOf(const std::shared_ptr<const Node> &a_childItem) const;
			size_t myIndex() const;

			std::vector<size_t> parentIndexList(size_t a_globalPriority = 0, size_t a_modifyLastPriority = 0);

			BoxAABB<> bounds(bool a_includeChildren = true);

			BoxAABB<> worldBounds(bool a_includeChildren = true){
				return worldFromLocal(bounds(a_includeChildren));
			}

			BoxAABB<int> screenBounds(bool a_includeChildren = true){
				return screenFromLocal(bounds(a_includeChildren));
			}

			Point<> worldFromLocal(const Point<> &a_local);
			Point<int> screenFromLocal(const Point<> &a_local);
			Point<> localFromScreen(const Point<int> &a_screen);
			Point<> localFromWorld(const Point<> &a_world);

			std::vector<Point<>> worldFromLocal(std::vector<Point<>> a_local);
			std::vector<Point<int>> screenFromLocal(const std::vector<Point<>> &a_local);
			std::vector<Point<>> localFromWorld(std::vector<Point<>> a_world);
			std::vector<Point<>> localFromScreen(const std::vector<Point<int>> &a_screen);

			BoxAABB<> worldFromLocal(const BoxAABB<>& a_local){
				return BoxAABB<>(draw2d.worldFromLocal(a_local.minPoint, worldTransform()), draw2d.worldFromLocal(a_local.maxPoint, worldTransform()));
			}
			BoxAABB<int> screenFromLocal(const BoxAABB<>& a_local){
				return BoxAABB<int>(draw2d.screenFromLocal(a_local.minPoint, worldTransform()), draw2d.screenFromLocal(a_local.maxPoint, worldTransform()));
			}
			BoxAABB<> localFromScreen(const BoxAABB<int> &a_screen){
				return BoxAABB<>(draw2d.localFromScreen(a_screen.minPoint, worldTransform()), draw2d.localFromScreen(a_screen.maxPoint, worldTransform()));
			}
			BoxAABB<> localFromWorld(const BoxAABB<> &a_world){
				return BoxAABB<>(draw2d.localFromWorld(a_world.minPoint, worldTransform()), draw2d.localFromWorld(a_world.maxPoint, worldTransform()));
			}

			bool operator<(const Node &a_rhs) {
				return depth() < a_rhs.depth();
			}

			bool operator>(const Node &a_rhs) {
				return depth() > a_rhs.depth();
			}

			std::shared_ptr<Node> serializable(bool a_serializable);

			bool serializable() const;

			std::shared_ptr<Node> clone(const std::shared_ptr<Node> &a_parent = nullptr);

			Task& task() {
				return rootTask;
			}
		private:
			Task rootTask;

			void silenceInternal() {
				onChildAddSlot.block();
				onChildRemoveSlot.block();
				onAddSlot.block();
				onRemoveSlot.block();

				onEnableSlot.block();
				onDisableSlot.block();
				onPauseSlot.block();
				onResumeSlot.block();
				onShowSlot.block();
				onHideSlot.block();

				onBoundsRequestSlot.block();

				onTransformChangeSlot.block();
				onLocalBoundsChangeSlot.block();
				onOrderChangeSlot.block();
				onAlphaChangeSlot.block();

				onComponentUpdateSlot.block();

				onChangeSlot.block();
			}

			void unsilenceInternal(bool a_callBatched = true, bool a_callChanged = true);

			template<typename ComponentType>
			std::vector<std::shared_ptr<Component>>::const_iterator componentIterator(bool a_exactType = true, bool a_throwIfNotFound = true) const {
				std::lock_guard<std::recursive_mutex> guard(lock);
				if (a_exactType) {
					for (auto currentComponent = childComponents.cbegin(); currentComponent != childComponents.cend(); ++currentComponent) {
						if (typeid(*(*currentComponent)) == typeid(ComponentType)) {
							return currentComponent;
						}
					}
				} else {
					for (auto currentComponent = childComponents.cbegin(); currentComponent != childComponents.cend(); ++currentComponent) {
						auto castComponent = std::dynamic_pointer_cast<ComponentType>(*currentComponent);
						if (castComponent) {
							return currentComponent;
						}
					}
				}
				if (a_throwIfNotFound) {
					std::string componentsString;
					for (auto&& currentComponent : childComponents) {
						componentsString += std::string("\n") + typeid(*currentComponent).name();
					}
					require<ResourceException>(false, "Failed to locate component [", typeid(ComponentType).name(), "] in node: [", nodeId, "]\nComponents:", componentsString);
				}
				return childComponents.cend();
			}

			template<typename ComponentType>
			void componentsInChildrenInternal(bool exactType, std::vector<SafeComponent<ComponentType>> &matchingComponents, bool includeThis) const {
				if (includeThis) {
					std::lock_guard<std::recursive_mutex> guard(lock);
					if (exactType) {
						for (auto&& currentComponent : childComponents) {
							if (typeid(currentComponent.get()) == typeid(ComponentType)) {
								matchingComponents.push_back(SafeComponent<ComponentType>(shared_from_this(), std::static_pointer_cast<ComponentType>(currentComponent)));
							}
						}
					} else {
						for (auto&& currentComponent : childComponents) {
							auto castComponent = std::dynamic_pointer_cast<ComponentType>(currentComponent);
							if (castComponent) {
								matchingComponents.push_back(SafeComponent<ComponentType>(shared_from_this(), castComponent));
							}
						}
					}
				}
				for(auto&& currentChild : childNodes) {
					currentChild->componentsInChildrenInternal(exactType, matchingComponents, true);
				}
			}

			void safeOnChange();

			Node(const Node& a_rhs) = delete;
			Node& operator=(const Node& a_rhs) = delete;

			std::string getCloneId(const std::string &original) const;
			std::shared_ptr<Node> cloneInternal(const std::shared_ptr<Node> &a_parent);

			bool allowSerialize = true;

			void markMatrixDirty(bool a_rootCall = true);

			void recalculateChildBounds();
			void recalculateLocalBounds();
			void recalculateAlpha();
			void recalculateMatrix();

			void recalculateMatrixAfterLoad();

			Node(Draw2D &a_draw2d, const std::string &a_id);

			void fixChildOwnership();
			void postLoadStep(bool a_isRootNode);

			template <class Archive>
			void serialize(Archive & archive) {
				std::vector<std::shared_ptr<Node>> filteredChildren;
				std::copy_if(childNodes.begin(), childNodes.end(), std::back_inserter(filteredChildren), [](const auto &a_child){
					return a_child->allowSerialize;
				});
				std::weak_ptr<Node> weakParent;
				if (myParent) {
					weakParent = myParent->shared_from_this();
				}
				archive(
					CEREAL_NVP(nodeId),
					cereal::make_nvp("isRootNode", myParent == nullptr),
					cereal::make_nvp("parent", weakParent),
					CEREAL_NVP(allowDraw),
					CEREAL_NVP(allowUpdate),
					CEREAL_NVP(translateTo),
					CEREAL_NVP(rotateTo),
					CEREAL_NVP(scaleTo),
					CEREAL_NVP(sortDepth),
					CEREAL_NVP(nodeAlpha),
					CEREAL_NVP(localBounds),
					CEREAL_NVP(localChildBounds),
					cereal::make_nvp("childNodes", filteredChildren),
					CEREAL_NVP(childComponents)
				);
				if (childNodes.empty() && !filteredChildren.empty()) {
					childNodes = filteredChildren;
				}
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Node> &construct) {
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				MV::require<PointerException>(renderer != nullptr, "Null renderer in Node::load_and_construct.");
				std::string nodeId;
				archive(cereal::make_nvp("nodeId", nodeId));
				construct(*renderer, nodeId);
				bool isRootNode = false;
				std::weak_ptr<Node> weakParent;
				archive(cereal::make_nvp("parent", weakParent));
				if (!weakParent.expired()) {
					construct->myParent = weakParent.lock().get();
				}
				archive(
					cereal::make_nvp("isRootNode", isRootNode),
					cereal::make_nvp("parent", weakParent),
					cereal::make_nvp("allowDraw", construct->allowDraw),
					cereal::make_nvp("allowUpdate", construct->allowUpdate),
					cereal::make_nvp("translateTo", construct->translateTo),
					cereal::make_nvp("rotateTo", construct->rotateTo),
					cereal::make_nvp("scaleTo", construct->scaleTo),
					cereal::make_nvp("sortDepth", construct->sortDepth),
					cereal::make_nvp("nodeAlpha", construct->nodeAlpha),
					cereal::make_nvp("localBounds", construct->localBounds),
					cereal::make_nvp("localChildBounds", construct->localChildBounds),
					cereal::make_nvp("childNodes", construct->childNodes),
					cereal::make_nvp("childComponents", construct->childComponents)
				);
				construct->postLoadStep(isRootNode);
			}

			mutable std::recursive_mutex lock;

			Draw2D &draw2d;

			std::vector<std::shared_ptr<Node>> childNodes;
			std::vector<std::shared_ptr<Component>> childComponents;

			std::string nodeId;

			Scale scaleTo;
			Point<> translateTo;
			AxisAngles rotateTo;

			bool localMatrixDirty = true;
			bool worldMatrixDirty = true;
			TransformMatrix localMatrixTransform;
			TransformMatrix worldMatrixTransform;

			bool usingTemporaryMatrix = false;
			TransformMatrix temporaryWorldMatrixTransform;

			BoxAABB<> localBounds;
			BoxAABB<> localChildBounds;

			PointPrecision sortDepth = 0.0f;

			Node* myParent = nullptr;

			PointPrecision nodeAlpha = 1.0f;
			mutable PointPrecision parentAccumulatedAlpha = 1.0f;
			bool allowUpdate = true;
			bool allowDraw = true;

			bool inOnChange = false;
			bool inBoundsCalculation = false;
			bool inChildBoundsCalculation = false;
		};

		std::ostream& operator<<(std::ostream& os, const std::shared_ptr<Node>& a_node);

	}
}

#endif





