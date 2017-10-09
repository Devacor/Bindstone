#include "node.h"
#include "stddef.h"
#include <numeric>
#include <regex>
#include "chaiscript/chaiscript.hpp"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_CLASS_VERSION(MV::Scene::Node, 1);

namespace MV {
	namespace Scene {
		int64_t Node::recalculateLocalBoundsCalls = 0;
		int64_t Node::recalculateChildBoundsCalls = 0;
		int64_t Node::recalculateMatrixCalls = 0;

		Node::ReSort::ReSort(const std::shared_ptr<Node> &a_self):
			self(a_self) {

			if(self->myParent){
				auto foundSelf = std::find(self->myParent->childNodes.begin(), self->myParent->childNodes.end(), self);
				MV::require<MV::PointerException>(foundSelf != self->myParent->childNodes.end(), "Failed to re-sort: [", self->id(), "]");
				originalIndex = std::distance(self->myParent->childNodes.begin(), foundSelf);
				self->myParent->childNodes.erase(foundSelf);
			}
		}

		bool Node::ReSort::insert() {
			if (!inserted) {
				inserted = true;
				if (self->myParent) {
					int64_t newIndex = std::distance(self->myParent->childNodes.begin(), insertSorted(self->myParent->childNodes, self));
					return originalIndex != newIndex;
				}
			}
			return false;
		}

		Node::ReSort::~ReSort() {
			insert();
		}

		Node::~Node() {
			for (auto &&component : childComponents) {
				component->onOwnerDestroyed();
			}

			for(auto &&child : childNodes){
				if (child) {
					child->myParent = nullptr;
				}
			}
		}

		void Node::draw() {
			if (allowDraw) {
				bool allowChildrenToDraw = true;
				for (size_t i = 0; i < childComponents.size();++i) {
					allowChildrenToDraw = childComponents[i]->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					drawChildren();
				}
				for (size_t i = 0; i < childComponents.size(); ++i) {
					childComponents[i]->endDraw();
				}
			}
		}

		void Node::drawChildren() {
			if (allowDraw) {
				for (size_t i = 0;i < childNodes.size();++i) {
					childNodes[i]->draw();
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
				for (size_t i = 0; i < childComponents.size();++i) {
					allowChildrenToDraw = childComponents[i]->draw() && allowChildrenToDraw;
				}
				if (allowChildrenToDraw) {
					drawChildren(worldTransform());
				}
				for (size_t i = 0; i < childComponents.size(); ++i) {
					childComponents[i]->endDraw();
				}
			}
		}

		void Node::drawChildren(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				for (size_t i = 0; i < childNodes.size(); ++i) {
					childNodes[i]->draw(a_overrideParentMatrix);
				}
			}
		}

		void Node::update(double a_delta, bool a_force) {
			allowChangeCallNeeded = true;
			if (allowUpdate || a_force) {
				for (size_t i = 0; i < childComponents.size(); ++i) {
					childComponents[i]->update(a_delta);
				}
				for (size_t i = 0; i < childNodes.size(); ++i) {
					childNodes[i]->update(a_delta);
				}
				if (rootTask) {
					if (rootTask->update(a_delta)) {
						rootTask.reset();
					}
				}
			}
			if (onChangeCallNeeded) {
				onChangeCallNeeded = false;
				allowChangeCallNeeded = false;
				onChangeSignal(shared_from_this());
			}
		}

		void Node::drawUpdate(double a_delta) {
			auto selfReference = shared_from_this(); //keep us alive no matter the update step
			selfReference->update(a_delta);
			selfReference->draw();
		}

		std::shared_ptr<Node> Node::make(Draw2D& a_draw2d, const std::string &a_id) {
			return std::shared_ptr<Node>(new Node(a_draw2d, a_id));
		}

		std::shared_ptr<Node> Node::make(Draw2D& a_draw2d) {
			return make(a_draw2d, guid("root_"));
		}

		std::shared_ptr<Node> Node::load(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, bool a_doPostLoadStep) {
			return load(a_filename, a_binder, "", a_doPostLoadStep);
		}

		std::shared_ptr<Node> Node::load(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, const std::string &a_newNodeId, bool a_doPostLoadStep) {
			std::ifstream stream(a_filename);
			require<ResourceException>(stream, "File not found for Node::load: ", a_filename);
			cereal::JSONInputArchive archive(stream);
			if (a_binder) {
				a_binder(archive);
			}
			std::shared_ptr<Node> result;
			archive.add(cereal::make_nvp("postLoad", a_doPostLoadStep));
			archive(result);
			if (!a_newNodeId.empty()) {
				result->id(a_newNodeId);
			}
			return result;
		}

		std::shared_ptr<Node> Node::loadBinary(const std::string &a_filename, const std::function<void(cereal::PortableBinaryInputArchive &)> a_binder, bool a_doPostLoadStep) {
			return loadBinary(a_filename, a_binder, "", a_doPostLoadStep);
		}

		std::shared_ptr<Node> Node::loadBinary(const std::string &a_filename, const std::function<void(cereal::PortableBinaryInputArchive &)> a_binder, const std::string &a_newNodeId, bool a_doPostLoadStep) {
			std::ifstream stream(a_filename);
			require<ResourceException>(stream, "File not found for Node::load: ", a_filename);
			cereal::PortableBinaryInputArchive archive(stream);
			if (a_binder) {
				a_binder(archive);
			}
			std::shared_ptr<Node> result;
			archive.add(cereal::make_nvp("postLoad", a_doPostLoadStep));
			archive(result);
			if (!a_newNodeId.empty()) {
				result->id(a_newNodeId);
			}
			return result;
		}

		std::shared_ptr<Node> Node::save(const std::string &a_filename, bool a_renameNodeToFile) {
			return save(a_filename, a_renameNodeToFile ? fileNameFromPath(a_filename) : nodeId);
		}

		std::shared_ptr<Node> Node::save(const std::string &a_filename, const std::string &a_newId) {
			std::ofstream stream(a_filename);

			std::string oldId = nodeId;
			auto oldParent = myParent;
			SCOPE_EXIT{ nodeId = oldId; myParent = oldParent; };
			nodeId = a_newId;
			myParent = nullptr;

			auto self = shared_from_this();
			{
				cereal::JSONOutputArchive archive(stream);
				archive(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::saveBinary(const std::string &a_filename, bool a_renameNodeToFile) {
			return save(a_filename, a_renameNodeToFile ? fileNameFromPath(a_filename) : nodeId);
		}

		std::shared_ptr<Node> Node::saveBinary(const std::string &a_filename, const std::string &a_newId) {
			std::ofstream stream(a_filename);

			std::string oldId = nodeId;
			auto oldParent = myParent;
			SCOPE_EXIT{ nodeId = oldId; myParent = oldParent; };
			nodeId = a_newId;
			myParent = nullptr;

			auto self = shared_from_this();
			{
				cereal::PortableBinaryOutputArchive archive(stream);
				archive(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::make(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, const std::string &a_newNodeId) {
			return loadChild(a_filename, a_binder, a_newNodeId);
		}

		std::shared_ptr<Node> Node::loadChild(const std::string &a_filename, const std::function<void(cereal::JSONInputArchive &)> a_binder, const std::string &a_newNodeId) {
			auto toAdd = Node::load(a_filename, a_binder, a_newNodeId, false);
			add(toAdd);
			toAdd->postLoadStep();
			return toAdd;
		}

		std::shared_ptr<Node> Node::loadChildBinary(const std::string &a_filename, const std::function<void(cereal::PortableBinaryInputArchive &)> a_binder, const std::string &a_newNodeId) {
			auto toAdd = Node::loadBinary(a_filename, a_binder, a_newNodeId, false);
			add(toAdd);
			toAdd->postLoadStep();
			return toAdd;
		}

		std::shared_ptr<Node> Node::make(const std::string &a_id) {
			auto toAdd = Node::make(draw2d, a_id);
			add(toAdd);
			return toAdd;
		}

		std::shared_ptr<Node> Node::make() {
			return make(MV::guid("id_"));
		}

		std::shared_ptr<Node> Node::add(const std::shared_ptr<Node> &a_child, bool a_overrideSortDepth) {
			Node* parentCheck = this;
			while (parentCheck = parentCheck->myParent) {
				MV::require<RangeException>(parentCheck != a_child.get(), "Adding [", a_child->id(), "] to [", id(), "] would result in a circular ownership!");
			}
			if(a_child->myParent != this){
				remove(a_child->id(), false);
				a_child->removeFromParent();
				if(a_overrideSortDepth){
					a_child->sortDepth = (childNodes.empty()) ? 0.0f : childNodes[childNodes.size() - 1]->sortDepth + 1.0f;
				}
				insertSorted(childNodes, a_child);
				a_child->myParent = this;
				if (a_child->ourCameraId != ourCameraId) {
					a_child->cameraIdInternal(ourCameraId);
				}
				a_child->onAddSignal(a_child);
				onChildAddSignal(a_child);
				a_child->markMatrixDirty(true);
			}
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::removeFromParent() {
			auto self = shared_from_this();
			if(myParent){
				myParent->remove(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::remove(const std::string &a_id, bool a_throw) {
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

		bool Node::hasParent(const std::string &a_id) const {
			const Node* currentParent = this;
			while (currentParent = currentParent->myParent) {
				if (currentParent->id() == a_id) {
					return true;
				}
			}
			return false;
		}
		std::shared_ptr<Node> Node::getParent(const std::string &a_id, bool a_throw) {
			Node* currentParent = this;
			while (currentParent = currentParent->myParent) {
				if (currentParent->id() == a_id) {
					return currentParent->shared_from_this();
				}
			}
			require<ResourceException>(!a_throw, "Failed to getParent: [", a_id, "] from child node: [", nodeId, "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::get(const std::string &a_id, bool a_throw) {
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_child){
				return a_child->id() == a_id;
			});
			if(foundNode != childNodes.end()){
				return *foundNode;
			}
			for(auto&& child : childNodes){
				auto found = child->get(a_id, false);
				if(found){
					return found;
				}
			}
			require<ResourceException>(!a_throw, "Failed to get: [", a_id, "] from parent node: [", nodeId, "]");
			return nullptr;
		}
		std::shared_ptr<Node> Node::getImmediate(const std::string &a_id, bool a_throw) {
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_child) {
				return a_child->id() == a_id;
			});
			if (foundNode != childNodes.end()) {
				return *foundNode;
			}
			require<ResourceException>(!a_throw, "Failed to get: [", a_id, "] from parent node: [", nodeId, "]");
			return nullptr;
		}

		bool Node::has(const std::string &a_id) const {
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_child) {
				return a_child->id() == a_id;
			});
			if (foundNode != childNodes.end()) {
				return true;
			}
			for (auto&& child : childNodes) {
				auto found = child->has(a_id);
				if (found) {
					return found;
				}
			}
			return false;
		}

		bool Node::hasImmediate(const std::string &a_id) const {
			auto foundNode = std::find_if(childNodes.begin(), childNodes.end(), [&](const std::shared_ptr<Node> &a_child) {
				return a_child->id() == a_id;
			});
			return foundNode != childNodes.end();
		}

		std::shared_ptr<Node> Node::id(const std::string &a_id) {
			auto self = shared_from_this();
			if (nodeId != a_id) {
				bool orderChanged = false;
				{
					ReSort sort(self);
					nodeId = a_id;
					orderChanged = sort.insert();
				}
				if (orderChanged) {
					onOrderChangeSignal(self);
					if (myParent) {
						myParent->onChildOrderChangeSignal(myParent->shared_from_this(), self);
					}
				}
			}
			return self;
		}

		std::shared_ptr<Node> Node::depth(PointPrecision a_newDepth) {
			auto self = shared_from_this();
			if (sortDepth != a_newDepth) {
				bool orderChanged = false;
				{
					ReSort sort(self);
					sortDepth = a_newDepth;
					orderChanged = sort.insert();
				}
				if (orderChanged) {
					onOrderChangeSignal(self);
					if (myParent) {
						myParent->onChildOrderChangeSignal(myParent->shared_from_this(), self);
					}
				}
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
			auto self = shared_from_this();
			if (a_newPosition != translateTo) {
				translateTo = a_newPosition;
				onTransformChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::rotation(const AxisAngles &a_newRotation) {
			auto self = shared_from_this();
			if (a_newRotation != rotateTo) {
				rotateTo = a_newRotation;
				onTransformChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::scale(const Scale &a_newScale) {
			auto self = shared_from_this();
			if (scaleTo != a_newScale) {
				scaleTo = a_newScale;
				onTransformChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::alpha(PointPrecision a_alpha) {
			auto self = shared_from_this();
			auto originalAlpha = nodeAlpha;
			nodeAlpha = a_alpha;
			recalculateAlpha();
			if (!equals(originalAlpha, a_alpha)) {
				onAlphaChangeSignal(self);
			}
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

		std::vector<int64_t> Node::parentIndexList(int64_t a_globalPriority) {
			std::vector<int64_t> list;
			Node* current = this;
			while(current){
				list.push_back(current->myIndex());
				current = current->myParent;
			}
			list.push_back(a_globalPriority); //This gives us the ability to prioritize more things before any nodes get evaluated.
			std::reverse(list.begin(), list.end());
			return list;
		}

		std::vector<std::shared_ptr<MV::Scene::Node>> Node::parents() {
			std::vector<std::shared_ptr<MV::Scene::Node>> list;
			Node* current = this;
			while (current) {
				current = current->myParent;
				if (current) {
					list.push_back(current->shared_from_this());
				}
			}
			return list;
		}

		BoxAABB<> Node::bounds(bool a_includeChildren /*= true*/) {
			if (dirtyLocalBounds) {
				recalculateLocalBounds();
			}
			onBoundsRequestSignal(shared_from_this());
			if(a_includeChildren){
				if (dirtyChildBounds) {
					recalculateChildBounds();
				}
				if(!localBounds.empty() && !localChildBounds.empty()){
					return BoxAABB<>(localChildBounds).expandWith(localBounds);
				} else if(!localChildBounds.empty()){
					return localChildBounds;
				}
			}
			return localBounds;
		}

		BoxAABB<> Node::childBounds() {
			if (dirtyChildBounds) {
				recalculateChildBounds();
			}
			return localChildBounds;
		}

		std::shared_ptr<Node> Node::parent(const std::shared_ptr<Node> &a_parent) {
			auto self = shared_from_this();
			a_parent->add(self);
			return self;
		}

		void Node::recalculateLocalBounds() {
			recalculateLocalBoundsCalls++;
			auto self = shared_from_this();
			auto oldBounds = localBounds;
			{
				dirtyLocalBounds = false;

				if (!childComponents.empty()) {
					localBounds = childComponents[0]->bounds();
					for (size_t i = 1; i < childComponents.size(); ++i) {
						auto componentBounds = childComponents[i]->bounds();
						if (!componentBounds.empty()) {
							localBounds.expandWith(componentBounds);
						}
					}
				}
				else {
					localBounds = BoxAABB<>();
				}
			}
			if (localBounds != oldBounds) {
				markParentBoundsDirty();
			}
		}

		void Node::recalculateChildBounds() {
			recalculateChildBoundsCalls++;
			{
				dirtyChildBounds = false;
				if (!childNodes.empty()) {
					localChildBounds = childNodes[0]->bounds();
					for (size_t i = 1; i < childNodes.size(); ++i) {
						if (childNodes[i]->selfVisible()) {
							auto nodeBounds = childNodes[i]->bounds();
							if (!nodeBounds.empty()) {
								localChildBounds.expandWith(nodeBounds);
							}
						}
					}
				} else {
					localChildBounds = BoxAABB<>();
				}
			}
			markParentBoundsDirty();
		}

		void Node::recalculateMatrix() {
			bool eitherMatrixUpdated = localMatrixDirty || worldMatrixDirty;
			if (localMatrixDirty) {
				localMatrixDirty = false;
				localMatrixTransform.makeIdentity();

				localMatrixTransform.position(translateTo);

				if (!MV::equals(rotateTo.x, 0.0f)) { localMatrixTransform.rotateX(toRadians(rotateTo.x)); }
				if (!MV::equals(rotateTo.y, 0.0f)) { localMatrixTransform.rotateY(toRadians(rotateTo.y)); }
				if (!MV::equals(rotateTo.z, 0.0f)) { localMatrixTransform.rotateZ(toRadians(rotateTo.z)); }

				if (scaleTo != 1.0f) {
					localMatrixTransform.scale(scaleTo.x, scaleTo.y, scaleTo.z);
				}

				markParentBoundsDirty();
			}

			if (eitherMatrixUpdated) {
				recalculateMatrixCalls++;
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
			onChildBoundsChange(onChildBoundsChangeSignal),
			onOrderChange(onOrderChangeSignal),
			onAlphaChange(onAlphaChangeSignal),
			onChildOrderChange(onChildOrderChangeSignal),
			onChange(onChangeSignal),
			onComponentUpdate(onComponentUpdateSignal),
			onAttach(onAttachSignal),
			onDetach(onDetachSignal){

			worldMatrixTransform.makeIdentity();
			auto blockOnChange = std::make_shared<bool>(false);
			onAdd.connect("SELF", [&, blockOnChange](const std::shared_ptr<Node> &a_self) {
				{
					*blockOnChange = true;
					SCOPE_EXIT{ *blockOnChange = false; };
					onParentAlphaChangeSignal = myParent->onAlphaChangeSignal.connect([&, blockOnChange](const std::shared_ptr<Node> &a_parent) {
						recalculateAlpha();
					});
					markBoundsDirty();
					recalculateAlpha();
				}
				safeOnChange();
			});
			onRemove.connect("SELF", [&, blockOnChange](const std::shared_ptr<Node> &a_self) {
				{
					*blockOnChange = true;
					SCOPE_EXIT{ *blockOnChange = false; };
					if (myParent) {
						myParent->markBoundsDirty();
						myParent = nullptr;
					}
					onParentAlphaChangeSignal = nullptr;
					onTransformChangeSignal(a_self);
					recalculateAlpha();
				}
				safeOnChange();
			});
			onTransformChange.connect("SELF", [&](const std::shared_ptr<Node> &a_self) {
				markMatrixDirty();
				safeOnChange();
			});
			
			auto onChangeCallback = [blockOnChange](const std::shared_ptr<Node> &a_self) {
				if (!*blockOnChange) {
					a_self->safeOnChange();
				}
			};
			onShow.connect("SELF", onChangeCallback);
			onHide.connect("SELF", onChangeCallback);
			onLocalBoundsChange.connect("SELF", onChangeCallback);
			onOrderChange.connect("SELF", onChangeCallback);
			onAlphaChange.connect("SELF", onChangeCallback);
			onComponentUpdate.connect("SELF", [&](const std::shared_ptr<Component> &a_component){
				std::shared_ptr<Component> safeguard = a_component;
				try {
					markBoundsDirty();
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
			} else if (allowChangeCallNeeded) {
				onChangeCallNeeded = true;
			}
		}

		void Node::recalculateAlpha() {
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
				markParentBoundsDirty();
				onShowSignal(self);
			}
			if (!allowUpdate) {
				changedState = true;
				allowUpdate = true;
				onResumeSignal(self);
			}
			if (changedState) {
				onEnableSignal(self);
			}
			return self;
		}

		std::shared_ptr<MV::Scene::Node> Node::active(bool a_isActive) {
			if (a_isActive) {
				return enable();
			} else {
				return disable();
			}
		}

		std::shared_ptr<Node> Node::disable() {
			auto self = shared_from_this();
			bool changedState = false;
			if (allowDraw) {
				changedState = true;
				allowDraw = false;
				markParentBoundsDirty();
				onHideSignal(self);
			}
			if (allowUpdate) {
				changedState = true;
				allowUpdate = false;
				onPauseSignal(self);
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

		std::shared_ptr<MV::Scene::Node> Node::updating(bool a_isUpdating) {
			if (a_isUpdating) {
				return resume();
			} else {
				return pause();
			}
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
				markParentBoundsDirty();
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
				markParentBoundsDirty();
				onHideSignal(self);
				if (allowUpdate == false) {
					onDisableSignal(self);
				}
			}
			return self;
		}

		std::shared_ptr<MV::Scene::Node> Node::visible(bool a_isVisible) {
			if (a_isVisible) {
				return show();
			} else {
				return hide();
			}
		}

		void Node::fixChildOwnership() {
			for (auto &&childNode : childNodes) {
				childNode->myParent = this;
			}
		}

		void Node::postLoadStep() {
			recalculateAlpha();
			recalculateMatrixAfterLoad();
		}

		void Node::quietLocalMatrixFix() {
			localMatrixDirty = false;
			worldMatrixDirty = false;
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
		}

		void Node::quietLocalAndChildMatrixFix() {
			quietLocalMatrixFix();

			for (auto&& child : childNodes) {
				child->quietLocalAndChildMatrixFix();
			}
		}

		void Node::localAndChildPostLoadInitializeComponents() {
			for (auto&& child : childNodes) {
				child->localAndChildPostLoadInitializeComponents();
			}
			for (auto&& childComponent : childComponents) {
				childComponent->postLoadInitialize();
			}
		}

		void Node::recalculateMatrixAfterLoad() {
			quietLocalMatrixFix();

			for (auto&& child : childNodes) {
				child->recalculateMatrixAfterLoad();
			}

			for (auto&& childComponent : childComponents){
				childComponent->postLoadInitialize();
			}
		}

		Point<> Node::localFromScreen(const Point<int> &a_screen) {
			return draw2d.localFromScreen(a_screen, ourCameraId, worldTransform());
		}

		std::vector<Point<>> Node::localFromScreen(const std::vector<Point<int>> &a_screen) {
			std::vector<Point<>> processed;
			for (const Point<int>& point : a_screen) {
				processed.push_back(draw2d.localFromScreen(point, ourCameraId, worldTransform()));
			}
			return processed;
		}

		Point<> Node::localFromWorld(const Point<> &a_world) {
			return draw2d.localFromWorld(a_world, ourCameraId, worldTransform());
		}

		std::vector<Point<>> Node::localFromWorld(std::vector<Point<>> a_world) {
			for (Point<>& point : a_world) {
				point = draw2d.localFromWorld(point, ourCameraId, worldTransform());
			}
			return a_world;
		}

		Point<int> Node::screenFromLocal(const Point<> &a_local) {
			return draw2d.screenFromLocal(a_local, ourCameraId, worldTransform());
		}

		std::vector<Point<int>> Node::screenFromLocal(const std::vector<Point<>> &a_local) {
			std::vector<Point<int>> processed;
			for (const Point<>& point : a_local) {
				processed.push_back(draw2d.screenFromLocal(point, ourCameraId, worldTransform()));
			}
			return processed;
		}

		Point<> Node::worldFromLocal(const Point<> &a_local) {
			return draw2d.worldFromLocal(a_local, ourCameraId, worldTransform());
		}

		std::vector<Point<>> Node::worldFromLocal(std::vector<Point<>> a_local) {
			for (Point<>& point : a_local) {
				point = draw2d.worldFromLocal(point, ourCameraId, worldTransform());
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
				
				markBoundsDirty();
			} else {
				worldMatrixDirty = true;
			}
			for (auto&& child : *this) {
				child->markMatrixDirty(false);
			}
		}

		std::string Node::getUniqueId(const std::string &a_original) const {
			std::regex clonePattern("(.+)_([0-9]+)$");
			std::smatch originalMatches;
			std::regex_match(a_original, originalMatches, clonePattern);
			std::string strippedId = (originalMatches.size() > 2) ? originalMatches[1] : a_original;
			int cloneCount = (originalMatches.size() > 2) ? std::stoi(originalMatches[2]) + 1 : 0;
			bool hasPlainStrippedId = false;
			bool done = false;
			while (!done) {
				done = true;
				for (auto&& child : childNodes) {
					hasPlainStrippedId = hasPlainStrippedId || (child->nodeId == strippedId);

					if (child->nodeId == strippedId + "_" + std::to_string(cloneCount)) {
						++cloneCount;
						done = false;
						break;
					}
				}
			}

			if (!hasPlainStrippedId && cloneCount == 0) {
				return strippedId;
			}
			else {
				return strippedId + "_" + std::to_string(cloneCount);
			}
		}

		std::shared_ptr<Node> Node::clone(const std::shared_ptr<Node> &a_parent) {
			std::string newNodeId = nodeId;
			if (a_parent) {
				newNodeId = a_parent->getUniqueId(nodeId);
			} else if (myParent) {
				newNodeId = myParent->getUniqueId(nodeId);
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
			result->ourCameraId = ourCameraId;
			result->localMatrixDirty = true;
			result->worldMatrixDirty = true;

			for (auto&& childNode : childNodes) {
				if (childNode->serializable()) {
					result->childNodes.push_back(childNode->cloneInternal(result));
				}
			}

			for (auto&& childComponent : childComponents) {
				if (childComponent->serializable()) {
					childComponent->clone(result);
				}
			}

			result->quietLocalAndChildMatrixFix();

			if (a_parent) {
				a_parent->add(result);
			} else if (myParent) {
				myParent->add(result);
			}

			result->recalculateAlpha();
			result->localAndChildPostLoadInitializeComponents();

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
			result->ourCameraId = ourCameraId;
			result->localMatrixDirty = true;
			result->worldMatrixDirty = true;

			for (auto&& childNode : childNodes) {
				if (childNode->serializable()) {
					result->childNodes.push_back(childNode->cloneInternal(result));
				}
			}

			for (auto&& childComponent : childComponents) {
				if (childComponent->serializable()) {
					childComponent->clone(result);
				}
			}

			return result;
		}

		std::shared_ptr<Node> Node::makeOrGet(const std::string &a_id) {
			auto foundNode = get(a_id, false);
			if (foundNode) {
				return foundNode;
			} else {
				auto toAdd = Node::make(draw2d, a_id);
				add(toAdd);
				return toAdd;
			}
		}

		void Node::markParentBoundsDirty() {
			auto* currentParent = myParent;
			while (currentParent) {
				currentParent->dirtyChildBounds = true;
				currentParent->onChildBoundsChangeSignal(currentParent->shared_from_this());
				currentParent = currentParent->myParent;
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
			if (onChildOrderChangeSignal.unblock()) {
				//TODO: call for each child 
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
			if (onChildBoundsChangeSignal.unblock()) {
				if (a_callBatched) {
					try {
						onChildBoundsChangeSignal(self);
					}
					catch (std::exception &e) {
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
			a_script.add(chaiscript::fun(&Node::removeFromParent), "removeFromParent");
			a_script.add(chaiscript::fun(&Node::clear), "clear");
			a_script.add(chaiscript::fun(&Node::root), "root");
			a_script.add(chaiscript::fun(&Node::hasImmediate), "hasImmediate");
			a_script.add(chaiscript::fun(&Node::has), "has");
			a_script.add(chaiscript::fun(&Node::hasParent), "hasParent");
			a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id) {return a_self.getParent(a_id); }), "getParent");
			a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id, bool a_throw) {return a_self.getParent(a_id, a_throw); }), "getParent");
			a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id) {return a_self.getImmediate(a_id); }), "getImmediate");
			a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id, bool a_throw) {return a_self.getImmediate(a_id, a_throw); }), "getImmediate");
			a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id) {return a_self.get(a_id); }), "get");
			a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id, bool a_throw) {return a_self.get(a_id, a_throw); }), "get");
			a_script.add(chaiscript::fun([](Node &a_self, const std::string &a_componentId) { return a_self.componentInChildren<Component>(a_componentId, false, true); }), "component");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node> (Node::*)() const>(&Node::parent)), "parent");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node> (Node::*)(const std::shared_ptr<Node> &)>(&Node::parent)), "parent");
			a_script.add(chaiscript::fun(&Node::operator[]), "[]");
			a_script.add(chaiscript::fun(&Node::size), "size");
			a_script.add(chaiscript::fun(&Node::empty), "empty");
			a_script.add(chaiscript::fun(&Node::task), "task");
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

			a_script.add(chaiscript::fun(static_cast<bool(Node::*)() const>(&Node::visible)), "visible");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(bool)>(&Node::visible)), "visible");
			a_script.add(chaiscript::fun(static_cast<bool(Node::*)() const>(&Node::visible)), "updating");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(bool)>(&Node::visible)), "updating");
			a_script.add(chaiscript::fun(static_cast<bool(Node::*)() const>(&Node::visible)), "active");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(bool)>(&Node::visible)), "active");

			a_script.add(chaiscript::fun(&Node::show), "show");
			a_script.add(chaiscript::fun(&Node::hide), "hide");
			a_script.add(chaiscript::fun(&Node::pause), "pause");
			a_script.add(chaiscript::fun(&Node::resume), "resume");
			a_script.add(chaiscript::fun(&Node::disable), "disable");
			a_script.add(chaiscript::fun(&Node::enable), "enable");

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
