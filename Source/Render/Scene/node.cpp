#include "node.h"
#include "stddef.h"
#include <numeric>
#include <regex>
#include "chaiscript/chaiscript.hpp"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Component);

namespace MV {
	namespace Scene {
		void appendQuadVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset) {
			std::vector<GLuint> quadIndices{
				a_pointOffset, a_pointOffset + 1,
				a_pointOffset + 2, a_pointOffset + 2,
				a_pointOffset + 3, a_pointOffset
			};
			a_indices.insert(a_indices.end(), quadIndices.begin(), quadIndices.end());
		}

		Node::ReSort::ReSort(const std::shared_ptr<Node> &a_self):
			self(a_self) {

			if(self->myParent){
				auto foundSelf = std::find(self->myParent->childNodes.begin(), self->myParent->childNodes.end(), self);
				require<PointerException>(foundSelf != self->myParent->childNodes.end(), "Failed to re-sort: [", self->id(), "]");
				self->myParent->childNodes.erase(foundSelf);
			}
		}

		Node::ReSort::~ReSort() {
			if(self->myParent){
				insertSorted(self->myParent->childNodes, self);
			}
		}


		void Component::notifyParentOfBoundsChange() {
			try{
				owner()->recalculateLocalBounds();
			} catch (PointerException &) {
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

		Component::Component(const std::weak_ptr<Node> &a_owner):
			componentOwner(a_owner) {
		}

		BoxAABB<int> Component::screenBounds() {
			return owner()->screenFromLocal(boundsImplementation());
		}

		BoxAABB<> Component::worldBounds() {
			return owner()->worldFromLocal(boundsImplementation());
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

		Node::~Node() {
			for (auto &&component : childComponents) {
				component->onOwnerDestroyed();
			}

			for(auto &&child : childNodes){
				child->myParent = nullptr;
			}
		}

		void Node::draw() {
			if (allowDraw) {
				bool allowChildrenToDraw = true;
				auto stableComponentList = childComponents;
				for (auto&& component : stableComponentList) {
					allowChildrenToDraw = component->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					drawChildren();
				}
			}
		}

		void Node::drawChildren() {
			if (allowDraw) {
				auto stableChildNodes = childNodes;
				for (auto&& child : stableChildNodes) {
					child->draw();
				}
			}
		}

		void Node::draw(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				SCOPE_EXIT{ usingTemporaryMatrix = false; worldMatrixDirty = true; localMatrixDirty = true; };
				usingTemporaryMatrix = true;
				temporaryWorldMatrixTransform = a_overrideParentMatrix;
				temporaryWorldMatrixTransform *= localTransform();
				bool allowChildrenToDraw = true;
				auto stableComponentList = childComponents;
				for (auto&& component : stableComponentList) {
					allowChildrenToDraw = component->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					drawChildren(worldTransform());
				}
			}
		}

		void Node::drawChildren(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				auto stableChildNodes = childNodes;
				for (auto&& child : stableChildNodes) {
					child->draw(a_overrideParentMatrix);
				}
			}
		}

		void Node::update(double a_delta) {
			if (onChangeCallNeeded) {
				onChangeCallNeeded = false;
				onChangeSignal(shared_from_this());
			}
			if (allowUpdate) {
				auto stableComponentList = childComponents;
				for (auto&& component : stableComponentList) {
					component->update(a_delta);
				}
				auto stableChildNodes = childNodes;
				for (auto&& child : stableChildNodes) {
					child->update(a_delta);
				}
				rootTask.update(a_delta);
			}
		}

		void Node::drawUpdate(double a_delta) {
			auto selfReference = shared_from_this(); //keep us alive no matter the update step
			if (allowDraw && allowUpdate) {
				bool allowChildrenToDraw = true;
				auto stableComponentList = childComponents;
				for (auto&& component : stableComponentList) {
					component->updateImplementation(a_delta);
					allowChildrenToDraw = component->draw() && allowChildrenToDraw;
				}
				auto stableChildNodes = childNodes;
				if (allowChildrenToDraw) {
					for (auto&& child : stableChildNodes) {
						child->drawUpdate(a_delta);
					}
				} else {
					for (auto&& child : stableChildNodes) {
						child->update(a_delta);
					}
				}
				rootTask.update(a_delta);
			} else if (allowDraw) {
				selfReference->draw();
			} else if (allowUpdate) {
				selfReference->update(a_delta);
			}
		}

		std::shared_ptr<Node> Node::make(Draw2D& a_draw2d, const std::string &a_id) {
			return std::shared_ptr<Node>(new Node(a_draw2d, a_id));
		}

		std::shared_ptr<Node> Node::make(Draw2D& a_draw2d) {
			return make(a_draw2d, guid("root_"));
		}

		std::shared_ptr<Node> Node::make(const std::string &a_id) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto toAdd = Node::make(draw2d, a_id);
			remove(a_id, false);
			add(toAdd);
			return toAdd;
		}

		std::shared_ptr<Node> Node::make() {
			return make(MV::guid("id_"));
		}

		std::shared_ptr<Node> Node::add(const std::shared_ptr<Node> &a_child, bool a_overrideSortDepth) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			Node* parentCheck = this;
			while (parentCheck = parentCheck->myParent) {
				MV::require<RangeException>(parentCheck != a_child.get(), "Adding [", a_child->id(), "] to [", id(), "] would result in a circular ownership!");
			}
			if(a_child->myParent != this){
				a_child->removeFromParent();
				if(a_overrideSortDepth){
					a_child->sortDepth = (childNodes.empty()) ? 0.0f : childNodes[childNodes.size() - 1]->sortDepth + 1.0f;
				}
				insertSorted(childNodes, a_child);
				a_child->myParent = this;
				a_child->onAddSignal(a_child);
				onChildAddSignal(a_child);
			}
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::removeFromParent() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			if(myParent){
				myParent->remove(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::remove(const std::string &a_id, bool a_throw) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_child){
				return a_child->id() == a_id;
			});
			if(foundNode != childNodes.end()){
				auto self = shared_from_this();
				auto child = *foundNode;
				childNodes.erase(foundNode);
				child->onRemoveSignal(child);
				onChildRemoveSignal(self, child);
				return child;
			}
			require<ResourceException>(!a_throw, "Failed to remove: [", a_id, "] from parent node: [", nodeId, "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::remove(const std::shared_ptr<Node> &a_child, bool a_throw) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_ourChild){
				return a_ourChild == a_child;
			});
			if(foundNode != childNodes.end()){
				auto self = shared_from_this();
				auto child = *foundNode;
				childNodes.erase(foundNode);
				child->onRemoveSignal(child);
				onChildRemoveSignal(self, child);
				return child;
			}
			require<ResourceException>(!a_throw, "Failed to remove: [", a_child->id(), "] from parent node: [", nodeId, "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::clear() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			while(!childNodes.empty()){
				auto childToRemove = *childNodes.begin();
				childNodes.erase(childNodes.begin());
				childToRemove->onRemoveSignal(childToRemove);
				onChildRemoveSignal(self, childToRemove);
			}
			return self;
		}

		std::shared_ptr<Node> Node::root() {
			Node* current = this;
			while(current->myParent != nullptr){
				current = current->myParent;
			}
			return current->shared_from_this();
		}

		std::shared_ptr<Node> Node::get(const std::string &a_id, bool a_throw) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_child){
				return a_child->id() == a_id;
			});
			if(foundNode != childNodes.end()){
				return *foundNode;
			}
			for(auto&& child : childNodes){
				auto found = child->get(a_id, false);
				if(found != nullptr){
					return found;
				}
			}
			require<ResourceException>(!a_throw, "Failed to get: [", a_id, "] from parent node: [", nodeId, "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::id(const std::string &a_id) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			if (nodeId != a_id) {
				{
					ReSort sort(self);
					nodeId = a_id;
				}
				onOrderChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::depth(PointPrecision a_newDepth) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			if (sortDepth != a_newDepth) {
				{
					ReSort sort(self);
					sortDepth = a_newDepth;
				}
				onOrderChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::normalizeDepth() {
			PointPrecision currentDepth = 0.0f;
			for (auto&& childNode : *this) {
				childNode->sortDepth = currentDepth++;
			}
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::position(const Point<> &a_newPosition) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			translateTo = a_newPosition;
			auto self = shared_from_this();
			onTransformChangeSignal(self);
			return self;
		}

		std::shared_ptr<Node> Node::rotation(const AxisAngles &a_newRotation) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			rotateTo = a_newRotation;
			auto self = shared_from_this();
			onTransformChangeSignal(self);
			return self;
		}

		std::shared_ptr<Node> Node::scale(const Scale &a_newScale) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			scaleTo = a_newScale;
			auto self = shared_from_this();
			onTransformChangeSignal(self);
			return self;
		}

		std::shared_ptr<Node> Node::alpha(PointPrecision a_alpha) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			nodeAlpha = a_alpha;
			recalculateAlpha();
			auto self = shared_from_this();
			onAlphaChangeSignal(self);
			return self;
		}

		size_t Node::indexOf(const std::shared_ptr<const Node> &a_childItem) const {
			size_t i = 0;
			for(auto&& cell : childNodes){
				if(cell == a_childItem){
					return i;
				}
				++i;
			}
			return i;
		}

		size_t Node::myIndex() const {
			return (myParent) ? myParent->indexOf(shared_from_this()) : 0;
		}

		std::vector<size_t> Node::parentIndexList(size_t a_globalPriority, size_t a_modifyLastPriority) {
			std::vector<size_t> list;
			Node* current = this;
			while(current){
				list.push_back(current->myIndex());
				current = current->myParent;
			}
			if (!list.empty()) {
				list[0] += a_modifyLastPriority;
			}
			list.push_back(a_globalPriority); //This gives us the ability to prioritize more things before any nodes get evaluated.
			std::reverse(list.begin(), list.end());
			return list;
		}

		BoxAABB<> Node::bounds(bool a_includeChildren /*= true*/) {
			onBoundsRequestSignal(shared_from_this());
			if(a_includeChildren){
				if(!localBounds.empty() && !localChildBounds.empty()){
					return BoxAABB<>(localChildBounds).expandWith(localBounds);
				} else if(!localChildBounds.empty()){
					return localChildBounds;
				}
			}
			return localBounds;
		}

		std::shared_ptr<Node> Node::parent(const std::shared_ptr<Node> &a_parent) {
			auto self = shared_from_this();
			a_parent->add(self);
			return self;
		}

		void Node::recalculateLocalBounds() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			//if (!inBoundsCalculation) {
				auto self = shared_from_this();
				inBoundsCalculation = true;
				SCOPE_EXIT{ inBoundsCalculation = false; };

				auto oldBounds = localBounds;

				if (!childComponents.empty()) {
					localBounds = childComponents[0]->bounds();
					for (size_t i = 1; i < childComponents.size(); ++i) {
						auto componentBounds = childComponents[i]->bounds();
						if (!componentBounds.empty()) {
							localBounds.expandWith(componentBounds);
						}
					}
				} else {
					localBounds = BoxAABB<>();
				}
				if (myParent) {
					myParent->recalculateChildBounds();
				}

				if (localBounds != oldBounds) {
					onLocalBoundsChangeSignal(self);
				}
			//}
		}

		void Node::recalculateChildBounds() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			//if (!inChildBoundsCalculation) {
				inChildBoundsCalculation = true;
				SCOPE_EXIT{ inChildBoundsCalculation = false; };
				if (!childNodes.empty()) {
					localChildBounds = childNodes[0]->bounds();
					for (size_t i = 1; i < childNodes.size(); ++i) {
						auto nodeBounds = childNodes[i]->bounds();
						if (!nodeBounds.empty()) {
							localChildBounds.expandWith(nodeBounds);
						}
					}
				} else {
					localChildBounds = BoxAABB<>();
				}
				if (myParent) {
					myParent->recalculateChildBounds();
				}
			//}
		}

		void Node::recalculateMatrix() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			
			bool eitherMatrixUpdated = localMatrixDirty || worldMatrixDirty;
			if (localMatrixDirty) {
				localMatrixDirty = false;
				localMatrixTransform.makeIdentity();

				if (!translateTo.atOrigin()) {
					localMatrixTransform.translate(translateTo.x, translateTo.y, translateTo.z);
				}
				if (rotateTo != 0.0f) {
					localMatrixTransform.rotateX(toRadians(rotateTo.x)).rotateY(toRadians(rotateTo.y)).rotateZ(toRadians(rotateTo.z));
				}
				if (scaleTo != 1.0f) {
					localMatrixTransform.scale(scaleTo.x, scaleTo.y, scaleTo.z);
				}

				if (myParent) {
					myParent->recalculateChildBounds();
				}
			}

			if (eitherMatrixUpdated) {
				worldMatrixDirty = false;
				if (myParent) {
					worldMatrixTransform = myParent->worldTransform() * localMatrixTransform;
					parentAccumulatedAlpha = myParent->parentAccumulatedAlpha * nodeAlpha;
				} else {
					worldMatrixTransform = localMatrixTransform;
					parentAccumulatedAlpha = nodeAlpha;
				}
			}
		}

		Node::Node(Draw2D &a_draw2d, const std::string &a_id) :
			draw2d(a_draw2d),
			nodeId(a_id),
			onEnable(onEnableSignal),
			onDisable(onDisableSignal),
			onShow(onShowSignal),
			onHide(onHideSignal),
			onBoundsRequest(onBoundsRequestSignal),
			onPause(onPauseSignal),
			onResume(onResumeSignal),
			onChildAdd(onChildAddSignal),
			onChildRemove(onChildRemoveSignal),
			onAdd(onAddSignal),
			onRemove(onRemoveSignal),
			onTransformChange(onTransformChangeSignal),
			onLocalBoundsChange(onLocalBoundsChangeSignal),
			onOrderChange(onOrderChangeSignal),
			onAlphaChange(onAlphaChangeSignal),
			onChange(onChangeSignal),
			onComponentUpdate(onComponentUpdateSignal),
			onAttach(onAttachSignal),
			onDetach(onDetachSignal){

			worldMatrixTransform.makeIdentity();
			onAdd.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				onParentAlphaChangeSignal = myParent->onAlphaChangeSignal.connect([&](const std::shared_ptr<Node> &a_parent) {
					recalculateAlpha();
				});
				onTransformChangeSignal(a_self);
				recalculateAlpha();
				safeOnChange();
			});
			onRemove.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				if (myParent) {
					myParent->recalculateChildBounds();
					myParent = nullptr;
				}

				onParentAlphaChangeSignal = nullptr;
				onTransformChangeSignal(a_self);
				recalculateAlpha();
				safeOnChange();
			});
			onTransformChange.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				markMatrixDirty();
				safeOnChange();
			});
			
			auto onChangeCallback = [](const std::shared_ptr<Node> &a_self) {
				a_self->safeOnChange();
			};
			onShow.connect("SELF", onChangeCallback);
			onHide.connect("SELF", onChangeCallback);
			onLocalBoundsChange.connect("SELF", onChangeCallback);
			onOrderChange.connect("SELF", onChangeCallback);
			onAlphaChange.connect("SELF", onChangeCallback);
			onComponentUpdate.connect("SELF", [&](const std::shared_ptr<Component> &a_component){
				std::shared_ptr<Component> safeguard = a_component;
				try {
					auto owningNode = safeguard->owner();
					owningNode->safeOnChange();
				} catch (...) {
					std::cerr << "Failed to call onChangeSignal from onComponentUpdate! Node went out of scope too soon." << std::endl;
				}
			});

			onChange.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				if (a_self->myParent) {
					a_self->myParent->onChangeSignal(myParent->shared_from_this());
				}
			});
		}

		void Node::safeOnChange() {
			std::shared_ptr<Node> safeguard = shared_from_this();
			if (!inOnChange) {
				SCOPE_EXIT{ inOnChange = false; };
				inOnChange = true;
				onChangeSignal(safeguard);
			} else {
				onChangeCallNeeded = true;
			}
		}

		void Node::recalculateAlpha() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			parentAccumulatedAlpha = (myParent) ? myParent->parentAccumulatedAlpha * nodeAlpha : nodeAlpha;
			for (auto&& child : childNodes) {
				child->recalculateAlpha();
			}
		}

		std::shared_ptr<Node> Node::serializable(bool a_serializable) {
			allowSerialize = a_serializable;
			return shared_from_this();
		}

		bool Node::serializable() const {
			return allowSerialize;
		}

		std::shared_ptr<Node> Node::enable() {
			auto self = shared_from_this();
			bool changedState = false;
			if (!allowDraw) {
				changedState = true;
				allowDraw = true;
				onResumeSignal(self);
			}
			if (!allowUpdate) {
				changedState = true;
				allowUpdate = true;
				onShowSignal(self);
			}
			if (changedState) {
				onEnableSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::disable() {
			auto self = shared_from_this();
			bool changedState = false;
			if (allowDraw) {
				changedState = true;
				allowDraw = false;
				onPauseSignal(self);
			}
			if (allowUpdate) {
				changedState = true;
				allowUpdate = false;
				onHideSignal(self);
			}
			if (changedState) {
				onDisableSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::resume() {
			auto self = shared_from_this();
			if (!allowUpdate) {
				allowUpdate = true;
				onResumeSignal(self);
				if (allowDraw == true) {
					onEnableSignal(self);
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::pause() {
			auto self = shared_from_this();
			if (allowUpdate) {
				allowUpdate = false;
				onPauseSignal(self);
				if (allowDraw == false) {
					onDisableSignal(self);
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::show() {
			auto self = shared_from_this();
			if (!allowDraw) {
				allowDraw = true;
				onShowSignal(self);
				if (allowUpdate == true) {
					onEnableSignal(self);
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::hide() {
			auto self = shared_from_this();
			if (allowDraw) {
				allowDraw = false;
				onHideSignal(self);
				if (allowUpdate == false) {
					onDisableSignal(self);
				}
			}
			return self;
		}

		void Node::fixChildOwnership() {
			for (auto &&childNode : childNodes) {
				childNode->myParent = this;
			}
		}

		void Node::postLoadStep(bool a_isRootNode) {
			if (a_isRootNode) {
				recalculateAlpha();
				recalculateMatrixAfterLoad();
			}
		}

		void Node::recalculateMatrixAfterLoad() {
			localMatrixDirty = false;
			worldMatrixDirty = false;
			std::lock_guard<std::recursive_mutex> guard(lock);
			localMatrixTransform.makeIdentity();

			if (!translateTo.atOrigin()) {
				localMatrixTransform.translate(translateTo.x, translateTo.y, translateTo.z);
			}
			if (rotateTo != 0.0f) {
				localMatrixTransform.rotateX(toRadians(rotateTo.x)).rotateY(toRadians(rotateTo.y)).rotateZ(toRadians(rotateTo.z));
			}
			if (scaleTo != 1.0f) {
				localMatrixTransform.scale(scaleTo.x, scaleTo.y, scaleTo.z);
			}

			if (myParent) {
				worldMatrixTransform = myParent->worldMatrixTransform * localMatrixTransform;
				parentAccumulatedAlpha = myParent->parentAccumulatedAlpha * nodeAlpha;
			} else {
				worldMatrixTransform = localMatrixTransform;
				parentAccumulatedAlpha = nodeAlpha;
			}

			for (auto&& child : childNodes) {
				child->recalculateMatrixAfterLoad();
			}
		}

		Point<> Node::localFromScreen(const Point<int> &a_screen) {
			return draw2d.localFromScreen(a_screen, worldTransform());
		}

		std::vector<Point<>> Node::localFromScreen(const std::vector<Point<int>> &a_screen) {
			std::vector<Point<>> processed;
			for (const Point<int>& point : a_screen) {
				processed.push_back(draw2d.localFromScreen(point, worldTransform()));
			}
			return processed;
		}

		Point<> Node::localFromWorld(const Point<> &a_world) {
			return draw2d.localFromWorld(a_world, worldTransform());
		}

		std::vector<Point<>> Node::localFromWorld(std::vector<Point<>> a_world) {
			for (Point<>& point : a_world) {
				point = draw2d.localFromWorld(point, worldTransform());
			}
			return a_world;
		}

		Point<int> Node::screenFromLocal(const Point<> &a_local) {
			return draw2d.screenFromLocal(a_local, worldTransform());
		}

		std::vector<Point<int>> Node::screenFromLocal(const std::vector<Point<>> &a_local) {
			std::vector<Point<int>> processed;
			for (const Point<>& point : a_local) {
				processed.push_back(draw2d.screenFromLocal(point, worldTransform()));
			}
			return processed;
		}

		Point<> Node::worldFromLocal(const Point<> &a_local) {
			return draw2d.worldFromLocal(a_local, worldTransform());
		}

		std::vector<Point<>> Node::worldFromLocal(std::vector<Point<>> a_local) {
			for (Point<>& point : a_local) {
				point = draw2d.worldFromLocal(point, worldTransform());
			}
			return a_local;
		}

		std::shared_ptr<Node> Node::screenPosition(const Point<int> &a_newPosition) {
			return position((myParent != nullptr) ? parent()->localFromScreen(a_newPosition) : renderer().worldFromScreen(a_newPosition));
		}

		std::shared_ptr<Node> Node::nodePosition(const std::shared_ptr<Node> &a_newPosition) {
			return worldPosition(a_newPosition->worldPosition());
		}

		std::shared_ptr<Node> Node::worldPosition(const Point<> &a_newPosition) {
			return position((myParent != nullptr) ? parent()->localFromWorld(a_newPosition) : a_newPosition);
		}

		std::shared_ptr<Node> Node::translate(const Point<> &a_newPosition) {
			return position(position() + a_newPosition);
		}

		MV::AxisAngles Node::worldRotation() const {
			auto accumulatedRotation = rotateTo;
			const Node* current = this;
			while (current = current->myParent) {
				accumulatedRotation += current->rotateTo;
			}
			return accumulatedRotation;
		}

		std::shared_ptr<Node> Node::worldRotation(const AxisAngles &a_newAngle) {
			auto accumulatedRotation = a_newAngle;
			const Node* current = this;
			while (current = current->myParent) {
				accumulatedRotation -= current->rotateTo;
			}
			return rotation(accumulatedRotation);
		}

		MV::Scale Node::worldScale() const {
			auto accumulatedScale = scaleTo;
			const Node* current = this;
			while (current = current->myParent) {
				accumulatedScale *= current->scaleTo;
			}
			return accumulatedScale;
		}

		std::shared_ptr<Node> Node::worldScale(const Scale &a_newScale) {
			auto accumulatedScale = a_newScale;
			const Node* current = this;
			while (current = current->myParent) {
				accumulatedScale /= current->scaleTo;
			}
			return scale(accumulatedScale);
		}

		void Node::markMatrixDirty(bool a_rootCall) {
			if (a_rootCall) {
				localMatrixDirty = true;
			} else {
				worldMatrixDirty = true;
			}
			for (auto&& child : *this) {
				child->markMatrixDirty(false);
			}
		}

		std::string Node::getCloneId(const std::string &a_original) const {
			std::regex clonePattern("(.+) (\\(clone ([0-9]*)\\))");
			std::smatch originalMatches;
			std::regex_match(a_original, originalMatches, clonePattern);
			std::string strippedId = (originalMatches.size() > 2) ? originalMatches[1] : a_original;
			
			int cloneCount = 0;
			for (auto&& child : childNodes) {
				if (cloneCount == 0 && child->nodeId == strippedId) {
					cloneCount = 1;
				} else {
					std::smatch matches;
					std::regex_match(child->nodeId, matches, clonePattern);
					if (matches.size() > 3 && matches[1] == strippedId) {
						cloneCount = std::max(cloneCount, std::stoi(matches[3]) + 1);
					}
				}
			}

			if (cloneCount == 0) {
				return strippedId;
			} else {
				return strippedId + " (clone " + std::to_string(cloneCount) + ")";
			}
		}

		std::shared_ptr<Node> Node::clone(const std::shared_ptr<Node> &a_parent) {
			std::string newNodeId = nodeId;
			if (a_parent) {
				newNodeId = a_parent->getCloneId(nodeId);
			} else if (myParent) {
				newNodeId = myParent->getCloneId(nodeId);
			}
			std::shared_ptr<Node> result = Node::make(draw2d, newNodeId);

			//myParent set later
			result->myParent = nullptr;
			result->allowDraw = allowDraw;
			result->allowUpdate = allowUpdate;
			result->translateTo = translateTo;
			result->rotateTo = rotateTo;
			result->scaleTo = scaleTo;
			result->sortDepth = sortDepth;
			result->nodeAlpha = nodeAlpha;
			result->localBounds = localBounds;
			result->localChildBounds = localChildBounds;
			result->allowSerialize = allowSerialize;
			result->localMatrixDirty = true;
			result->worldMatrixDirty = true;

			for (auto&& childComponent : childComponents) {
				childComponent->clone(result);
			}

			for (auto&& childNode : childNodes) {
				result->childNodes.push_back(childNode->cloneInternal(result));
			}

			recalculateAlpha();
			recalculateMatrixAfterLoad();

			if (a_parent) {
				a_parent->add(result);
			} else if (myParent) {
				myParent->add(result);
			}

			return result;
		}

		std::shared_ptr<Node> Node::cloneInternal(const std::shared_ptr<Node> &a_parent) {
			std::shared_ptr<Node> result = Node::make(draw2d, nodeId);

			result->myParent = a_parent.get();
			result->allowDraw = allowDraw;
			result->allowUpdate = allowUpdate;
			result->translateTo = translateTo;
			result->rotateTo = rotateTo;
			result->scaleTo = scaleTo;
			result->sortDepth = sortDepth;
			result->nodeAlpha = nodeAlpha;
			result->localBounds = localBounds;
			result->localChildBounds = localChildBounds;
			result->allowSerialize = allowSerialize;
			result->localMatrixDirty = true;
			result->worldMatrixDirty = true;

			for (auto&& childComponent : childComponents) {
				childComponent->clone(result);
			}

			for (auto&& childNode : childNodes) {
				result->childNodes.push_back(childNode->cloneInternal(result));
			}

			return result;
		}

		std::shared_ptr<Node> Node::makeOrGet(const std::string &a_id) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto foundNode = get(a_id, false);
			if (foundNode) {
				return foundNode;
			} else {
				auto toAdd = Node::make(draw2d, a_id);
				add(toAdd);
				return toAdd;
			}
		}

		void Node::unsilenceInternal(bool a_callBatched /*= true*/, bool a_callChanged /*= true*/) {
			const std::shared_ptr<Node> self = shared_from_this();
			bool changed = false;
			if (onChildAddSignal.unblock()) {
				if (a_callBatched) {
					try {
						onChildAddSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onChildRemoveSignal.unblock()) {
				//TODO: call for each child removed
				changed = true;
			}

			if (onAddSignal.unblock()) {
				if (a_callBatched) {
					try {
						onAddSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onRemoveSignal.unblock()) {
				if (a_callBatched) {
					try {
						onRemoveSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onEnableSignal.unblock() && (allowUpdate && allowDraw)) {
				if (a_callBatched) {
					try {
						onEnableSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onDisableSignal.unblock() && (!allowDraw && !allowUpdate)) {
				if (a_callBatched) {
					try {
						onDisableSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onPauseSignal.unblock() && (!allowUpdate)) {
				if (a_callBatched) {
					try {
						onPauseSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onResumeSignal.unblock() && (allowUpdate)) {
				if (a_callBatched) {
					try {
						onResumeSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onShowSignal.unblock() && (allowDraw)) {
				if (a_callBatched) {
					try {
						onShowSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onHideSignal.unblock() && (!allowDraw)) {
				if (a_callBatched) {
					try {
						onHideSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onBoundsRequestSignal.unblock()) {
				if (a_callBatched) {
					try {
						onBoundsRequestSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onTransformChangeSignal.unblock()) {
				if (a_callBatched) {
					try {
						onTransformChangeSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onLocalBoundsChangeSignal.unblock()) {
				if (a_callBatched) {
					try {
						onLocalBoundsChangeSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onOrderChangeSignal.unblock()) {
				if (a_callBatched) {
					try {
						onOrderChangeSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onAlphaChangeSignal.unblock()) {
				if (a_callBatched) {
					try {
						onAlphaChangeSignal(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onComponentUpdateSignal.unblock()) {
				//TODO: call for each component updated
				changed = true;
			}

			onChangeSignal.unblock();
			if (changed && a_callChanged) {
				safeOnChange();
			}
		}

		chaiscript::ChaiScript& Node::hook(chaiscript::ChaiScript &a_script) {
			a_script.add(chaiscript::user_type<Node>(), "Node");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node> (Node::*)()>(&Node::make)), "make");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::string &)>(&Node::make)), "make");
			a_script.add(chaiscript::fun(&Node::makeOrGet), "makeOrGet");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::string &, bool)>(&Node::remove)), "remove");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::shared_ptr<Node> &, bool)>(&Node::remove)), "remove");
			a_script.add(chaiscript::fun(&Node::clear), "clear");
			a_script.add(chaiscript::fun(&Node::root), "root");
			a_script.add(chaiscript::fun(&Node::get), "get");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node> (Node::*)() const>(&Node::parent)), "parent");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node> (Node::*)(const std::shared_ptr<Node> &)>(&Node::parent)), "parent");
			a_script.add(chaiscript::fun(&Node::operator[]), "[]");
			a_script.add(chaiscript::fun(&Node::size), "size");
			a_script.add(chaiscript::fun(&Node::empty), "empty");
			a_script.add(chaiscript::fun(static_cast<std::string (Node::*)() const>(&Node::id)), "id");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node> (Node::*)(const std::string &)>(&Node::id)), "id");
			a_script.add(chaiscript::fun(static_cast<PointPrecision(Node::*)() const>(&Node::depth)), "depth");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(PointPrecision)>(&Node::depth)), "depth");

			a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)() const>(&Node::position)), "position");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<> &)>(&Node::position)), "position");
			a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)()>(&Node::worldPosition)), "worldPosition");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<> &)>(&Node::worldPosition)), "worldPosition");
			a_script.add(chaiscript::fun(static_cast<Point<int>(Node::*)()>(&Node::screenPosition)), "screenPosition");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<int> &)>(&Node::screenPosition)), "screenPosition");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::shared_ptr<Node> &)>(&Node::nodePosition)), "nodePosition");
			
			a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)(const Point<> &)>(&Node::worldFromLocal)), "worldFromLocal");
			a_script.add(chaiscript::fun(static_cast<Point<int>(Node::*)(const Point<> &)>(&Node::screenFromLocal)), "screenFromLocal");
			a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)(const Point<int> &)>(&Node::localFromScreen)), "localFromScreen");
			a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)(const Point<> &)>(&Node::localFromWorld)), "localFromWorld");

			a_script.add(chaiscript::fun(static_cast<std::vector<Point<>>(Node::*)(std::vector<Point<>>)>(&Node::worldFromLocal)), "worldFromLocal");
			a_script.add(chaiscript::fun(static_cast<std::vector<Point<int>>(Node::*)(const std::vector<Point<>> &)>(&Node::screenFromLocal)), "screenFromLocal");
			a_script.add(chaiscript::fun(static_cast<std::vector<Point<>>(Node::*)(const std::vector<Point<int>> &)>(&Node::localFromScreen)), "localFromScreen");
			a_script.add(chaiscript::fun(static_cast<std::vector<Point<>>(Node::*)(std::vector<Point<>>)>(&Node::localFromWorld)), "localFromWorld");
			
			a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Node::*)(const BoxAABB<> &)>(&Node::worldFromLocal)), "worldFromLocal");
			a_script.add(chaiscript::fun(static_cast<BoxAABB<int>(Node::*)(const BoxAABB<> &)>(&Node::screenFromLocal)), "screenFromLocal");
			a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Node::*)(const BoxAABB<int> &)>(&Node::localFromScreen)), "localFromScreen");
			a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Node::*)(const BoxAABB<> &)>(&Node::localFromWorld)), "localFromWorld");

			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<> &)>(&Node::translate)), "translate");

			a_script.add(chaiscript::fun(static_cast<AxisAngles(Node::*)() const>(&Node::rotation)), "rotation");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles &)>(&Node::rotation)), "rotation");
			a_script.add(chaiscript::fun(static_cast<AxisAngles(Node::*)() const>(&Node::worldRotation)), "worldRotation");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles &)>(&Node::worldRotation)), "worldRotation");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles &)>(&Node::addRotation)), "addRotation");
			
			a_script.add(chaiscript::fun(static_cast<Scale(Node::*)() const>(&Node::scale)), "scale");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Scale &)>(&Node::scale)), "scale");
			a_script.add(chaiscript::fun(static_cast<Scale(Node::*)() const>(&Node::worldScale)), "worldScale");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Scale &)>(&Node::worldScale)), "worldScale");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Scale &)>(&Node::addScale)), "addScale");

			a_script.add(chaiscript::fun(&Node::show), "show");
			a_script.add(chaiscript::fun(&Node::hide), "hide");
			a_script.add(chaiscript::fun(&Node::visible), "visible");

			a_script.add(chaiscript::fun(&Node::pause), "pause");
			a_script.add(chaiscript::fun(&Node::resume), "resume");
			a_script.add(chaiscript::fun(&Node::updating), "updating");
			a_script.add(chaiscript::fun(&Node::disable), "disable");
			a_script.add(chaiscript::fun(&Node::enable), "enable");
			a_script.add(chaiscript::fun(&Node::active), "active");

			a_script.add(chaiscript::fun(&Node::bounds), "bounds");
			a_script.add(chaiscript::fun(&Node::worldBounds), "worldBounds");
			a_script.add(chaiscript::fun(&Node::screenBounds), "screenBounds");

			a_script.add(chaiscript::fun(&Node::clone), "clone");

			return a_script;
		}


		std::ostream& operator<<(std::ostream& os, const std::shared_ptr<Node>& a_node) {
			std::string spacing(a_node->parentIndexList().size(), ' ');
			os << spacing << "|->(" << a_node->id() << ")[" << a_node->size() << "]: L:" << a_node->position() << " | W:" << a_node->worldPosition() << " | S:" << a_node->screenPosition() << "\n";
			for (auto&& child : *a_node) {
				if (a_node->serializable()){
					os << child;
				}
			}
			return os;
		}
	}
}
