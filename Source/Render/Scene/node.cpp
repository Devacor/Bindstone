#include "node.h"
#include "stddef.h"
#include <numeric>
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
			owner()->recalculateLocalBounds();
		}

		void Component::notifyParentOfComponentChange() {
			owner()->onComponentUpdateSlot(shared_from_this());
		}

		std::shared_ptr<Component> Component::remove() {
			auto self = shared_from_this(); //guard against deallocation
			onRemoved();
			return self;
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


		Node::~Node() {
			for(auto &&child : childNodes){
				child->myParent = nullptr;
			}
		}

		void Node::draw() {
			if (allowDraw) {
				bool allowChildrenToDraw = true;
				for (auto&& component : childComponents) {
					allowChildrenToDraw = component->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					drawChildren();
				}
			}
		}

		void Node::drawChildren() {
			if (allowDraw) {
				for (auto&& child : childNodes) {
					child->draw();
				}
			}
		}

		void Node::draw(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				SCOPE_EXIT{ usingTemporaryMatrix = false; };
				usingTemporaryMatrix = true;
				temporaryWorldMatrixTransform = a_overrideParentMatrix * localMatrixTransform;
				bool allowChildrenToDraw = true;
				for (auto&& component : childComponents) {
					allowChildrenToDraw = component->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					drawChildren(temporaryWorldMatrixTransform);
				}
			}
		}

		void Node::drawChildren(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				for (auto&& child : childNodes) {
					child->draw(a_overrideParentMatrix);
				}
			}
		}

		void Node::update(double a_delta) {
			if (allowUpdate) {
				for (auto&& component : childComponents) {
					component->update(a_delta);
				}
				for (auto&& child : childNodes) {
					child->update(a_delta);
				}
			}
		}

		void Node::drawUpdate(double a_delta) {
			if (allowDraw && allowUpdate) {
				bool allowChildrenToDraw = true;
				for (auto&& component : childComponents) {
					component->update(a_delta);
					allowChildrenToDraw = component->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					for (auto&& child : childNodes) {
						child->drawUpdate(a_delta);
					}
				} else {
					for (auto&& child : childNodes) {
						child->update(a_delta);
					}
				}
			} else if (allowDraw) {
				draw();
			} else if (allowUpdate) {
				update(a_delta);
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
				a_child->onAddSlot(a_child);
				onChildAddSlot(a_child);
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
				auto child = *foundNode;
				childNodes.erase(foundNode);
				child->onRemoveSlot(child);
				onChildRemoveSlot(shared_from_this(), child);
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
				auto child = *foundNode;
				childNodes.erase(foundNode);
				child->onRemoveSlot(child);
				onChildRemoveSlot(shared_from_this(), child);
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
				childToRemove->onRemoveSlot(childToRemove);
				onChildRemoveSlot(shared_from_this(), childToRemove);
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

		std::shared_ptr<Node> Node::id(const std::string a_id) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			if (nodeId != a_id) {
				ReSort sort(self);
				nodeId = a_id;
			}
			return self;
		}

		std::shared_ptr<Node> Node::depth(PointPrecision a_newDepth) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto self = shared_from_this();
			if (sortDepth != a_newDepth) {
				ReSort sort(self);
				sortDepth = a_newDepth;
				onDepthChangeSlot(self);
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
			onTransformChangeSlot(self);
			return self;
		}

		std::shared_ptr<Node> Node::rotation(const AxisAngles &a_newRotation) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			rotateTo = a_newRotation;
			auto self = shared_from_this();
			onTransformChangeSlot(self);
			return self;
		}

		std::shared_ptr<Node> Node::scale(const Scale &a_newScale) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			scaleTo = a_newScale;
			auto self = shared_from_this();
			onTransformChangeSlot(self);
			return self;
		}

		std::shared_ptr<Node> Node::alpha(PointPrecision a_alpha) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			nodeAlpha = a_alpha;
			recalculateAlpha();
			auto self = shared_from_this();
			onAlphaChangeSlot(self);
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

		std::vector<size_t> Node::parentIndexList(size_t a_globalPriority /*= 0*/) {
			std::vector<size_t> list;
			Node* current = this;
			while(current){
				list.push_back(current->myIndex());
				current = current->myParent;
			}
			list.push_back(a_globalPriority); //This gives us the ability to prioritize more things before any nodes get evaluated.
			std::reverse(list.begin(), list.end());
			return list;
		}

		BoxAABB<> Node::bounds(bool a_includeChildren /*= true*/) {
			onBoundsRequestSlot(shared_from_this());
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
			if (!inBoundsCalculation) {
				auto self = shared_from_this();
				inBoundsCalculation = true;
				SCOPE_EXIT{ inBoundsCalculation = false; };

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
				onLocalBoundsChangeSlot(self);
			}
		}

		void Node::recalculateChildBounds() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (!inChildBoundsCalculation) {
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
			}
		}

		void Node::recalculateMatrix() {
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
				child->recalculateMatrix();
			}

			if (myParent) {
				myParent->recalculateChildBounds();
			}
		}

		Node::Node(Draw2D &a_draw2d, const std::string &a_id) :
			draw2d(a_draw2d),
			nodeId(a_id),
			onEnable(onEnableSlot),
			onDisable(onDisableSlot),
			onShow(onShowSlot),
			onHide(onHideSlot),
			onBoundsRequest(onBoundsRequestSlot),
			onPause(onPauseSlot),
			onResume(onResumeSlot),
			onChildAdd(onChildAddSlot),
			onChildRemove(onChildRemoveSlot),
			onAdd(onAddSlot),
			onRemove(onRemoveSlot),
			onTransformChange(onTransformChangeSlot),
			onLocalBoundsChange(onLocalBoundsChangeSlot),
			onDepthChange(onDepthChangeSlot),
			onAlphaChange(onAlphaChangeSlot) {

			worldMatrixTransform.makeIdentity();
			onAdd.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				onParentTransformChangeSignal = myParent->onTransformChangeSlot.connect([&](const std::shared_ptr<Node> &a_parent) {
					recalculateMatrix();
				});
				onParentAlphaChangeSignal = myParent->onAlphaChangeSlot.connect([&](const std::shared_ptr<Node> &a_parent) {
					recalculateAlpha();
				});
				onTransformChangeSlot(a_self);
				recalculateAlpha();
			});
			onRemove.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				if (myParent) {
					myParent->recalculateChildBounds();
					myParent = nullptr;
				}
				onParentTransformChangeSignal = nullptr;
				onParentAlphaChangeSignal = nullptr;
				onTransformChangeSlot(a_self);
				recalculateAlpha();
			});
			onTransformChange.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				recalculateMatrix();
			});
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
				onResumeSlot(self);
			}
			if (!allowUpdate) {
				changedState = true;
				allowUpdate = true;
				onShowSlot(self);
			}
			if (changedState) {
				onEnableSlot(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::disable() {
			auto self = shared_from_this();
			bool changedState = false;
			if (allowDraw) {
				changedState = true;
				allowDraw = false;
				onPauseSlot(self);
			}
			if (allowUpdate) {
				changedState = true;
				allowUpdate = false;
				onHideSlot(self);
			}
			if (changedState) {
				onDisableSlot(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::resume() {
			auto self = shared_from_this();
			if (!allowUpdate) {
				allowUpdate = true;
				onResumeSlot(self);
				if (allowDraw == true) {
					onEnableSlot(self);
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::pause() {
			auto self = shared_from_this();
			if (allowUpdate) {
				allowUpdate = false;
				onPauseSlot(self);
				if (allowDraw == false) {
					onDisableSlot(self);
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::show() {
			auto self = shared_from_this();
			if (!allowDraw) {
				allowDraw = true;
				onShowSlot(self);
				if (allowUpdate == true) {
					onEnableSlot(self);
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::hide() {
			auto self = shared_from_this();
			if (allowDraw) {
				allowDraw = false;
				onHideSlot(self);
				if (allowUpdate == false) {
					onDisableSlot(self);
				}
			}
			return self;
		}

		void Node::postLoadStep(bool a_isRootNode) {
			for (auto &&childNode : childNodes) {
				childNode->myParent = this;
			}
			if (a_isRootNode) {
				recalculateAlpha();
				recalculateMatrixAfterLoad();
				recalculateBoundsAfterLoad();
			}
		}

		void Node::recalculateMatrixAfterLoad() {
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
				child->recalculateMatrix();
			}
		}

		void Node::recalculateBoundsAfterLoad() {
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
			for (auto&& child : childNodes) {
				child->recalculateBoundsAfterLoad();
			}
			if (childNodes.empty()) {
				recalculateChildBoundsAfterLoad();
			}
		}

		void Node::recalculateChildBoundsAfterLoad() {
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
				myParent->recalculateChildBoundsAfterLoad();
			}
		}

	}
}
