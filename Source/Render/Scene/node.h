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

namespace MV {

	namespace Scene {
		void appendQuadVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset);

		class Node;
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

			std::shared_ptr<Component> remove();

		protected:
			Component(const std::weak_ptr<Node> &a_owner);

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
					CEREAL_NVP(componentOwner)
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Component> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("componentOwner", construct->componentOwner)
				);
				construct->initialize();
			}

		private:
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
		private:
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
			Slot<BasicSignature> onDepthChangeSlot;
			Slot<BasicSignature> onAlphaChangeSlot;

			Slot<ComponentSignature> onComponentUpdateSlot;

			Slot<BasicSignature>::SharedSignalType onParentTransformChangeSignal;
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
			SlotRegister<BasicSignature> onDepthChange;
			SlotRegister<BasicSignature> onAlphaChange;

			Slot<ComponentSignature> onComponentUpdate;

			void draw();
			void drawChildren();
			void update(double a_delta = 0.0f);
			void drawUpdate(double a_delta = 0.0f);

			void draw(const TransformMatrix &a_overrideParentMatrix);
			void drawChildren(const TransformMatrix &a_overrideParentMatrix);

			static std::shared_ptr<Node> make(Draw2D& a_draw2d, const std::string &a_id);
			static std::shared_ptr<Node> make(Draw2D& a_draw2d);

			std::shared_ptr<Node> make(const std::string &a_id);
			std::shared_ptr<Node> make();

			template<typename ComponentType>
			std::shared_ptr<ComponentType> attach() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(shared_from_this()));
				childComponents.push_back(newComponent);
				newComponent->initialize();
				return newComponent;
			}

			template<typename ComponentType, typename... Args>
			std::shared_ptr<ComponentType> attach(Args&&... a_arguments){
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto newComponent = std::shared_ptr<ComponentType>(new ComponentType(shared_from_this(), std::forward<Args>(a_arguments)...));
				childComponents.push_back(newComponent);
				newComponent->initialize();
				return newComponent;
			}

			template<typename ComponentType>
			std::shared_ptr<ComponentType> component(bool exactType = true, bool a_throwIfNotFound = true) const {
				std::lock_guard<std::recursive_mutex> guard(lock);
				if (exactType) {
					for (auto&& currentComponent : childComponents) {
						if (typeid(*currentComponent) == typeid(ComponentType)) {
							return std::static_pointer_cast<ComponentType>(currentComponent);
						}
					}
				} else {
					for (auto&& currentComponent : childComponents) {
						auto castComponent = std::dynamic_pointer_cast<ComponentType>(currentComponent);
						if (castComponent) {
							return castComponent;
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
				return nullptr;
			}

			template<typename ComponentType>
			std::vector<std::shared_ptr<ComponentType>> components(bool exactType = true) const{
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::vector<std::shared_ptr<ComponentType>> matchingComponents;
				if (exactType) {
					for (auto&& currentComponent : childComponents) {
						if (typeid(currentComponent.get()) == typeid(ComponentType)) {
							matchingComponents.push_back(std::static_pointer_cast<ComponentType>(currentComponent));
						}
					}
				} else {
					for (auto&& currentComponent : childComponents) {
						auto castComponent = std::dynamic_pointer_cast<ComponentType>(currentComponent);
						if (castComponent) {
							matchingComponents.push_back(castComponent);
						}
					}
				}
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
			Point<> worldPosition() const{
				return worldFromLocal(translateTo);
			}
			Point<int> screenPosition() const{
				return screenFromLocal(translateTo);
			}

			std::shared_ptr<Node> position(const Point<> &a_newPosition);
			std::shared_ptr<Node> translate(const Point<> &a_newPosition) {
				return position(position() + a_newPosition);
			}
			std::shared_ptr<Node> worldPosition(const Point<> &a_newPosition){
				return position(localFromWorld(a_newPosition));
			}
			std::shared_ptr<Node> screenPosition(const Point<int> &a_newPosition){
				return position(localFromScreen(a_newPosition));
			}
			std::shared_ptr<Node> nodePosition(const std::shared_ptr<Node> &a_newPosition){
				return worldPosition(a_newPosition->worldPosition());
			}

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

			TransformMatrix localTransform() const{
				return localMatrixTransform;
			}

			TransformMatrix worldTransform() const{
				if (usingTemporaryMatrix) {
					return temporaryWorldMatrixTransform;
				} else {
					return worldMatrixTransform;
				}
			}

			size_t indexOf(const std::shared_ptr<const Node> &a_childItem) const;
			size_t myIndex() const;

			std::vector<size_t> parentIndexList(size_t a_globalPriority = 0);

			BoxAABB<> bounds(bool a_includeChildren = true);

			BoxAABB<> worldBounds(bool a_includeChildren = true){
				return worldFromLocal(bounds(a_includeChildren));
			}

			BoxAABB<int> screenBounds(bool a_includeChildren = true){
				return screenFromLocal(bounds(a_includeChildren));
			}

			Point<> worldFromLocal(const Point<> &a_local) const{
				return draw2d.worldFromLocal(a_local, worldTransform());
			}
			Point<int> screenFromLocal(const Point<> &a_local) const{
				return draw2d.screenFromLocal(a_local, worldTransform());
			}
			Point<> localFromScreen(const Point<int> &a_screen) const{
				return draw2d.localFromScreen(a_screen, worldTransform());
			}
			Point<> Node::localFromWorld(const Point<> &a_world) const{
				return draw2d.localFromWorld(a_world, worldTransform());
			}

			std::vector<Point<>> Node::worldFromLocal(std::vector<Point<>> a_local) const{
				for(Point<>& point : a_local){
					point = draw2d.worldFromLocal(point, worldTransform());
				}
				return a_local;
			}

			std::vector<Point<int>> Node::screenFromLocal(const std::vector<Point<>> &a_local) const{
				std::vector<Point<int>> processed;
				for(const Point<>& point : a_local){
					processed.push_back(draw2d.screenFromLocal(point, worldTransform()));
				}
				return processed;
			}

			std::vector<Point<>> Node::localFromWorld(std::vector<Point<>> a_world) const{
				for(Point<>& point : a_world){
					point = draw2d.localFromWorld(point, worldTransform());
				}
				return a_world;
			}

			std::vector<Point<>> Node::localFromScreen(const std::vector<Point<int>> &a_screen) const{
				std::vector<Point<>> processed;
				for(const Point<int>& point : a_screen){
					processed.push_back(draw2d.localFromScreen(point, worldTransform()));
				}
				return processed;
			}

			BoxAABB<> Node::worldFromLocal(const BoxAABB<>& a_local) const{
				return BoxAABB<>(draw2d.worldFromLocal(a_local.minPoint, worldTransform()), draw2d.worldFromLocal(a_local.maxPoint, worldTransform()));
			}
			BoxAABB<int> Node::screenFromLocal(const BoxAABB<>& a_local) const{
				return BoxAABB<int>(draw2d.screenFromLocal(a_local.minPoint, worldTransform()), draw2d.screenFromLocal(a_local.maxPoint, worldTransform()));
			}
			BoxAABB<> Node::localFromScreen(const BoxAABB<int> &a_screen) const{
				return BoxAABB<>(draw2d.localFromScreen(a_screen.minPoint, worldTransform()), draw2d.localFromScreen(a_screen.maxPoint, worldTransform()));
			}
			BoxAABB<> Node::localFromWorld(const BoxAABB<> &a_world) const{
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
		private:
			Node(const Node& a_rhs) = delete;
			Node& operator=(const Node& a_rhs) = delete;

			bool allowSerialize = true;

			void recalculateChildBounds();
			void recalculateLocalBounds();
			void recalculateAlpha();
			void recalculateMatrix();

			Node(Draw2D &a_draw2d, const std::string &a_id);

			void postLoadStep(bool a_isRootNode);

			template <class Archive>
			void serialize(Archive & archive) {
				//if (allowSerialize) {
					archive(
						CEREAL_NVP(nodeId),
						cereal::make_nvp("isRootNode", myParent == nullptr),
						CEREAL_NVP(allowDraw),
						CEREAL_NVP(allowUpdate),
						CEREAL_NVP(translateTo),
						CEREAL_NVP(rotateTo),
						CEREAL_NVP(scaleTo),
						CEREAL_NVP(sortDepth),
						CEREAL_NVP(nodeAlpha),
						CEREAL_NVP(childNodes),
						CEREAL_NVP(childComponents)
					);
				//}
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
				archive(
					cereal::make_nvp("isRootNode", isRootNode),
					cereal::make_nvp("allowDraw", construct->allowDraw),
					cereal::make_nvp("allowUpdate", construct->allowUpdate),
					cereal::make_nvp("translateTo", construct->translateTo),
					cereal::make_nvp("rotateTo", construct->rotateTo),
					cereal::make_nvp("scaleTo", construct->scaleTo),
					cereal::make_nvp("sortDepth", construct->sortDepth),
					cereal::make_nvp("nodeAlpha", construct->nodeAlpha),
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

			TransformMatrix localMatrixTransform;
			TransformMatrix worldMatrixTransform;

			bool usingTemporaryMatrix = false;
			TransformMatrix temporaryWorldMatrixTransform;

			BoxAABB<> localBounds;
			BoxAABB<> localChildBounds;

			PointPrecision sortDepth = 0.0f;

			Node* myParent = nullptr;

			PointPrecision nodeAlpha = 1.0f;
			PointPrecision parentAccumulatedAlpha = 1.0f;
			bool allowUpdate = true;
			bool allowDraw = true;

			bool inBoundsCalculation = false;
		};

	}
}

#endif





