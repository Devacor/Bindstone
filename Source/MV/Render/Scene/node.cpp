#include "node.h"
#include "stddef.h"
#include <numeric>
#include <regex>
#include <chrono>
#include <iostream>

#include "MV/Utility/scopeGuard.hpp"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include "MV/Utility/services.hpp"
#include "MV/Utility/threadPool.hpp"
#include "MV/Render/sharedTextures.h"
#include "text.h"
#include "clickable.h"

#include "MV/Serialization/serialize.h"
#include <jaiscript/properties/property_serialization.hpp>

static jai::registrar<MV::Scene::Node, MV::Services> _hookNode("Node",
	[](jai::dynamic_binder<MV::Scene::Node>& builder, const MV::Services&) {
	builder.method("make", static_cast<std::shared_ptr<MV::Scene::Node>(MV::Scene::Node::*)()>(&MV::Scene::Node::make));
	builder.method("make", static_cast<std::shared_ptr<MV::Scene::Node>(MV::Scene::Node::*)(const std::string&)>(&MV::Scene::Node::make));
	builder.method("makeOrGet", &MV::Scene::Node::makeOrGet);
	builder.method("removeFromParent", &MV::Scene::Node::removeFromParent);
	builder.method("clear", &MV::Scene::Node::clear);
	builder.method("root", &MV::Scene::Node::root);
	builder.method("has", &MV::Scene::Node::has);
	builder.method("hasParent", &MV::Scene::Node::hasParent);
	builder.method("size", &MV::Scene::Node::size);
	builder.method("empty", &MV::Scene::Node::empty);
	builder.method("task", &MV::Scene::Node::task);

	// Interface-script helpers: descendant-by-id lookup + typed component fetch.
	builder.method("get", [](MV::Scene::Node& a_self, const std::string& a_id) { return a_self.get(a_id); });
	builder.method("componentText", [](MV::Scene::Node& a_self) {
		return a_self.componentInChildren<MV::Scene::Text>(false, true, true).self();
	});
	builder.method("componentButton", [](MV::Scene::Node& a_self) {
		return a_self.componentInChildren<MV::Scene::Clickable>(false, true, true).self();
	});

	builder.method("id", static_cast<std::string(MV::Scene::Node::*)() const>(&MV::Scene::Node::id));
	builder.method("id", static_cast<std::shared_ptr<MV::Scene::Node>(MV::Scene::Node::*)(const std::string&)>(&MV::Scene::Node::id));

	builder.method("depth", static_cast<MV::PointPrecision(MV::Scene::Node::*)() const>(&MV::Scene::Node::depth));
	builder.method("depth", static_cast<std::shared_ptr<MV::Scene::Node>(MV::Scene::Node::*)(MV::PointPrecision)>(&MV::Scene::Node::depth));

	builder.method("position", static_cast<MV::Point<>(MV::Scene::Node::*)() const>(&MV::Scene::Node::position));
	builder.method("position", static_cast<std::shared_ptr<MV::Scene::Node>(MV::Scene::Node::*)(const MV::Point<>&)>(&MV::Scene::Node::position));

	builder.method("visible", static_cast<bool(MV::Scene::Node::*)() const>(&MV::Scene::Node::visible));
	builder.method("visible", static_cast<std::shared_ptr<MV::Scene::Node>(MV::Scene::Node::*)(bool)>(&MV::Scene::Node::visible));
	builder.method("show", &MV::Scene::Node::show);
	builder.method("hide", &MV::Scene::Node::hide);

	builder.method("bounds", &MV::Scene::Node::bounds);
	builder.method("worldBounds", &MV::Scene::Node::worldBounds);
	builder.method("screenBounds", &MV::Scene::Node::screenBounds);

	builder.method("clone", &MV::Scene::Node::clone);

	builder.property("onChildAdd", &MV::Scene::Node::onChildAdd);
	builder.property("onChildRemove", &MV::Scene::Node::onChildRemove);
	builder.property("onChildOrderChange", &MV::Scene::Node::onChildOrderChange);
	builder.property("onAdd", &MV::Scene::Node::onAdd);
	builder.property("onRemove", &MV::Scene::Node::onRemove);
	builder.property("onEnable", &MV::Scene::Node::onEnable);
	builder.property("onDisable", &MV::Scene::Node::onDisable);
	builder.property("onPause", &MV::Scene::Node::onPause);
	builder.property("onResume", &MV::Scene::Node::onResume);
	builder.property("onShow", &MV::Scene::Node::onShow);
	builder.property("onHide", &MV::Scene::Node::onHide);
	builder.property("onBoundsRequest", &MV::Scene::Node::onBoundsRequest);
	builder.property("onTransformChange", &MV::Scene::Node::onTransformChange);
	builder.property("onLocalBoundsChange", &MV::Scene::Node::onLocalBoundsChange);
	builder.property("onChildBoundsChange", &MV::Scene::Node::onChildBoundsChange);
	builder.property("onOrderChange", &MV::Scene::Node::onOrderChange);
	builder.property("onAlphaChange", &MV::Scene::Node::onAlphaChange);
	builder.property("onChange", &MV::Scene::Node::onChange);
	builder.property("onAttach", &MV::Scene::Node::onAttach);
	builder.property("onDetach", &MV::Scene::Node::onDetach);
	builder.property("onComponentUpdate", &MV::Scene::Node::onComponentUpdate);
});

namespace MV {
	namespace Scene {
		int64_t Node::recalculateLocalBoundsCalls = 0;
		int64_t Node::recalculateChildBoundsCalls = 0;
		int64_t Node::recalculateMatrixCalls = 0;

		Node::ReSort::ReSort(const std::shared_ptr<Node> &a_self):
			self(a_self) {

			if(self->myParent){
				auto foundSelf = std::find(self->myParent->childNodes->begin(), self->myParent->childNodes->end(), self);
				MV::require<MV::PointerException>(foundSelf != self->myParent->childNodes->end(), "Failed to re-sort: [", self->id(), "]");
				originalIndex = std::distance(self->myParent->childNodes->begin(), foundSelf);
				self->myParent->childNodes->erase(foundSelf);
			}
		}

		bool Node::ReSort::insert() {
			if (!inserted) {
				inserted = true;
				if (self->myParent) {
					int64_t newIndex = std::distance(self->myParent->childNodes->begin(), insertSorted(self->myParent->childNodes.get(), self));
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
				require<ResourceException>(renderer().device(), "Node::draw requires a render device.");
				bool allowChildrenToDraw = true;
				for (size_t i = 0; i < childComponents->size();++i) {
					// Guard against re-entrant node destruction: a draw callback can
					// fire a signal that detaches/destroys components on this node.
					// Check ownerIsAlive() before drawing to avoid the PointerException
					// from Component::owner() when the parent node has been released.
					if ((*childComponents)[i]->ownerIsAlive()) {
						allowChildrenToDraw = childComponents[i]->draw() && allowChildrenToDraw;
					}
				}
				if (allowChildrenToDraw) {
					drawChildren();
				}
				for (size_t i = 0; i < childComponents->size(); ++i) {
					if ((*childComponents)[i]->ownerIsAlive()) {
						childComponents[i]->endDraw();
					}
				}
			}
		}

		void Node::drawChildren() {
			if (allowDraw) {
				for (size_t i = 0;i < childNodes->size();++i) {
					childNodes[i]->draw();
				}
			}
		}

		void Node::draw(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				require<ResourceException>(renderer().device(), "Node::draw requires a render device.");
				// Only usingTemporaryMatrix needs resetting on exit. We deliberately do NOT
				// re-dirty the matrices: recalculateLocalMatrix() below leaves localMatrixTransform
				// valid (it is parent-independent) and never writes worldMatrixTransform, so the
				// persistent world transform is never polluted with this draw's temporary clip-space
				// parent. markMatrixDirty() (wired to the transform setters) still flags real changes.
				SCOPE_EXIT{ usingTemporaryMatrix = false; };
				usingTemporaryMatrix = true;
				temporaryWorldMatrixTransform = a_overrideParentMatrix;
				recalculateLocalMatrix();
				// override parent and local are both affine, so this is bit-identical to the
				// generic (*= localTransform()) product it replaces, with ~44% fewer flops.
				temporaryWorldMatrixTransform = affineMultiply(temporaryWorldMatrixTransform, localMatrixTransform);
				bool allowChildrenToDraw = true;
				for (size_t i = 0; i < childComponents->size();++i) {
					if ((*childComponents)[i]->ownerIsAlive()) {
						allowChildrenToDraw = childComponents[i]->draw() && allowChildrenToDraw;
					}
				}
				if (allowChildrenToDraw) {
					drawChildren(worldTransform());
				}
				for (size_t i = 0; i < childComponents->size(); ++i) {
					if ((*childComponents)[i]->ownerIsAlive()) {
						childComponents[i]->endDraw();
					}
				}
			}
		}

		void Node::drawChildren(const TransformMatrix &a_overrideParentMatrix) {
			if (allowDraw) {
				for (size_t i = 0; i < childNodes->size(); ++i) {
					childNodes[i]->draw(a_overrideParentMatrix);
				}
			}
		}

		void Node::update(double a_delta, bool a_force) {
			auto selfReference = shared_from_this(); //keep us alive no matter the update step
			allowChangeCallNeeded = true;
			if (allowUpdate || a_force) {
				for (size_t i = 0; i < childComponents->size(); ++i) {
					childComponents[i]->update(a_delta);
				}
				for (size_t i = 0; i < childNodes->size(); ++i) {
					childNodes[i]->update(a_delta);
				}
				if (rootTask) {
					if (!rootTask->update(a_delta)) {
						rootTask.reset();
					}
				}
			}
			if (onChangeCallNeeded) {
				onChangeCallNeeded = false;
				allowChangeCallNeeded = false;
				onChangeSignal(selfReference);
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

		std::shared_ptr<Node> Node::load(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep) {
			return load(a_filename, a_services, "", a_doPostLoadStep);
		}

		template <typename MakeReader>
		std::shared_ptr<Node> Node::loadFile(const char* a_loader, const std::string& a_filename, MV::Services& a_services, const std::string& a_newNodeId, bool a_doPostLoadStep, MakeReader a_makeReader) {
			auto contents = fileContents(a_filename);
			require<ResourceException>(!contents.empty(), "File not found for ", a_loader, ": ", a_filename);

			LoadOptions nodeOptions(a_services, a_doPostLoadStep);
			auto* engine = a_services.get<jai::engine>();

			const auto tParse0 = std::chrono::steady_clock::now();
			auto ar = a_makeReader(contents, engine);
			const auto tParse1 = std::chrono::steady_clock::now();

			ar.template set_user_context<MV::Services>(&a_services);
			std::shared_ptr<Node> result;
			ar(result);  // load_complete() fires on root via post_load depth tracking. File
			             // textures stream in via SharedTextures (async decode on the pool +
			             // main-thread GL upload); the scene loader stays out of it now.
			const auto tWalk1 = std::chrono::steady_clock::now();

			if (!a_newNodeId.empty()) {
				result->id(a_newNodeId);
			}

			if (contents.size() > 100000) {
				auto ms = [](auto a, auto b) {
					return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count() / 1000.0;
				};
				std::cout << "\n[" << a_loader << " " << a_filename << " " << contents.size() << " bytes]"
					<< "  parse: " << ms(tParse0, tParse1) << " ms"
					<< "  walk+postLoad: " << ms(tParse1, tWalk1) << " ms\n";
			}
			return result;
		}

		std::shared_ptr<Node> Node::load(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId, bool a_doPostLoadStep) {
			return loadFile("Node::load", a_filename, a_services, a_newNodeId, a_doPostLoadStep, [](const std::string& a_contents, jai::engine* a_engine) {
				return jai::serialization::json_archive_reader(a_contents, a_engine);
			});
		}

		std::shared_ptr<Node> Node::loadBinary(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep) {
			return loadBinary(a_filename, a_services, "", a_doPostLoadStep);
		}

		std::shared_ptr<Node> Node::loadBinary(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId, bool a_doPostLoadStep) {
			return loadFile("Node::loadBinary", a_filename, a_services, a_newNodeId, a_doPostLoadStep, [](const std::string& a_contents, jai::engine* a_engine) {
				return jai::serialization::binary_archive_reader(a_contents.data(), a_contents.size(), a_engine);
			});
		}

		std::shared_ptr<Node> Node::saveBinary(const std::string &a_filename, MV::Services& a_services, bool a_renameNodeToFile) {
			return saveBinary(a_filename, a_services, a_renameNodeToFile ? fileNameFromPath(a_filename) : nodeId.get());
		}

		std::shared_ptr<Node> Node::saveBinary(const std::string &a_filename, MV::Services& a_services, const std::string &a_newId) {
			auto self = shared_from_this();

			std::string oldId = nodeId.get();
			auto oldParent = myParent;
			SCOPE_EXIT{ nodeId = oldId; myParent = oldParent; };
			nodeId = a_newId;
			myParent = nullptr;

			auto* engine = a_services.get<jai::engine>();
			jai::serialization::binary_archive_writer ar(engine);
			ar(self);
			auto& data = ar.data();
			writeToFile(a_filename, std::string(data.begin(), data.end()));
			return self;
		}

		std::shared_ptr<Node> Node::make(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId) {
			return loadChild(a_filename, a_services, a_newNodeId);
		}

		std::shared_ptr<Node> Node::loadChild(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId) {
			auto toAdd = Node::load(a_filename, a_services, a_newNodeId, false);
			add(toAdd);
			toAdd->postLoadStep();
			return toAdd;
		}

		std::shared_ptr<Node> Node::loadChildBinary(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId) {
			auto toAdd = Node::loadBinary(a_filename, a_services, a_newNodeId, false);
			add(toAdd);
			toAdd->postLoadStep();
			return toAdd;
		}

		std::shared_ptr<Node> Node::saveJai(const std::string &a_filename, MV::Services& a_services, bool a_renameNodeToFile) {
			return saveJai(a_filename, a_services, a_renameNodeToFile ? fileNameFromPath(a_filename) : nodeId.get());
		}

		std::shared_ptr<Node> Node::saveJai(const std::string &a_filename, MV::Services& a_services, const std::string &a_newId) {
			auto self = shared_from_this();

			std::string oldId = nodeId.get();
			auto oldParent = myParent;
			SCOPE_EXIT{ nodeId = oldId; myParent = oldParent; };
			nodeId = a_newId;
			myParent = nullptr;

			// Serialize directly as shared_ptr (bypasses to_json wrapper which adds incompatible outer object)
			auto* engine = a_services.get<jai::engine>();
			jai::serialization::json_archive_writer ar(0, engine);  // indent=0: compact JSON
			ar(self);
			writeToFile(a_filename, ar.str());
			return self;
		}

		std::shared_ptr<Node> Node::loadJai(const std::string &a_filename, MV::Services& a_services, bool a_doPostLoadStep) {
			return loadJai(a_filename, a_services, "", a_doPostLoadStep);
		}

		std::shared_ptr<Node> Node::loadJai(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId, bool a_doPostLoadStep) {
			return load(a_filename, a_services, a_newNodeId, a_doPostLoadStep);
		}

		std::shared_ptr<Node> Node::loadChildJai(const std::string &a_filename, MV::Services& a_services, const std::string &a_newNodeId) {
			auto toAdd = Node::loadJai(a_filename, a_services, a_newNodeId, false);
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

		void Node::requireNotAncestor(const std::shared_ptr<Node> &a_child) const {
			for (const Node* ancestor = this; ancestor; ancestor = ancestor->myParent) {
				MV::require<RangeException>(ancestor != a_child.get(), "Adding [", a_child->id(), "] to [", id(), "] would result in a circular ownership!");
			}
		}

		void Node::adopt(const std::shared_ptr<Node> &a_child) {
			requireNotAncestor(a_child);
			a_child->myParent = this;
			if (a_child->ourCameraId != ourCameraId) {
				a_child->cameraIdInternal(ourCameraId);
			}
			a_child->onParentAlphaChangeSignal = onAlphaChangeSignal.connect([child = a_child.get()](const std::shared_ptr<Node> &) {
				child->recalculateAlpha();
			});
		}

		std::shared_ptr<Node> Node::add(const std::shared_ptr<Node> &a_child, bool a_overrideSortDepth) {
			requireNotAncestor(a_child);
			if(a_child->myParent != this){
				remove(a_child->id(), false);
				a_child->removeFromParent();
				if(a_overrideSortDepth){
					a_child->sortDepth = (childNodes->empty()) ? 0.0f : childNodes[childNodes->size() - 1]->sortDepth + 1.0f;
				}
				insertSorted(childNodes.get(), a_child);
				adopt(a_child);
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
			auto foundNode = std::find_if(childNodes->begin(), childNodes->end(), [&](const std::shared_ptr<Node> &a_child){
				return a_child->id() == a_id;
			});
			if(foundNode != childNodes->end()){
				auto self = shared_from_this();
				auto child = *foundNode;
				childNodes->erase(foundNode);
				child->onRemoveSignal(child);
				onChildRemoveSignal(self, child);
				return child;
			}
			require<ResourceException>(!a_throw, "Failed to remove: [", a_id, "] from parent node: [", nodeId.get(), "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::remove(const std::shared_ptr<Node> &a_child, bool a_throw) {
			auto foundNode = std::find_if(childNodes->begin(), childNodes->end(), [&](const std::shared_ptr<Node> &a_ourChild){
				return a_ourChild == a_child;
			});
			if(foundNode != childNodes->end()){
				auto self = shared_from_this();
				auto child = *foundNode;
				childNodes->erase(foundNode);
				child->onRemoveSignal(child);
				onChildRemoveSignal(self, child);
				return child;
			}
			require<ResourceException>(!a_throw, "Failed to remove: [", a_child->id(), "] from parent node: [", nodeId.get(), "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::clear() {
			auto self = shared_from_this();
			while(!childNodes->empty()){
				auto childToRemove = *childNodes->begin();
				childNodes->erase(childNodes->begin());
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
			require<ResourceException>(!a_throw, "Failed to getParent: [", a_id, "] from child node: [", nodeId.get(), "]");
			return nullptr;
		}

		std::shared_ptr<Node> Node::get(const std::string &a_id, bool a_throw) {
			auto foundNode = std::find_if(childNodes->begin(), childNodes->end(), [&](const std::shared_ptr<Node> &a_child){
				return a_child->id() == a_id;
			});
			if(foundNode != childNodes->end()){
				return *foundNode;
			}
			for(auto&& child : childNodes){
				auto found = child->get(a_id, false);
				if(found){
					return found;
				}
			}
			require<ResourceException>(!a_throw, "Failed to get: [", a_id, "] from parent node: [", nodeId.get(), "]");
			return nullptr;
		}
		std::shared_ptr<Node> Node::getImmediate(const std::string &a_id, bool a_throw) {
			auto foundNode = std::find_if(childNodes->begin(), childNodes->end(), [&](const std::shared_ptr<Node> &a_child) {
				return a_child->id() == a_id;
			});
			if (foundNode != childNodes->end()) {
				return *foundNode;
			}
			require<ResourceException>(!a_throw, "Failed to get: [", a_id, "] from parent node: [", nodeId.get(), "]");
			return nullptr;
		}

		bool Node::has(const std::string &a_id) const {
			auto foundNode = std::find_if(childNodes->begin(), childNodes->end(), [&](const std::shared_ptr<Node> &a_child) {
				return a_child->id() == a_id;
			});
			if (foundNode != childNodes->end()) {
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
			auto foundNode = std::find_if(childNodes->begin(), childNodes->end(), [&](const std::shared_ptr<Node> &a_child) {
				return a_child->id() == a_id;
			});
			return foundNode != childNodes->end();
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
			auto newRotationRadians = toRadians(a_newRotation);
			if (newRotationRadians != rotateTo) {
				rotateTo = newRotationRadians;
				onTransformChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Node> Node::rotationRad(const AxisAngles &a_newRotation) {
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
			auto originalAlpha = nodeAlpha.get();
			nodeAlpha = a_alpha;
			recalculateAlpha();
			if (!equals(originalAlpha, a_alpha)) {
				onAlphaChangeSignal(self);
			}
			return self;
		}

		std::optional<size_t> Node::indexOf(const std::shared_ptr<const Node> &a_childItem) const {
			auto found = std::find(childNodes->cbegin(), childNodes->cend(), a_childItem);
			if (found == childNodes->cend()) {
				return std::nullopt;
			}
			return static_cast<size_t>(std::distance(childNodes->cbegin(), found));
		}

		int64_t Node::myIndex() const {
			if (!myParent) {
				return 0;
			}
			auto index = myParent->indexOf(shared_from_this());
			return index ? static_cast<int64_t>(*index) : -1;
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
				if(!localBounds->empty() && !localChildBounds->empty()){
					return BoxAABB<>(localChildBounds).expandWith(localBounds);
				} else if(!localChildBounds->empty()){
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
			auto oldBounds = localBounds.get();
			{
				dirtyLocalBounds = false;

				if (!childComponents->empty()) {
					localBounds = childComponents[0]->bounds();
					for (size_t i = 1; i < childComponents->size(); ++i) {
						auto componentBounds = childComponents[i]->bounds();
						if (!componentBounds.empty()) {
							localBounds->expandWith(componentBounds);
						}
					}
				}
				else {
					localBounds = BoxAABB<>();
				}
			}
			if (localBounds.get() != oldBounds) {
				markParentBoundsDirty();
			}
		}

		void Node::recalculateChildBounds() {
			recalculateChildBoundsCalls++;
			{
				dirtyChildBounds = false;
				localChildBounds = BoxAABB<>();
				for (auto&& child : childNodes) {
					if (child->selfVisible()) {
						auto nodeBounds = child->bounds();
						if (nodeBounds.empty()) {
							continue;
						}
						if (localChildBounds->empty()) {
							localChildBounds = nodeBounds;
						} else {
							localChildBounds->expandWith(nodeBounds);
						}
					}
				}
			}
			markParentBoundsDirty();
		}

		// Rebuild ONLY the local TRS matrix (parent-independent). Safe to call during an
		// override-parent draw: it never touches worldMatrixTransform, so it cannot pollute it
		// with the temporary clip-space parent transform.
		void Node::recalculateLocalMatrix() {
			if (localMatrixDirty) {
				localMatrixDirty = false;
				localMatrixTransform.makeIdentity();

				localMatrixTransform.position(translateTo);

				if (rotateTo != 0.0f) {
					localMatrixTransform.setRotationXYZ(rotateTo);
				}

				if (scaleTo != 1.0f) {
					localMatrixTransform.scale(scaleTo->x, scaleTo->y, scaleTo->z);
				}

				markParentBoundsDirty();
			}
		}

		void Node::recalculateMatrix() {
			bool eitherMatrixUpdated = localMatrixDirty || worldMatrixDirty;
			recalculateLocalMatrix();

			if (eitherMatrixUpdated) {
				recalculateMatrixCalls++;
				worldMatrixDirty = false;
				if (myParent) {
					// Both operands are affine TRS transforms (built only via makeIdentity +
					// position + setRotationXYZ + scale), so the affine multiply is bit-identical
					// to the generic 4x4 product here while skipping the known [0 0 0 1] 4th row.
					worldMatrixTransform = affineMultiply(myParent->worldTransform(), localMatrixTransform);
					parentAccumulatedAlpha = myParent->parentAccumulatedAlpha * nodeAlpha;
				} else {
					worldMatrixTransform = localMatrixTransform;
					parentAccumulatedAlpha = nodeAlpha;
				}
			}
		}

		Node::Node(Draw2D &a_draw2d, const std::string &a_id) :
			draw2d(a_draw2d),
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
			onMatrixDirty(onMatrixDirtySignal),
			onComponentUpdate(onComponentUpdateSignal),
			onAttach(onAttachSignal),
			onDetach(onDetachSignal){

			nodeId = a_id;
			worldMatrixTransform.makeIdentity();
			auto blockOnChange = std::make_shared<bool>(false);
			onAdd.connect("SELF", [&, blockOnChange](const std::shared_ptr<Node> &a_self) {
				{
					*blockOnChange = true;
					SCOPE_EXIT{ *blockOnChange = false; };
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

			auto recordOnce = [](auto &a_silenced, const auto &a_item) {
				if (std::find(a_silenced.begin(), a_silenced.end(), a_item) == a_silenced.end()) {
					a_silenced.push_back(a_item);
				}
			};
			onChildRemoveSignal.set_blocked_callback([this, recordOnce](const std::shared_ptr<Node> &, const std::shared_ptr<Node> &a_child) {
				recordOnce(silencedChildRemovals, a_child);
			});
			onChildOrderChangeSignal.set_blocked_callback([this, recordOnce](const std::shared_ptr<Node> &, const std::shared_ptr<Node> &a_child) {
				recordOnce(silencedChildOrderChanges, a_child);
			});
			onComponentUpdateSignal.set_blocked_callback([this, recordOnce](const std::shared_ptr<Component> &a_component) {
				recordOnce(silencedComponentUpdates, a_component);
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

		Node::SerializableChildrenScope::SerializableChildrenScope(Node& a_node) :
			node(a_node),
			originalChildNodes(a_node.childNodes.get()),
			originalChildComponents(a_node.childComponents.get()) {
			std::vector<std::shared_ptr<Node>> filteredChildren;
			std::copy_if(originalChildNodes.begin(), originalChildNodes.end(), std::back_inserter(filteredChildren),
				[](const auto& child) { return child->serializable(); });
			std::vector<std::shared_ptr<Component>> filteredComponents;
			std::copy_if(originalChildComponents.begin(), originalChildComponents.end(), std::back_inserter(filteredComponents),
				[](const auto& comp) { return comp->serializable(); });
			node.childNodes = filteredChildren;
			node.childComponents = filteredComponents;
		}

		Node::SerializableChildrenScope::~SerializableChildrenScope() {
			node.childNodes = originalChildNodes;
			node.childComponents = originalChildComponents;
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
				adopt(childNode);
			}
			// The enclosing node IS the owner; never trust a deserialized componentOwner
			// (conversion-era assets carry null/stale refs — a dead owner throws on first
			// component->owner() call).
			auto self = shared_from_this();
			for (auto &&childComponent : childComponents) {
				if (childComponent->componentOwner->lock() != self) {
					childComponent->reattached(self);
				}
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

			localMatrixTransform.position(translateTo);

			if (rotateTo != 0.0f) {
				localMatrixTransform.setRotationXYZ(rotateTo);
			}
			if (scaleTo != 1.0f) {
				localMatrixTransform.scale(scaleTo->x, scaleTo->y, scaleTo->z);
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
			fixChildOwnership();
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
			return position((myParent != nullptr) ? myParent->localFromScreen(a_newPosition) : renderer().worldFromScreen(a_newPosition));
		}

		std::shared_ptr<Node> Node::worldPosition(const Point<> &a_newPosition) {
			return position((myParent != nullptr) ? myParent->localFromWorld(a_newPosition) : a_newPosition);
		}

		std::shared_ptr<Node> Node::translate(const Point<> &a_newPosition) {
			return position(position() + a_newPosition);
		}

		std::shared_ptr<Node> Node::worldTranslate(const Point<>& a_newPosition) {
			auto localDelta = (myParent != nullptr) ? myParent->localFromWorld(a_newPosition) : a_newPosition;
			return position(position() + localDelta);
		}

		std::shared_ptr<Node> Node::screenTranslate(const Point<int>& a_newPosition) {
			auto localDelta = (myParent != nullptr) ? myParent->localFromScreen(a_newPosition) : renderer().worldFromScreen(a_newPosition);
			return position(position() + localDelta);
		}

		MV::AxisAngles Node::worldRotationRad() const {
			auto accumulatedRotation = rotateTo.get();
			const Node* current = this;
			while ( (current = current->myParent) ) {
				accumulatedRotation += current->rotateTo.get();
			}
			return accumulatedRotation;
		}

		std::shared_ptr<Node> Node::worldRotationRad(const AxisAngles &a_newAngle) {
			auto accumulatedRotation = a_newAngle;
			const Node* current = this;
			while ( (current = current->myParent) ) {
				accumulatedRotation -= current->rotateTo.get();
			}
			return rotationRad(accumulatedRotation);
		}

		MV::AxisAngles Node::worldRotation() const {
			return toDegrees(worldRotationRad());
		}

		std::shared_ptr<Node> Node::worldRotation(const AxisAngles &a_newAngle) {
			return worldRotationRad(toRadians(a_newAngle));
		}

		MV::Scale Node::worldScale() const {
			auto accumulatedScale = scaleTo.get();
			const Node* current = this;
			while ( (current = current->myParent) ) {
				accumulatedScale *= current->scaleTo.get();
			}
			return accumulatedScale;
		}

		std::shared_ptr<Node> Node::worldScale(const Scale &a_newScale) {
			auto accumulatedScale = a_newScale;
			const Node* current = this;
			while ( (current = current->myParent) ) {
				accumulatedScale /= current->scaleTo.get();
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
			onMatrixDirtySignal(shared_from_this());
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
			std::shared_ptr<Node> result = cloneInternal(newNodeId, nullptr);

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

		std::shared_ptr<Node> Node::cloneInternal(const std::string &a_newId, const std::shared_ptr<Node> &a_parent) {
			std::shared_ptr<Node> result = Node::make(draw2d, a_newId);
			visit_reflected_properties([&](const std::string &a_name, jai::property_base *a_property) {
				if (a_name == "childNodes" || a_name == "childComponents" || a_name == "nodeId") {
					return;
				}
				a_property->clone_to_target(*result->find_reflected_property(a_name));
			});
			result->allowSerialize = allowSerialize;
			result->localMatrixDirty = true;
			result->worldMatrixDirty = true;
			if (a_parent) {
				a_parent->adopt(result);
			}

			for (auto&& childNode : childNodes) {
				if (childNode->serializable()) {
					result->childNodes->push_back(childNode->cloneInternal(childNode->nodeId.get(), result));
				}
			}

			for (auto&& childComponent : childComponents) {
				if (childComponent->serializable()) {
					childComponent->cloneWithoutPostLoad(result);
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
			auto unblock = [&](auto &a_signal, bool a_stateMatches = true) {
				bool calledWhileBlocked = a_signal.unblock() && a_stateMatches;
				changed = changed || calledWhileBlocked;
				return calledWhileBlocked;
			};
			auto replay = [&](auto &a_signal, auto&&... a_args) {
				if (!a_callBatched) {
					return;
				}
				try {
					a_signal(a_args...);
				} catch (std::exception &e) {
					std::cerr << "Node::unsilenceInternal callback exception: " << e.what() << std::endl;
				}
			};

			if (unblock(onChildAddSignal)) {
				replay(onChildAddSignal, self);
			}
			if (unblock(onChildRemoveSignal)) {
				for (auto&& child : std::exchange(silencedChildRemovals, {})) {
					replay(onChildRemoveSignal, self, child);
				}
			}
			if (unblock(onChildOrderChangeSignal)) {
				for (auto&& child : std::exchange(silencedChildOrderChanges, {})) {
					replay(onChildOrderChangeSignal, self, child);
				}
			}

			if (unblock(onAddSignal)) {
				replay(onAddSignal, self);
			}
			if (unblock(onRemoveSignal)) {
				replay(onRemoveSignal, self);
			}

			if (unblock(onEnableSignal, allowUpdate && allowDraw)) {
				replay(onEnableSignal, self);
			}
			if (unblock(onDisableSignal, !allowDraw && !allowUpdate)) {
				replay(onDisableSignal, self);
			}
			if (unblock(onPauseSignal, !allowUpdate)) {
				replay(onPauseSignal, self);
			}
			if (unblock(onResumeSignal, allowUpdate)) {
				replay(onResumeSignal, self);
			}
			if (unblock(onShowSignal, allowDraw)) {
				replay(onShowSignal, self);
			}
			if (unblock(onHideSignal, !allowDraw)) {
				replay(onHideSignal, self);
			}

			if (unblock(onBoundsRequestSignal)) {
				replay(onBoundsRequestSignal, self);
			}

			if (unblock(onTransformChangeSignal)) {
				replay(onTransformChangeSignal, self);
			}
			if (unblock(onLocalBoundsChangeSignal)) {
				replay(onLocalBoundsChangeSignal, self);
			}
			if (unblock(onChildBoundsChangeSignal)) {
				replay(onChildBoundsChangeSignal, self);
			}
			if (unblock(onOrderChangeSignal)) {
				replay(onOrderChangeSignal, self);
			}
			if (unblock(onAlphaChangeSignal)) {
				replay(onAlphaChangeSignal, self);
			}

			if (unblock(onComponentUpdateSignal)) {
				for (auto&& component : std::exchange(silencedComponentUpdates, {})) {
					replay(onComponentUpdateSignal, component);
				}
			}

			onChangeSignal.unblock();
			if (changed && a_callChanged) {
				safeOnChange();
			}
		}

		std::ostream& operator<<(std::ostream& os, const std::shared_ptr<Node>& a_node) {
			std::string spacing(a_node->parentIndexList().size(), ' ');
			os << spacing << "|->(" << a_node->id() << ")[" << a_node->size() << "]: L:" << a_node->position() << " | W:" << a_node->worldPosition() << " | S:" << a_node->screenPosition() << "\n";
			for (auto&& child : *a_node) {
				if (child->serializable()){
					os << child;
				}
			}
			return os;
		}
	}
}
