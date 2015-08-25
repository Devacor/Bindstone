#ifndef _MV_SCENE_COLLIDER_H_
#define _MV_SCENE_COLLIDER_H_

// #include "drawable.h"
// #include "ArtificialIntelligence/pathfinding.h"

namespace MV {
	namespace Scene {
// 
// 		class Collider : public Drawable {
// 			friend Node;
// 			friend cereal::access;
// 		public:
// 			typedef void CallbackSignature(std::shared_ptr<PathAgent>);
// 			typedef SignalRegister<CallbackSignature>::SharedRecieverType SharedRecieverType;
// 		private:
// 			Signal<CallbackSignature> onArriveSignal;
// 			Signal<CallbackSignature> onBlockedSignal;
// 			Signal<CallbackSignature> onStopSignal;
// 			Signal<CallbackSignature> onStartSignal;
// 
// 		public:
// 			SignalRegister<CallbackSignature> onArrive;
// 			SignalRegister<CallbackSignature> onBlocked;
// 			SignalRegister<CallbackSignature> onStop;
// 			SignalRegister<CallbackSignature> onStart;
// 
// 			ComponentDerivedAccessors(PathMap)
// 
// 				Point<PointPrecision> gridPosition() const {
// 				return agent->position();
// 			}
// 
// 			std::vector<PathNode> path() {
// 				return agent->path();
// 			}
// 
// 			std::shared_ptr<PathAgent> gridPosition(const Point<int> &a_newPosition) {
// 				agent->position(a_newPosition);
// 				return std::static_pointer_cast<PathAgent>(shared_from_this());
// 			}
// 
// 			std::shared_ptr<PathAgent> gridPosition(const Point<> &a_newPosition) {
// 				agent->position(a_newPosition);
// 				return std::static_pointer_cast<PathAgent>(shared_from_this());
// 			}
// 
// 			std::shared_ptr<PathAgent> gridGoal(const Point<int> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
// 				agent->goal(a_newGoal, a_acceptableDistance);
// 				return std::static_pointer_cast<PathAgent>(shared_from_this());
// 			}
// 
// 			std::shared_ptr<PathAgent> gridGoal(const Point<> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
// 				agent->goal(a_newGoal, a_acceptableDistance);
// 				return std::static_pointer_cast<PathAgent>(shared_from_this());
// 			}
// 
// 			Point<PointPrecision> gridGoal() const {
// 				return agent->goal();
// 			}
// 
// 			PointPrecision gridSpeed() const {
// 				return agent->speed();
// 			}
// 
// 			std::shared_ptr<PathAgent> gridSpeed(PointPrecision a_newSpeed) {
// 				agent->speed(a_newSpeed);
// 				return std::static_pointer_cast<PathAgent>(shared_from_this());
// 			}
// 			bool pathfinding() const {
// 				return agent->pathfinding();
// 			}
// 
// 		protected:
// 			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<> &a_gridPosition) :
// 				Component(a_owner),
// 				map(a_map),
// 				agent(NavigationAgent::make(a_map->map, a_gridPosition)),
// 				onArrive(onArriveSignal),
// 				onBlocked(onBlockedSignal),
// 				onStop(onStopSignal),
// 				onStart(onStartSignal) {
// 			}
// 
// 			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<int> &a_gridPosition) :
// 				Component(a_owner),
// 				map(a_map),
// 				agent(NavigationAgent::make(a_map->map, a_gridPosition)),
// 				onArrive(onArriveSignal),
// 				onBlocked(onBlockedSignal),
// 				onStop(onStopSignal),
// 				onStart(onStartSignal) {
// 			}
// 
// 			virtual void updateImplementation(double a_dt) override {
// 				agent->update(a_dt);
// 				applyAgentPositionToOwner();
// 			}
// 
// 			void applyAgentPositionToOwner() {
// 				auto ourOwner = owner();
// 				if (ourOwner->parent() == map->owner()) {
// 					ourOwner->position(map->localFromGrid(agent->position()));
// 				}
// 				else {
// 					ourOwner->worldPosition(map->owner()->worldFromLocal(map->localFromGrid(agent->position())));
// 				}
// 			}
// 
// 			template <class Archive>
// 			void serialize(Archive & a_archive) {
// 				a_archive(
// 					cereal::make_nvp("map", map),
// 					cereal::make_nvp("agent", agent),
// 					cereal::make_nvp("Component", cereal::base_class<Component>(this))
// 					);
// 			}
// 
// 			template <class Archive>
// 			static void load_and_construct(Archive & a_archive, cereal::construct<PathAgent> &a_construct) {
// 				a_construct(std::shared_ptr<Node>());
// 				a_archive(
// 					cereal::make_nvp("map", a_construct->map),
// 					cereal::make_nvp("agent", a_construct->agent),
// 					cereal::make_nvp("Component", cereal::base_class<Component>(a_construct.ptr()))
// 					);
// 				a_construct->initialize();
// 			}
// 
// 			virtual void initialize() override {
// 				applyAgentPositionToOwner();
// 				agentPassthroughSignals.push_back(agent->onArrive.connect([&](std::shared_ptr<NavigationAgent>) {
// 					onArriveSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
// 				}));
// 				agentPassthroughSignals.push_back(agent->onBlocked.connect([&](std::shared_ptr<NavigationAgent>) {
// 					onBlockedSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
// 				}));
// 				agentPassthroughSignals.push_back(agent->onStop.connect([&](std::shared_ptr<NavigationAgent>) {
// 					onStopSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
// 				}));
// 				agentPassthroughSignals.push_back(agent->onStart.connect([&](std::shared_ptr<NavigationAgent>) {
// 					onStartSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
// 				}));
// 			}
// 
// 			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
// 				return cloneHelper(a_parent->attach<PathAgent>(map, agent->position()).self());
// 			}
// 
// 			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
// 				Component::cloneHelper(a_clone);
// 				auto agentClone = std::static_pointer_cast<PathAgent>(a_clone);
// 				agentClone->map = map;
// 				agentClone->agent = agent->clone();
// 				return a_clone;
// 			}
// 		private:
// 			//only for loading
// 			PathAgent(const std::weak_ptr<Node> &a_owner) :
// 				Component(a_owner),
// 				map(nullptr),
// 				agent(nullptr),
// 				onArrive(onArriveSignal),
// 				onBlocked(onBlockedSignal),
// 				onStop(onStopSignal),
// 				onStart(onStartSignal) {
// 			}
// 			std::vector<NavigationAgent::SharedRecieverType> agentPassthroughSignals;
// 			std::shared_ptr<PathMap> map;
// 			std::shared_ptr<NavigationAgent> agent;
// 		};
	}
}

// CEREAL_CLASS_VERSION(MV::Scene::PathAgent, 1);
// CEREAL_CLASS_VERSION(MV::Scene::PathMap, 1);

#endif
