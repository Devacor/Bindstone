#ifndef _MV_SCENE_NODE_H_
#define _MV_SCENE_NODE_H_

#include "component.h"

namespace MV {

	namespace Scene {

		class Node : public jai::property_owner<Node>, public std::enable_shared_from_this<Node> {
			friend cereal::access;
			friend class jai::serialization::access;
			friend jai::access;
			friend Component;

			struct LoadOptions {
				LoadOptions(MV::Services &a_services, bool a_doPostLoad) : doPostLoad(a_doPostLoad), services(a_services) {
					services.connect(this);
				}
				~LoadOptions() {
					services.disconnect<LoadOptions>();
				}

				// Depth tracking for load_complete: incremented when load_and_construct
				// enters a node, decremented in post_load. The node whose decrement
				// brings depth to 0 is the root — it fires load_complete() if present.
				void enterLoad() { ++depth; }
				bool exitLoad() { return --depth == 0; }

				bool doPostLoad;
			private:
				MV::Services &services;
				int depth = 0;
				LoadOptions(const LoadOptions &) = delete;
				LoadOptions& operator=(const LoadOptions &) = delete;
			};

		public:
			typedef void ParentInteractionSignature(const std::shared_ptr<Node> &a_parent, const std::shared_ptr<Node> &a_child);
			typedef void BasicSignature(const std::shared_ptr<Node> &a_this);
			typedef void ComponentSignature(const std::shared_ptr<Component> &a_this);

			typedef jai::signal<BasicSignature>::shared_receiver_type BasicReceiverType;
			typedef jai::signal<ParentInteractionSignature>::shared_receiver_type ParentInteractionReceiverType;
			typedef jai::signal<ComponentSignature>::shared_receiver_type ComponentReceiverType;

			class Quiet {
			public:
				Quiet(const std::shared_ptr<Node> &a_node) :
					node(a_node){
					node->silenceInternal();
				}

				Quiet(Quiet && a_other) noexcept : 
					node(std::move(a_other.node)),
					callBatchedAfterUnsilence(a_other.callBatchedAfterUnsilence){
				}

				Quiet& forget() {
					callBatchedAfterUnsilence = false;
					return *this;
				}

				~Quiet() {
					if (node) {
						node->unsilenceInternal(callBatchedAfterUnsilence, callBatchedAfterUnsilence);
					}
				}

				std::shared_ptr<Node> operator->() const{
					return node;
				}

			private:
				Quiet(const Quiet &) = delete;
				std::shared_ptr<Node> node;
				bool callBatchedAfterUnsilence = true;
			};

		private:
			friend Quiet;

			jai::signal_emitter<BasicSignature> onChildAddSignal;
			jai::signal_emitter<ParentInteractionSignature> onChildRemoveSignal;
			jai::signal_emitter<ParentInteractionSignature> onChildOrderChangeSignal;
			jai::signal_emitter<BasicSignature> onAddSignal;
			jai::signal_emitter<BasicSignature> onRemoveSignal;

			jai::signal_emitter<BasicSignature> onEnableSignal;
			jai::signal_emitter<BasicSignature> onDisableSignal;
			jai::signal_emitter<BasicSignature> onPauseSignal;
			jai::signal_emitter<BasicSignature> onResumeSignal;
			jai::signal_emitter<BasicSignature> onShowSignal;
			jai::signal_emitter<BasicSignature> onHideSignal;

			jai::signal_emitter<BasicSignature> onBoundsRequestSignal;

			jai::signal_emitter<BasicSignature> onTransformChangeSignal;
			jai::signal_emitter<BasicSignature> onLocalBoundsChangeSignal;
			jai::signal_emitter<BasicSignature> onChildBoundsChangeSignal;
			jai::signal_emitter<BasicSignature> onOrderChangeSignal;
			jai::signal_emitter<BasicSignature> onAlphaChangeSignal;

			jai::signal_emitter<ComponentSignature> onAttachSignal;
			jai::signal_emitter<ComponentSignature> onDetachSignal;
			jai::signal_emitter<ComponentSignature> onComponentUpdateSignal;

			jai::signal_emitter<BasicSignature> onChangeSignal;

			jai::signal_emitter<BasicSignature> onMatrixDirtySignal;

			BasicReceiverType onParentAlphaChangeSignal;

			class ReSort {
			public:
				ReSort(const std::shared_ptr<Node> &a_self);
				~ReSort();

				//returns true if the insert changed its position.
				bool insert();
			private:
				std::shared_ptr<Node> self;
				int64_t originalIndex = -1;
				bool inserted = false;
			};

			friend ReSort;

		public:
			static int64_t recalculateLocalBoundsCalls;
			static int64_t recalculateChildBoundsCalls;
			static int64_t recalculateMatrixCalls;

			~Node();

			jai::signal<BasicSignature> onChildAdd;
			jai::signal<ParentInteractionSignature> onChildRemove;
			jai::signal<ParentInteractionSignature> onChildOrderChange;
			jai::signal<BasicSignature> onAdd;
			jai::signal<BasicSignature> onRemove;

			jai::signal<BasicSignature> onEnable;
			jai::signal<BasicSignature> onDisable;
			jai::signal<BasicSignature> onPause;
			jai::signal<BasicSignature> onResume;
			jai::signal<BasicSignature> onShow;
			jai::signal<BasicSignature> onHide;

			jai::signal<BasicSignature> onBoundsRequest;

			jai::signal<BasicSignature> onTransformChange;
			jai::signal<BasicSignature> onLocalBoundsChange;
			jai::signal<BasicSignature> onChildBoundsChange;
			jai::signal<BasicSignature> onOrderChange;
			jai::signal<BasicSignature> onAlphaChange;

			jai::signal<BasicSignature> onChange;

			jai::signal<ComponentSignature> onDetach;
			jai::signal<ComponentSignature> onAttach;
			jai::signal<ComponentSignature> onComponentUpdate;

			jai::signal<BasicSignature> onMatrixDirty;

			void draw();
			void drawChildren();
			void update(double a_delta = 0.0f, bool a_force = false);
			void drawUpdate(double a_delta = 0.0f);

			void draw(const TransformMatrix &a_overrideParentMatrix);
			void drawChildren(const TransformMatrix &a_overrideParentMatrix);

			std::string getUniqueId(const std::string &original) const;

			Quiet silence() {
				return Quiet(shared_from_this());
			}

			static std::shared_ptr<Node> make(Draw2D& a_draw2d, const std::string &a_id);
			static std::shared_ptr<Node> make(Draw2D& a_draw2d);
			static std::shared_ptr<Node> load(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> loadBinary(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> load(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> loadBinary(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId, bool a_doPostLoadStep = true);

			// Cereal-based load (matches save() format)
			static std::shared_ptr<Node> loadCereal(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> loadCereal(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId, bool a_doPostLoadStep = true);

			std::shared_ptr<Node> save(const std::string &a_filename, bool a_renameNodeToFile = true);
			std::shared_ptr<Node> save(const std::string &a_filename, const std::string &a_overrideId);

			std::shared_ptr<Node> saveBinary(const std::string &a_filename, MV::Services& a_services, bool a_renameNodeToFile = true);
			std::shared_ptr<Node> saveBinary(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId);

			// JaiScript serialization
			std::shared_ptr<Node> saveJai(const std::string &a_filename, MV::Services& a_services, bool a_renameNodeToFile = true);
			std::shared_ptr<Node> saveJai(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId);
			static std::shared_ptr<Node> loadJai(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> loadJai(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId, bool a_doPostLoadStep = true);
			std::shared_ptr<Node> loadChildJai(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId = "");

			std::shared_ptr<Node> make(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId = "");
			std::shared_ptr<Node> loadChild(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId = "");
			
			std::shared_ptr<Node> loadChildBinary(const std::string &a_filename, MV::Services& a_services, const std::string &a_overrideId = "");

			std::shared_ptr<Node> make(const std::string &a_id);
			std::shared_ptr<Node> make();

			std::shared_ptr<Node> makeOrGet(const std::string &a_id);

			template<typename ComponentType>
			SafeComponent<ComponentType> attach(std::shared_ptr<ComponentType> a_component) {
				auto self = shared_from_this();
				a_component->detach();
				childComponents->push_back(a_component);
				a_component->reattached(self);
				onAttachSignal(a_component);
				return SafeComponent<ComponentType>(self, a_component);
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> attach() {
				auto self = shared_from_this();
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(self));
				childComponents->push_back(newComponent);
				newComponent->initialize();
				onAttachSignal(newComponent);
				return SafeComponent<ComponentType>(self, newComponent);
			}

			template<typename ComponentType, typename... Args>
			SafeComponent<ComponentType> attach(Args&&... a_arguments){
				auto self = shared_from_this();
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(self, std::forward<Args>(a_arguments)...));
				childComponents->push_back(newComponent);
				newComponent->initialize();
				onAttachSignal(newComponent);
				return SafeComponent<ComponentType>(self, newComponent);
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(bool a_exactType = true, bool a_throwIfNotFound = true) {
				auto self = shared_from_this();
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, a_throwIfNotFound);
				if (foundComponent != childComponents->end()) {
					auto sharedComponent = (*foundComponent);
					auto originalOwner = sharedComponent->componentOwner;
					sharedComponent->detachImplementation();
					childComponents->erase(foundComponent);
					onDetachSignal(sharedComponent);
					if (sharedComponent->componentOwner.lock() == originalOwner.lock()) {
						sharedComponent->componentOwner.reset();
					}
				}
				return self;
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(std::shared_ptr<ComponentType> a_component) {
				auto self = shared_from_this();
				auto found = std::find(childComponents->begin(), childComponents->end(), a_component);
				if (found != childComponents->end()) {
					auto sharedComponent = *found;
					auto originalOwner = sharedComponent->componentOwner.get();
					sharedComponent->detachImplementation();
					childComponents->erase(found);
					onDetachSignal(a_component);
					if (sharedComponent->componentOwner->lock() == originalOwner.lock()) {
						sharedComponent->componentOwner->reset();
					}
				}
				return self;
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(SafeComponent<ComponentType> a_component) {
				return detach(a_component.self());
			}

			std::shared_ptr<Node> detach(const std::string &a_componentId, bool a_throwIfNotFound = true) {
				auto self = shared_from_this();
				auto found = std::find_if(childComponents->begin(), childComponents->end(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				});
				if (found != childComponents->end()) {
					auto sharedComponent = *found;
					auto originalOwner = sharedComponent->componentOwner.get();
					sharedComponent->detachImplementation();
					childComponents->erase(found);
					onDetachSignal(sharedComponent);
					if (sharedComponent->componentOwner->lock() == originalOwner.lock()) {
						sharedComponent->componentOwner->reset();
					}
				} else if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_componentId, "] not found in node [", id(), "]");
				}
				return self;
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> component(bool a_exactType = true, bool a_throwIfNotFound = true) const {
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, a_throwIfNotFound);
				if (foundComponent != childComponents->end()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*foundComponent));
				}
				return SafeComponent<ComponentType>(nullptr, nullptr);
			}

			template<typename ... ComponentType>
			std::vector<std::variant<SafeComponent<ComponentType>...>> componentsInParents(bool a_exactType = true, bool a_includeSelf = true) const {
				typedef std::vector<std::variant<SafeComponent<ComponentType>...>> ResultType;
				ResultType results;
				if (a_includeSelf) {
					results = components<ComponentType...>(a_exactType);
				}

				if (myParent) {
					auto parentResults = myParent->componentsInParents<ComponentType...>(a_exactType, true);
					results.insert(results.end(), parentResults.begin(), parentResults.end());
				}
				return results;
			}

			//stop the bool overload from stealing raw strings.
			template<typename ComponentType = Component>
			SafeComponent<ComponentType> componentInParents(const char *a_componentId, bool a_throwIfNotFound = true, bool a_includeSelf = true) const {
				return componentInParents<ComponentType>(std::string(a_componentId), a_throwIfNotFound, a_includeSelf);
			}

			template<typename ComponentType = Component>
			SafeComponent<ComponentType> componentInParents(const std::string& a_id, bool a_throwIfNotFound = true, bool a_includeSelf = true) const {
				if (a_includeSelf) {
					auto foundComponent = component<ComponentType>(a_id, false);
					if (foundComponent) {
						return foundComponent;
					}
				}

				if (myParent) {
					auto parentComponent = myParent->componentInParents<ComponentType>(a_id, false, true);
					if (parentComponent) {
						return parentComponent;
					}
				}

				if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_id, "] not found in node (or parents of node) [", id(), "]");
				}
				return SafeComponent<ComponentType>(nullptr, nullptr);
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> componentInParents(bool a_exactType = true, bool a_throwIfNotFound = true, bool a_includeSelf = true) const {
				if (a_includeSelf) {
					std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, false);
					if (foundComponent != childComponents->end()) {
						return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*foundComponent));
					}
				}

				if (myParent) {
					auto parentComponent = myParent->componentInParents<ComponentType>(a_exactType, false, true);
					if (parentComponent) {
						return parentComponent;
					}
				}
				
				if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with type [", typeid(ComponentType).name(), "] not found in node (or parents of node) [", id(), "]");
				}
				return SafeComponent<ComponentType>(nullptr, nullptr);
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> componentInChildren(bool a_exactType = true, bool a_throwIfNotFound = true, bool a_includeSelf = true) const {
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = a_includeSelf ? componentIterator<ComponentType>(a_exactType, false) : childComponents->end();
				if (foundComponent != childComponents->end()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*foundComponent));
				} else if (!childNodes->empty()) {
					for (auto&& childNode : childNodes) {
						auto childComponent = childNode->componentInChildren<ComponentType>(a_exactType, false, true);
						if (childComponent) {
							return childComponent;
						}
					}
				}
				
				if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with type [", typeid(ComponentType).name(), "] not found in node (or children of node) [", id(), "]");
				}

				return SafeComponent<ComponentType>(nullptr, nullptr);
			}

			//stop the bool overload from stealing raw strings.
			template<typename ComponentType = Component>
			SafeComponent<ComponentType> component(const char *a_componentId, bool a_throwIfNotFound = true) const {
				return component<ComponentType>(std::string(a_componentId), a_throwIfNotFound);
			}

			template<typename ComponentType = Component>
			SafeComponent<ComponentType> component(const std::string &a_componentId, bool a_throwIfNotFound = true) const {
				auto found = std::find_if(childComponents->cbegin(), childComponents->cend(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				});
				if (found != childComponents->cend()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*found));
				} else if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_componentId, "] not found in node [", id(), "]");
				}
				return SafeComponent<ComponentType>(nullptr, nullptr);
			}
			
			//stop the bool overload from stealing raw strings.
			template<typename ComponentType = Component>
			SafeComponent<ComponentType> componentInChildren(const char *a_componentId, bool a_throwIfNotFound = true, bool a_includeSelf = true) const {
				return componentInChildren<ComponentType>(std::string(a_componentId), a_throwIfNotFound, a_includeSelf);
			}

			template<typename ComponentType = Component>
			SafeComponent<ComponentType> componentInChildren(const std::string &a_componentId, bool a_throwIfNotFound = true, bool a_includeSelf = true) const {
				auto found = a_includeSelf ? std::find_if(childComponents->cbegin(), childComponents->cend(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				}) : childComponents->end();
				if (found != childComponents->cend()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*found));
				} else if (!childNodes->empty()) {
					for (auto&& childNode : childNodes) {
						auto childComponent = componentInChildren<ComponentType>(a_componentId, false);
						if (childComponent) {
							return childComponent;
						}
					}
				}
				
				if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_componentId, "] not found in node [", id(), "]");
				} 
				return SafeComponent<ComponentType>(nullptr, nullptr);
			}

			template<typename ... ComponentType>
			std::vector<std::variant<SafeComponent<ComponentType>...>> components(bool exactType = true) const {
				std::vector<std::variant<SafeComponent<ComponentType>...>> results;
				if (exactType) {
					for (auto&& item : childComponents) {
						castAndAddIfExact<std::variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
					}
				} else {
					for (auto&& item : childComponents) {
						castAndAddIfDerived<std::variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
					}
				}
				return results;
			}

			template<typename ... ComponentType>
			std::vector<std::variant<SafeComponent<ComponentType>...>> componentsInChildren(bool exactType = true, bool includeComponentsInThis = true) const {
				std::vector<std::variant<SafeComponent<ComponentType>...>> results;
				componentsInChildrenInternal<ComponentType...>(exactType, includeComponentsInThis, results);
				return results;
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

			bool hasParent(const std::string &a_id) const;
			std::shared_ptr<Node> getParent(const std::string &a_id, bool a_throw = true);

			std::shared_ptr<Node> get(const std::string &a_id, bool a_throw = true);
			std::shared_ptr<Node> getImmediate(const std::string &a_id, bool a_throw = true);
			bool has(const std::string &a_id) const;
			bool hasImmediate(const std::string &a_id) const;

			std::shared_ptr<Node> operator[](size_t a_index) const{
				require<RangeException>(a_index < childNodes->size(), "Failed to get node at index: [", a_index, "] from parent node: [", nodeId.get(), "]");
				return childNodes[a_index];
			}

			size_t size() const{
				return childNodes->size();
			}

			bool empty() const {
				return childNodes->empty();
			}

			std::vector<std::shared_ptr<Node>>::iterator begin(){
				return childNodes->begin();
			}

			std::vector<std::shared_ptr<Node>>::iterator end(){
				return childNodes->end();
			}

			std::vector<std::shared_ptr<Node>>::const_iterator cbegin() const{
				return childNodes->cbegin();
			}

			std::vector<std::shared_ptr<Node>>::const_iterator cend() const{
				return childNodes->cend();
			}

			std::string id() const{
				return nodeId;
			}

			std::shared_ptr<Node> id(const std::string &a_id);

			std::shared_ptr<Node> normalizeDepth();

			TransformMatrix& camera() {
				return renderer().camera(ourCameraId);
			}

			int32_t cameraId() const {
				return ourCameraId;
			}

			std::shared_ptr<Node> cameraId(int32_t a_newCameraId) {
				cameraIdInternal(a_newCameraId);
				return shared_from_this();
			}

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
			std::shared_ptr<Node> worldTranslate(const Point<>& a_newPosition);
			std::shared_ptr<Node> screenTranslate(const Point<int>& a_newPosition);

			AxisAngles worldRotation() const;
			std::shared_ptr<Node> worldRotation(const AxisAngles &a_newAngle);
			AxisAngles worldRotationRad() const;
			std::shared_ptr<Node> worldRotationRad(const AxisAngles &a_newAngle);
			AxisAngles rotation() const {
				return toDegrees(rotateTo.get());
			}
			AxisAngles rotationRad() const{
				return rotateTo;
			}
			std::shared_ptr<Node> rotation(const AxisAngles &a_newRotation);
			std::shared_ptr<Node> rotationRad(const AxisAngles &a_newRotation);
			std::shared_ptr<Node> addRotation(const AxisAngles &a_incrementRotation) {
				return rotationRad(rotateTo.get() + toRadians(a_incrementRotation));
			}
			std::shared_ptr<Node> addRotationRad(const AxisAngles &a_incrementRotation) {
				return rotationRad(rotateTo.get() + a_incrementRotation);
			}

			Scale scale() const{
				return scaleTo;
			}
			Scale worldScale() const;
			std::shared_ptr<Node> worldScale(const Scale &a_newScale);
			std::shared_ptr<Node> scale(const Scale &a_newScale);
			std::shared_ptr<Node> addScale(const Scale &a_incrementScale){
				return scale(scaleTo + a_incrementScale);
			}

			std::shared_ptr<Node> show();
			std::shared_ptr<Node> hide();

			std::shared_ptr<Node> visible(bool a_isVisible);
			bool visible() const {
				return allowDraw && (myParent ? myParent->visible() : true);
			}

			bool selfVisible() const {
				return allowDraw;
			}

			std::shared_ptr<Node> pause();
			std::shared_ptr<Node> resume();

			std::shared_ptr<Node> updating(bool a_isUpdating);
			bool updating() const {
				return allowUpdate && (myParent ? myParent->updating() : true);
			}

			bool selfUpdating() const {
				return allowUpdate;
			}

			std::shared_ptr<Node> disable();
			std::shared_ptr<Node> enable();

			std::shared_ptr<Node> active(bool a_isActive);
			bool active() const {
				return allowDraw && allowUpdate && (myParent ? myParent->active() : true);
			}

			bool selfActive() const {
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

			std::vector<int64_t> parentIndexList(int64_t a_globalPriority = 0);
			std::vector<std::shared_ptr<MV::Scene::Node>> parents();

			BoxAABB<> bounds(bool a_includeChildren = true); //our node's local bounds + optionally included childBounds;
			BoxAABB<> childBounds(); //only child bounds

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
				return BoxAABB<>(draw2d.worldFromLocal(a_local.minPoint, ourCameraId, worldTransform()), draw2d.worldFromLocal(a_local.maxPoint, ourCameraId, worldTransform()));
			}
			BoxAABB<int> screenFromLocal(const BoxAABB<>& a_local){
				return BoxAABB<int>(draw2d.screenFromLocal(a_local.minPoint, ourCameraId, worldTransform()), draw2d.screenFromLocal(a_local.maxPoint, ourCameraId, worldTransform()));
			}
			BoxAABB<> localFromScreen(const BoxAABB<int> &a_screen){
				return BoxAABB<>(draw2d.localFromScreen(a_screen.minPoint, ourCameraId, worldTransform()), draw2d.localFromScreen(a_screen.maxPoint, ourCameraId, worldTransform()));
			}
			BoxAABB<> localFromWorld(const BoxAABB<> &a_world){
				return BoxAABB<>(draw2d.localFromWorld(a_world.minPoint, ourCameraId, worldTransform()), draw2d.localFromWorld(a_world.maxPoint, ourCameraId, worldTransform()));
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
				if (!rootTask) {
					rootTask = std::make_unique<Task>();
				}
				return *rootTask;
			}

			//use this to suppress messages when you know what the fuck you're doing.
			void quietLocalAndChildMatrixFix();
			void quietLocalMatrixFix();

			//loud recalculations if you know what you're doing.
			void recalculateChildBounds();
			void recalculateLocalBounds();
			void recalculateAlpha();
			void recalculateMatrix();
			void recalculateLocalMatrix();   // local TRS only; used by the override-parent draw path

			//manual post load only if you know what you're doing.
			void postLoadStep();

		private:
			std::unique_ptr<Task> rootTask;

			void cameraIdInternal(int32_t a_newCameraId) {
				ourCameraId = a_newCameraId;
				for (auto&& child : childNodes) {
					child->cameraIdInternal(a_newCameraId);
				}
			}

			template<typename ContainerObjectType>
			void castAndAddIfExact(const std::shared_ptr<Component> &base, std::vector<ContainerObjectType> &container) const {
				//base case do nothing
			}

			template<typename ContainerObjectType, typename T>
			void castAndAddIfExact(const std::shared_ptr<Component> &base, std::vector<ContainerObjectType> &container) const {
				if (typeid(*base) == typeid(T)) {
					container.push_back(SafeComponent<T>(shared_from_this(), std::static_pointer_cast<T>(base)));
				}
			}

			template<typename ContainerObjectType, typename T, typename T2, typename ...V>
			void castAndAddIfExact(const std::shared_ptr<Component> &base, std::vector<ContainerObjectType> &container) const {
				if (typeid(*base) == typeid(T)) {
					container.push_back(SafeComponent<T>(shared_from_this(), std::static_pointer_cast<T>(base)));
				} else {
					castAndAddIfExact<ContainerObjectType, T2, V...>(base, container);
				}
			}

			template<typename ContainerObjectType>
			void castAndAddIfDerived(const std::shared_ptr<Component> &base, std::vector<ContainerObjectType> &container) const {
				//base case do nothing
			}

			template<typename ContainerObjectType, typename T>
			void castAndAddIfDerived(const std::shared_ptr<Component> &base, std::vector<ContainerObjectType> &container) const {
				auto result = std::dynamic_pointer_cast<T>(base);
				if (result) {
					container.push_back(SafeComponent<T>(shared_from_this(), result));
				}
			}

			template<typename ContainerObjectType, typename T, typename T2, typename ...V>
			void castAndAddIfDerived(const std::shared_ptr<Component> &base, std::vector<ContainerObjectType> &container) const {
				auto result = std::dynamic_pointer_cast<T>(base);
				if (result) {
					container.push_back(SafeComponent<T>(shared_from_this(), result));
				} else {
					castAndAddIfExact<ContainerObjectType, T2, V...>(base, container);
				}
			}

			void markBoundsDirty() {
				dirtyLocalBounds = true;
				markParentBoundsDirty();
				onLocalBoundsChangeSignal(shared_from_this());
			}

			void markParentBoundsDirty();

			void silenceInternal() {
				onChildAddSignal.block();
				onChildRemoveSignal.block();
				onChildOrderChangeSignal.block();
				onAddSignal.block();
				onRemoveSignal.block();

				onEnableSignal.block();
				onDisableSignal.block();
				onPauseSignal.block();
				onResumeSignal.block();
				onShowSignal.block();
				onHideSignal.block();

				onBoundsRequestSignal.block();

				onTransformChangeSignal.block();
				onLocalBoundsChangeSignal.block();
				onChildBoundsChangeSignal.block();
				onOrderChangeSignal.block();
				onAlphaChangeSignal.block();

				onComponentUpdateSignal.block();

				onChangeSignal.block();
			}

			void unsilenceInternal(bool a_callBatched = true, bool a_callChanged = true);

			template<typename ComponentType>
			std::vector<std::shared_ptr<Component>>::const_iterator componentIterator(bool a_exactType = true, bool a_throwIfNotFound = true) const {
				if (a_exactType) {
					for (auto currentComponent = childComponents->cbegin(); currentComponent != childComponents->cend(); ++currentComponent) {
						if (typeid(*(*currentComponent)) == typeid(ComponentType)) {
							return currentComponent;
						}
					}
				} else {
					for (auto currentComponent = childComponents->cbegin(); currentComponent != childComponents->cend(); ++currentComponent) {
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
					require<ResourceException>(false, "Failed to locate component [", typeid(ComponentType).name(), "] in node: [", nodeId.get(), "]\nComponents:", componentsString);
				}
				return childComponents->cend();
			}

			template<typename ... ComponentType>
			void componentsInChildrenInternal(bool exactType, bool includeComponentsInThis, std::vector<std::variant<SafeComponent<ComponentType>...>>& results) const {
				if (includeComponentsInThis) {
					if (exactType) {
						for (auto&& item : childComponents) {
							castAndAddIfExact<std::variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
						}
					} else {
						for (auto&& item : childComponents) {
							castAndAddIfDerived<std::variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
						}
					}
				}
				for (auto&& child : childNodes) {
					child->componentsInChildrenInternal<ComponentType...>(exactType, true, results);
				}
			}

			void safeOnChange();

			Node(const Node& a_rhs) = delete;
			Node& operator=(const Node& a_rhs) = delete;

			std::shared_ptr<Node> cloneInternal(const std::shared_ptr<Node> &a_parent);

			bool allowSerialize = true;

			void markMatrixDirty(bool a_rootCall = true);

			void recalculateMatrixAfterLoad();
			void localAndChildPostLoadInitializeComponents();

			Node(Draw2D &a_draw2d, const std::string &a_id);

			void fixChildOwnership();

			// Serialization for both JaiScript and Cereal
			// Uses if constexpr to dispatch to the appropriate format
			// Note: Uses save/load_and_construct pattern because Node requires constructor args
			template<class Archive>
			void save(Archive & archive, std::uint32_t const version) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					// JaiScript path - use automatic property serialization
					const_cast<Node*>(this)->property_mgr.save(archive);
				} else {
					// Filter children and components that are serializable
					std::vector<std::shared_ptr<Node>> filteredChildren;
					std::copy_if(childNodes.get().begin(), childNodes.get().end(), std::back_inserter(filteredChildren), [](const auto &a_child){
						return a_child->allowSerialize;
					});
					std::vector<std::shared_ptr<Component>> filteredChildComponents;
					std::copy_if(childComponents.get().begin(), childComponents.get().end(), std::back_inserter(filteredChildComponents), [](const auto &a_child) {
						return a_child->allowSerialize;
					});
					std::weak_ptr<Node> weakParent;
					if (myParent) {
						weakParent = myParent->shared_from_this();
					}
					// Cereal path - ensure bounds are up to date before saving
					if (dirtyChildBounds) {
						const_cast<Node*>(this)->recalculateChildBounds();
					}
					if (dirtyLocalBounds) {
						const_cast<Node*>(this)->recalculateLocalBounds();
					}

					archive(
						cereal::make_nvp("nodeId", nodeId.get()),
						cereal::make_nvp("isRootNode", myParent == nullptr),
						cereal::make_nvp("parent", weakParent),
						cereal::make_nvp("allowDraw", allowDraw.get()),
						cereal::make_nvp("allowUpdate", allowUpdate.get()),
						cereal::make_nvp("translateTo", translateTo.get()),
						cereal::make_nvp("rotateTo", rotateTo.get()),
						cereal::make_nvp("scaleTo", scaleTo.get()),
						cereal::make_nvp("sortDepth", sortDepth.get()),
						cereal::make_nvp("nodeAlpha", nodeAlpha.get()),
						cereal::make_nvp("localBounds", localBounds.get()),
						cereal::make_nvp("localChildBounds", localChildBounds.get()));
					if (version > 0) {
						archive(cereal::make_nvp("cameraId", ourCameraId.get()));
					}
					archive(
						cereal::make_nvp("childNodes", filteredChildren),
						cereal::make_nvp("childComponents", filteredChildComponents)
					);
				}
			}

			template<class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Node> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* renderer = services.get<MV::Draw2D>();
				LoadOptions* options = services.get<LoadOptions>(false);
				bool doPostLoad = options ? options->doPostLoad : true;
				std::string nodeId;
				archive(cereal::make_nvp("nodeId", nodeId));
				construct(*renderer, nodeId);
				bool isRootNode = false;
				std::weak_ptr<Node> weakParent;
				archive(
					cereal::make_nvp("isRootNode", isRootNode),
					cereal::make_nvp("parent", weakParent));

				if (auto lockedParent = weakParent.lock()) {
					construct->myParent = lockedParent.get();
				}
				archive(
					cereal::make_nvp("allowDraw", construct->allowDraw.get()),
					cereal::make_nvp("allowUpdate", construct->allowUpdate.get()),
					cereal::make_nvp("translateTo", construct->translateTo.get()),
					cereal::make_nvp("rotateTo", construct->rotateTo.get()),
					cereal::make_nvp("scaleTo", construct->scaleTo.get()),
					cereal::make_nvp("sortDepth", construct->sortDepth.get()),
					cereal::make_nvp("nodeAlpha", construct->nodeAlpha.get()),
					cereal::make_nvp("localBounds", construct->localBounds.get()),
					cereal::make_nvp("localChildBounds", construct->localChildBounds.get()));
				if (version > 0) {
					archive(cereal::make_nvp("cameraId", construct->ourCameraId.get()));
				}
				archive(
					cereal::make_nvp("childNodes", construct->childNodes.get()),
					cereal::make_nvp("childComponents", construct->childComponents.get())
				);
				if (version < 2) {
					toRadiansInPlace(construct->rotateTo.get());
				}
				if (doPostLoad && isRootNode) {
					construct->postLoadStep();
				}
			}

			// Pass 2: per-object reconnection/finalization. Fires on every deserialized
			// node after its own properties are loaded. Good for: asset ID → texture
			// handle lookup, weak-ptr backref relinking, per-node cache rebuilding.
			// Unsilences the node so signals work again after the load suppression.
			void post_load(jai::serialization::any_archive_reader& ar) {
				unsilenceInternal(false, false);  // restore signals; no batched callbacks

				// If this node is the root (depth returns to 0), fire load_complete.
				MV::Services* services = ar.get_user_context<MV::Services>();
				if (services) {
					auto* options = services->get<LoadOptions>(false);
					if (options && options->exitLoad() && options->doPostLoad) {
						load_complete();
					}
				}
			}

			// Pass 3: tree-wide structural finalization, fires once on the root node
			// after the entire tree is assembled and all post_load hooks have run.
			// Handles work that requires the full parent-child graph: matrix recalculation,
			// alpha propagation, component initialization.
			void load_complete() {
				postLoadStep();
			}

			// JaiScript version of load_and_construct (templated for CRTP archives)
			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Node>& construct) {
				MV::Services* services = ar.template get_user_context<MV::Services>();
				auto* renderer = services->get<MV::Draw2D>();

				// Track nesting depth so post_load can identify the root node.
				auto* options = services->get<LoadOptions>(false);
				if (options) { options->enterLoad(); }

				std::string nodeId;
				ar.serialize("nodeId", nodeId);
				construct(*renderer, nodeId);

				// Silence all signals before restoring properties. Without this,
				// every property setter fires markBoundsDirty() → full parent-chain
				// propagation — O(N²) recalculations across the tree. post_load
				// unsilences once all properties are restored.
				construct->silenceInternal();

				construct->load_with_hook(ar);
			}

			Draw2D &draw2d;

			// Serializable properties (JAI_PROPERTY)
			JAI_PROPERTY((std::vector<std::shared_ptr<Node>>), childNodes);
			JAI_PROPERTY((std::vector<std::shared_ptr<Component>>), childComponents);
			JAI_PROPERTY((std::string), nodeId);
			JAI_PROPERTY((Scale), scaleTo);
			JAI_PROPERTY((Point<>), translateTo);
			JAI_PROPERTY((AxisAngles), rotateTo);
			JAI_TRANSIENT_PROPERTY((BoxAABB<>), localBounds);       // derived: recomputed from component geometry
			JAI_TRANSIENT_PROPERTY((BoxAABB<>), localChildBounds);  // derived: recomputed from child nodes
			JAI_PROPERTY((PointPrecision), sortDepth, 0.0f);
			JAI_PROPERTY((PointPrecision), nodeAlpha, 1.0f);
			JAI_PROPERTY((bool), allowUpdate, true);
			JAI_PROPERTY((bool), allowDraw, true);
			JAI_PROPERTY((int32_t), ourCameraId, 0);

			// Non-serializable runtime state
			bool onChangeCallNeeded = false;
			bool allowChangeCallNeeded = true;

			bool localMatrixDirty = true;
			bool worldMatrixDirty = true;
			TransformMatrix localMatrixTransform;
			TransformMatrix worldMatrixTransform;

			bool usingTemporaryMatrix = false;
			TransformMatrix temporaryWorldMatrixTransform;

			Node* myParent = nullptr;

			mutable PointPrecision parentAccumulatedAlpha = 1.0f;

			bool inOnChange = false;

			bool dirtyLocalBounds = false;
			bool dirtyChildBounds = false;
		};

		std::ostream& operator<<(std::ostream& os, const std::shared_ptr<Node>& a_node);
	}
}

// Force Cereal to use non-polymorphic serialization for shared_ptr<Node> and weak_ptr<Node>
// Node is technically polymorphic (virtual destructor from property_owner), but
// existing scene files were saved using the non-polymorphic ptr_wrapper format.
// These overloads must be defined AFTER cereal/types/polymorphic.hpp is included
// (which happens via component.h -> textures.h -> serialize.h) and AFTER Node is defined.
namespace cereal {
	template<class Archive>
	void save(Archive& ar, const std::shared_ptr<MV::Scene::Node>& ptr) {
		ar(make_nvp("ptr_wrapper", memory_detail::make_ptr_wrapper(ptr)));
	}

	template<class Archive>
	void load(Archive& ar, std::shared_ptr<MV::Scene::Node>& ptr) {
		ar(make_nvp("ptr_wrapper", memory_detail::make_ptr_wrapper(ptr)));
	}

	template<class Archive>
	void save(Archive& ar, const std::weak_ptr<MV::Scene::Node>& ptr) {
		auto const sptr = ptr.lock();
		ar(make_nvp("ptr_wrapper", memory_detail::make_ptr_wrapper(sptr)));
	}

	template<class Archive>
	void load(Archive& ar, std::weak_ptr<MV::Scene::Node>& ptr) {
		std::shared_ptr<MV::Scene::Node> sptr;
		ar(make_nvp("ptr_wrapper", memory_detail::make_ptr_wrapper(sptr)));
		ptr = sptr;
	}
}

#endif





