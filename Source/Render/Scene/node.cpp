#include "node.h"
#include "stddef.h"
#include <numeric>
#include <regex>
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
				owner()->onComponentUpdateSlot(shared_from_this());
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


		Node::~Node() {
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
					component->update(a_delta);
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
				auto self = shared_from_this();
				auto child = *foundNode;
				childNodes.erase(foundNode);
				child->onRemoveSlot(child);
				onChildRemoveSlot(self, child);
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
				child->onRemoveSlot(child);
				onChildRemoveSlot(self, child);
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
				onChildRemoveSlot(self, childToRemove);
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
				{
					ReSort sort(self);
					nodeId = a_id;
				}
				onOrderChangeSlot(self);
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
				onOrderChangeSlot(self);
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
					onLocalBoundsChangeSlot(self);
				}
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

				if (myParent) {
					myParent->recalculateChildBounds();
				}
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
			onOrderChange(onOrderChangeSlot),
			onAlphaChange(onAlphaChangeSlot),
			onChange(onChangeSlot),
			onComponentUpdate(onComponentUpdateSlot){

			worldMatrixTransform.makeIdentity();
			onAdd.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				onParentAlphaChangeSignal = myParent->onAlphaChangeSlot.connect([&](const std::shared_ptr<Node> &a_parent) {
					recalculateAlpha();
				});
				onTransformChangeSlot(a_self);
				recalculateAlpha();
				safeOnChange();
			});
			onRemove.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				if (myParent) {
					myParent->recalculateChildBounds();
					myParent = nullptr;
				}

				onParentAlphaChangeSignal = nullptr;
				onTransformChangeSlot(a_self);
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
					std::cerr << "Failed to call onChangeSlot from onComponentUpdate! Node went out of scope too soon." << std::endl;
				}
			});

			onChange.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				if (a_self->myParent) {
					a_self->myParent->onChangeSlot(myParent->shared_from_this());
				}
			});
		}

		void Node::safeOnChange() {
			std::shared_ptr<Node> safeguard = shared_from_this();
			if (!inOnChange) {
				SCOPE_EXIT{ inOnChange = false; };
				inOnChange = true;
				onChangeSlot(safeguard);
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
			return translate(localFromScreen(a_newPosition));
		}

		std::shared_ptr<Node> Node::nodePosition(const std::shared_ptr<Node> &a_newPosition) {
			return worldPosition(a_newPosition->worldPosition());
		}

		std::shared_ptr<Node> Node::worldPosition(const Point<> &a_newPosition) {
			return position(localFromWorld(a_newPosition));
		}

		std::shared_ptr<Node> Node::translate(const Point<> &a_newPosition) {
			return position(position() + a_newPosition);
		}

		MV::Scale Node::worldScale() const {
			auto accumulatedScale = scaleTo;
			const Node* current = this;
			while (current = current->myParent) {
				accumulatedScale *= current->scaleTo;
			}
			return accumulatedScale;
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
				if (cloneCount < 0 && child->nodeId == strippedId) {
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
			std::shared_ptr<Node> result = Node::make(draw2d, nodeId);

			result->myParent = a_parent ? a_parent.get() : nullptr;
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

			for (auto&& childNode : childNodes) {
				result->childNodes.push_back(childNode->cloneInternal(result));
			}

			for (auto&& childComponent : childComponents) {
				result->childComponents.push_back(childComponent->clone(result).self());
			}

			recalculateAlpha();
			recalculateMatrixAfterLoad();

			if (a_parent) {
				a_parent->add(result);
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

			for (auto&& childNode : childNodes) {
				result->childNodes.push_back(childNode->cloneInternal(result));
			}

			for (auto&& childComponent : childComponents) {
				result->childComponents.push_back(childComponent->clone(result).self());
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
			if (onChildAddSlot.unblock()) {
				if (a_callBatched) {
					try {
						onChildAddSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onChildRemoveSlot.unblock()) {
				//TODO: call for each child removed
				changed = true;
			}

			if (onAddSlot.unblock()) {
				if (a_callBatched) {
					try {
						onAddSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onRemoveSlot.unblock()) {
				if (a_callBatched) {
					try {
						onRemoveSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onEnableSlot.unblock() && (allowUpdate && allowDraw)) {
				if (a_callBatched) {
					try {
						onEnableSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onDisableSlot.unblock() && (!allowDraw && !allowUpdate)) {
				if (a_callBatched) {
					try {
						onDisableSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onPauseSlot.unblock() && (!allowUpdate)) {
				if (a_callBatched) {
					try {
						onPauseSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onResumeSlot.unblock() && (allowUpdate)) {
				if (a_callBatched) {
					try {
						onResumeSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onShowSlot.unblock() && (allowDraw)) {
				if (a_callBatched) {
					try {
						onShowSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onHideSlot.unblock() && (!allowDraw)) {
				if (a_callBatched) {
					try {
						onHideSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onBoundsRequestSlot.unblock()) {
				if (a_callBatched) {
					try {
						onBoundsRequestSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onTransformChangeSlot.unblock()) {
				if (a_callBatched) {
					try {
						onTransformChangeSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onLocalBoundsChangeSlot.unblock()) {
				if (a_callBatched) {
					try {
						onLocalBoundsChangeSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onOrderChangeSlot.unblock()) {
				if (a_callBatched) {
					try {
						onOrderChangeSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}
			if (onAlphaChangeSlot.unblock()) {
				if (a_callBatched) {
					try {
						onAlphaChangeSlot(self);
					} catch (std::exception &e) {
						std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
					}
				}
				changed = true;
			}

			if (onComponentUpdateSlot.unblock()) {
				//TODO: call for each component updated
				changed = true;
			}

			onChangeSlot.unblock();
			if (changed && a_callChanged) {
				safeOnChange();
			}
		}
		
		std::ostream& operator<<(std::ostream& os, const std::shared_ptr<Node>& a_node) {
			std::string spacing(a_node->parentIndexList().size(), ' ');
			os << spacing << "|->(" << a_node->id() << ")[" << a_node->size() << "]: L:" << a_node->position() << " | W:" << a_node->worldPosition() << " | S:" << a_node->screenPosition() << "\n";
			for (auto&& child : *a_node) {
				os << child;
			}
			return os;
		}
	}
}
