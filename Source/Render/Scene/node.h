#ifndef _MV_SCENE_NODE_H_
#define _MV_SCENE_NODE_H_

#include "component.h"

namespace MV {

	namespace Scene {

		class Node : public std::enable_shared_from_this<Node> {
			friend cereal::access;
			friend Component;

		public:
			typedef void ParentInteractionSignature(const std::shared_ptr<Node> &a_parent, const std::shared_ptr<Node> &a_child);
			typedef void BasicSignature(const std::shared_ptr<Node> &a_this);
			typedef void ComponentSignature(const std::shared_ptr<Component> &a_this);

			typedef Signal<BasicSignature>::WeakRecieverType BasicWeakSignalType;
			typedef Signal<BasicSignature>::SharedRecieverType BasicSharedSignalType;
			typedef Signal<BasicSignature>::RecieverType BasicSignalType;
			
			typedef Signal<ParentInteractionSignature>::WeakRecieverType ParentInteractionWeakSignalType;
			typedef Signal<ParentInteractionSignature>::SharedRecieverType ParentInteractionSharedSignalType;
			typedef Signal<ParentInteractionSignature>::RecieverType ParentInteractionSignalType;

			typedef Signal<ComponentSignature>::WeakRecieverType ComponentWeakSignalType;
			typedef Signal<ComponentSignature>::SharedRecieverType ComponentSharedSignalType;
			typedef Signal<ComponentSignature>::RecieverType ComponentSignalType;

			class Quiet {
			public:
				Quiet(const std::shared_ptr<Node> &a_node) :
					node(a_node){
					node->silenceInternal();
				}

				Quiet(const Quiet && a_other) : 
					node(std::move(a_other.node)) {
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

			Signal<BasicSignature> onChildAddSignal;
			Signal<ParentInteractionSignature> onChildRemoveSignal;
			Signal<ParentInteractionSignature> onChildOrderChangeSignal;
			Signal<BasicSignature> onAddSignal;
			Signal<BasicSignature> onRemoveSignal;

			Signal<BasicSignature> onEnableSignal;
			Signal<BasicSignature> onDisableSignal;
			Signal<BasicSignature> onPauseSignal;
			Signal<BasicSignature> onResumeSignal;
			Signal<BasicSignature> onShowSignal;
			Signal<BasicSignature> onHideSignal;

			Signal<BasicSignature> onBoundsRequestSignal;

			Signal<BasicSignature> onTransformChangeSignal;
			Signal<BasicSignature> onLocalBoundsChangeSignal;
			Signal<BasicSignature> onChildBoundsChangeSignal;
			Signal<BasicSignature> onOrderChangeSignal;
			Signal<BasicSignature> onAlphaChangeSignal;
			
			Signal<ComponentSignature> onAttachSignal;
			Signal<ComponentSignature> onDetachSignal;
			Signal<ComponentSignature> onComponentUpdateSignal;

			Signal<BasicSignature> onChangeSignal;

			Signal<BasicSignature>::SharedRecieverType onParentAlphaChangeSignal;

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

			SignalRegister<BasicSignature> onChildAdd;
			SignalRegister<ParentInteractionSignature> onChildRemove;
			SignalRegister<ParentInteractionSignature> onChildOrderChange;
			SignalRegister<BasicSignature> onAdd;
			SignalRegister<BasicSignature> onRemove;

			SignalRegister<BasicSignature> onEnable;
			SignalRegister<BasicSignature> onDisable;
			SignalRegister<BasicSignature> onPause;
			SignalRegister<BasicSignature> onResume;
			SignalRegister<BasicSignature> onShow;
			SignalRegister<BasicSignature> onHide;

			SignalRegister<BasicSignature> onBoundsRequest;

			SignalRegister<BasicSignature> onTransformChange;
			SignalRegister<BasicSignature> onLocalBoundsChange;
			SignalRegister<BasicSignature> onChildBoundsChange;
			SignalRegister<BasicSignature> onOrderChange;
			SignalRegister<BasicSignature> onAlphaChange;

			SignalRegister<BasicSignature> onChange;

			SignalRegister<ComponentSignature> onDetach;
			SignalRegister<ComponentSignature> onAttach;
			SignalRegister<ComponentSignature> onComponentUpdate;

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
			static std::shared_ptr<Node> load(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> loadBinary(const std::string &a_filename, const std::function<void(cereal::PortableBinaryInputArchive &)> a_binder, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> load(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, const std::string &a_overrideId, bool a_doPostLoadStep = true);
			static std::shared_ptr<Node> loadBinary(const std::string &a_filename, const std::function<void(cereal::PortableBinaryInputArchive &)> a_binder, const std::string &a_overrideId, bool a_doPostLoadStep = true);

			std::shared_ptr<Node> save(const std::string &a_filename, bool a_renameNodeToFile = true);
			std::shared_ptr<Node> save(const std::string &a_filename, const std::string &a_overrideId);

			std::shared_ptr<Node> saveBinary(const std::string &a_filename, bool a_renameNodeToFile = true);
			std::shared_ptr<Node> saveBinary(const std::string &a_filename, const std::string &a_overrideId);

			std::shared_ptr<Node> make(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, const std::string &a_overrideId = "");
			std::shared_ptr<Node> loadChild(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, const std::string &a_overrideId = "");
			
			std::shared_ptr<Node> loadChildBinary(const std::string &a_filename, const std::function<void(cereal::PortableBinaryInputArchive &)> a_binder, const std::string &a_overrideId = "");

			std::shared_ptr<Node> make(const std::string &a_id);
			std::shared_ptr<Node> make();

			std::shared_ptr<Node> makeOrGet(const std::string &a_id);

			template<typename ComponentType>
			SafeComponent<ComponentType> attach(std::shared_ptr<ComponentType> a_component) {
				auto self = shared_from_this();
				a_component->detach();
				childComponents.push_back(a_component);
				a_component->reattached(self);
				onAttachSignal(a_component);
				return SafeComponent<ComponentType>(self, a_component);
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> attach() {
				auto self = shared_from_this();
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(self));
				childComponents.push_back(newComponent);
				newComponent->initialize();
				onAttachSignal(newComponent);
				return SafeComponent<ComponentType>(self, newComponent);
			}

			template<typename ComponentType, typename... Args>
			SafeComponent<ComponentType> attach(Args&&... a_arguments){
				auto self = shared_from_this();
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(self, std::forward<Args>(a_arguments)...));
				childComponents.push_back(newComponent);
				newComponent->initialize();
				onAttachSignal(newComponent);
				return SafeComponent<ComponentType>(self, newComponent);
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(bool a_exactType = true, bool a_throwIfNotFound = true) {
				auto self = shared_from_this();
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, a_throwIfNotFound);
				if (foundComponent != childComponents.end()) {
					(*foundComponent)->detachImplementation();
					auto sharedComponent = (*foundComponent);
					childComponents.erase(foundComponent);
					onDetachSignal(sharedComponent);
					(*foundComponent)->componentOwner.reset();
				}
				return self;
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(std::shared_ptr<ComponentType> a_component) {
				auto self = shared_from_this();
				auto found = std::find(childComponents.begin(), childComponents.end(), a_component);
				if (found != childComponents.end()) {
					(*found)->detachImplementation();
					childComponents.erase(found);
					onDetachSignal(a_component);
					(*found)->componentOwner.reset();
				}
				return self;
			}

			template<typename ComponentType>
			std::shared_ptr<Node> detach(SafeComponent<ComponentType> a_component) {
				return detach(a_component.self());
			}

			std::shared_ptr<Node> detach(const std::string &a_componentId, bool a_throwIfNotFound = true) {
				auto self = shared_from_this();
				auto found = std::find_if(childComponents.begin(), childComponents.end(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				});
				if (found != childComponents.end()) {
					(*found)->detachImplementation();
					auto foundComponent = *found;
					childComponents.erase(found);
					onDetachSignal(foundComponent);
					(*found)->componentOwner.reset();
				} else if (a_throwIfNotFound) {
					require<ResourceException>(false, "Component with id [", a_componentId, "] not found in node [", id(), "]");
				}
				return self;
			}

			template<typename ComponentType>
			SafeComponent<ComponentType> component(bool a_exactType = true, bool a_throwIfNotFound = true) const {
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = componentIterator<ComponentType>(a_exactType, a_throwIfNotFound);
				if (foundComponent != childComponents.end()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*foundComponent));
				}
				return SafeComponent<ComponentType>(nullptr, nullptr);
			}

			template<typename ... ComponentType>
			std::vector<MV::Variant<SafeComponent<ComponentType>...>> componentsInParents(bool a_exactType = true, bool a_includeSelf = true) const {
				typedef std::vector<MV::Variant<SafeComponent<ComponentType>...>> ResultType;
				ResultType results;
				if (a_includeSelf) {
					results = components<ComponentType...>(a_exactType);
				}

				if (myParent) {
					auto parentResults = myParent->componentsInParents<ComponentType...>(a_exactType, true);
					moveAppend(results, parentResults);
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
					if (foundComponent != childComponents.end()) {
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
				std::vector<std::shared_ptr<Component>>::const_iterator foundComponent = a_includeSelf ? componentIterator<ComponentType>(a_exactType, false) : childComponents.end();
				if (foundComponent != childComponents.end()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*foundComponent));
				} else if (!childNodes.empty()) {
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
				auto found = std::find_if(childComponents.cbegin(), childComponents.cend(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				});
				if (found != childComponents.cend()) {
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
				auto found = a_includeSelf ? std::find_if(childComponents.cbegin(), childComponents.cend(), [&](const std::shared_ptr<Component> &a_component) {
					return a_component->id() == a_componentId;
				}) : childComponents.end();
				if (found != childComponents.cend()) {
					return SafeComponent<ComponentType>(shared_from_this(), std::dynamic_pointer_cast<ComponentType>(*found));
				} else if (!childNodes.empty()) {
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
			std::vector<MV::Variant<SafeComponent<ComponentType>...>> components(bool exactType = true) const {
				std::vector<MV::Variant<SafeComponent<ComponentType>...>> results;
				if (exactType) {
					for (auto&& item : childComponents) {
						castAndAddIfExact<Variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
					}
				} else {
					for (auto&& item : childComponents) {
						castAndAddIfDerived<Variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
					}
				}
				return results;
			}

			template<typename ... ComponentType>
			std::vector<MV::Variant<SafeComponent<ComponentType>...>> componentsInChildren(bool exactType = true, bool includeComponentsInThis = true) const {
				std::vector<MV::Variant<SafeComponent<ComponentType>...>> results;
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
			std::shared_ptr<Node> nodePosition(const std::shared_ptr<Node> &a_newPosition);

			AxisAngles worldRotation() const;
			std::shared_ptr<Node> worldRotation(const AxisAngles &a_newAngle);
			AxisAngles worldRotationRad() const;
			std::shared_ptr<Node> worldRotationRad(const AxisAngles &a_newAngle);
			AxisAngles rotation() const {
				return toDegrees(rotateTo);
			}
			AxisAngles rotationRad() const{
				return rotateTo;
			}
			std::shared_ptr<Node> rotation(const AxisAngles &a_newRotation);
			std::shared_ptr<Node> rotationRad(const AxisAngles &a_newRotation);
			std::shared_ptr<Node> addRotation(const AxisAngles &a_incrementRotation) {
				return rotationRad(rotateTo + toRadians(a_incrementRotation));
			}
			std::shared_ptr<Node> addRotationRad(const AxisAngles &a_incrementRotation) {
				return rotationRad(rotateTo + a_incrementRotation);
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

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

			//use this to suppress messages when you know what the fuck you're doing.
			void quietLocalAndChildMatrixFix();
			void quietLocalMatrixFix();

			//loud recalculations if you know what you're doing.
			void recalculateChildBounds();
			void recalculateLocalBounds();
			void recalculateAlpha();
			void recalculateMatrix();

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

			template<typename ... ComponentType>
			void componentsInChildrenInternal(bool exactType, bool includeComponentsInThis, std::vector<MV::Variant<SafeComponent<ComponentType>...>>& results) const {
				if (includeComponentsInThis) {
					if (exactType) {
						for (auto&& item : childComponents) {
							castAndAddIfExact<Variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
						}
					} else {
						for (auto&& item : childComponents) {
							castAndAddIfDerived<Variant<SafeComponent<ComponentType>...>, ComponentType...>(item, results);
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

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const version) {
				std::vector<std::shared_ptr<Node>> filteredChildren;
				std::copy_if(childNodes.begin(), childNodes.end(), std::back_inserter(filteredChildren), [](const auto &a_child){
					return a_child->allowSerialize;
				});
				std::weak_ptr<Node> weakParent;
				std::vector<std::shared_ptr<Component>> filteredChildComponents;
				std::copy_if(childComponents.begin(), childComponents.end(), std::back_inserter(filteredChildComponents), [](const auto &a_child) {
					return a_child->allowSerialize;
				});

				if (myParent) {
					weakParent = myParent->shared_from_this();
				}
				if (dirtyChildBounds) {
					recalculateChildBounds();
				}
				if (dirtyLocalBounds) {
					recalculateLocalBounds();
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
					CEREAL_NVP(localChildBounds));
				if (version > 0) {
					archive(cereal::make_nvp("cameraId", ourCameraId));
				}
				archive(
					cereal::make_nvp("childNodes", filteredChildren),
					cereal::make_nvp("childComponents", filteredChildComponents)
				);
				if (childNodes.empty() && !filteredChildren.empty()) {
					childNodes = filteredChildren;
				}
				if (childComponents.empty() && !filteredChildComponents.empty()) {
					childComponents = filteredChildComponents;
				}
				if (version < 2) {
					rotateTo = toRadians(rotateTo);
				}
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Node> &construct, std::uint32_t const version) {
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				bool doPostLoad = true;
				archive.extract(cereal::make_nvp("postLoad", doPostLoad));
				MV::require<MV::PointerException>(renderer != nullptr, "Null renderer in Node::load_and_construct.");
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
					cereal::make_nvp("allowDraw", construct->allowDraw),
					cereal::make_nvp("allowUpdate", construct->allowUpdate),
					cereal::make_nvp("translateTo", construct->translateTo),
					cereal::make_nvp("rotateTo", construct->rotateTo),
					cereal::make_nvp("scaleTo", construct->scaleTo),
					cereal::make_nvp("sortDepth", construct->sortDepth),
					cereal::make_nvp("nodeAlpha", construct->nodeAlpha),
					cereal::make_nvp("localBounds", construct->localBounds),
					cereal::make_nvp("localChildBounds", construct->localChildBounds));
				if (version > 0) {
					archive(cereal::make_nvp("cameraId", construct->ourCameraId));
				}
				archive(
					cereal::make_nvp("childNodes", construct->childNodes),
					cereal::make_nvp("childComponents", construct->childComponents)
				);
				if (version < 2) {
					toRadiansInPlace(construct->rotateTo);
				}
				if (doPostLoad && isRootNode) {
					construct->postLoadStep();
				}
			}

			Draw2D &draw2d;

			std::vector<std::shared_ptr<Node>> childNodes;
			std::vector<std::shared_ptr<Component>> childComponents;

			std::string nodeId;

			bool onChangeCallNeeded = false;
			bool allowChangeCallNeeded = true;

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

			bool dirtyLocalBounds = false;
			bool dirtyChildBounds = false;

			int32_t ourCameraId = 0;
		};

		std::ostream& operator<<(std::ostream& os, const std::shared_ptr<Node>& a_node);
	}
}

#endif





