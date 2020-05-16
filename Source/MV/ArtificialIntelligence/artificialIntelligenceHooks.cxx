#include "pathfinding.h"

namespace MV {

	Script::Registrar<PathNode> _hookPathNode([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<PathNode>(), "PathNode");
		a_script.add(chaiscript::constructor<PathNode(const Point<int> &, float)>(), "PathNode");
		a_script.add(chaiscript::fun(&PathNode::position), "position");
		a_script.add(chaiscript::fun(&PathNode::cost), "cost");
	});

	Script::Registrar<NavigationAgent> _hookNavigationAgent([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<NavigationAgent>(), "NavigationAgent");
		a_script.add(chaiscript::fun(&NavigationAgent::pathfinding), "pathfinding");
		a_script.add(chaiscript::fun(&NavigationAgent::stop), "stop");
		a_script.add(chaiscript::fun(&NavigationAgent::path), "path");
		a_script.add(chaiscript::fun(&NavigationAgent::hasFootprint), "hasFootprint");
		a_script.add(chaiscript::fun(&NavigationAgent::disableFootprint), "disableFootprint");
		a_script.add(chaiscript::fun(&NavigationAgent::enableFootprint), "enableFootprint");
		a_script.add(chaiscript::fun(static_cast<PointPrecision(NavigationAgent::*)()const>(&NavigationAgent::speed)), "speed");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(PointPrecision)>(&NavigationAgent::speed)), "speed");

		a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(NavigationAgent::*)()const>(&NavigationAgent::goal)), "goal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(const Point<> &)>(&NavigationAgent::goal)), "goal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(const Point<int> &)>(&NavigationAgent::goal)), "goal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(const Point<> &, PointPrecision)>(&NavigationAgent::goal)), "goal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(const Point<int> &, PointPrecision)>(&NavigationAgent::goal)), "goal");

		a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(NavigationAgent::*)()const>(&NavigationAgent::position)), "position");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(const Point<> &)>(&NavigationAgent::position)), "position");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<NavigationAgent>(NavigationAgent::*)(const Point<int> &)>(&NavigationAgent::position)), "position");
	});

}