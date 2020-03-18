#include "managers.h"
#include "player.h"
#include "Instance/gameInstance.h"
#include "Game/Interface/interfaceManager.h"
#include "Game/NetworkLayer/package.h"


void bindstoneScriptHook(chaiscript::ChaiScript &a_script, MV::TapDevice &a_tapDevice, MV::ThreadPool &a_pool) {
	//Helps cereal performance for basic conversions to list them explicitly in here:
	a_script.add(chaiscript::type_conversion<int, float>());
	a_script.add(chaiscript::type_conversion<int, double>());
	a_script.add(chaiscript::type_conversion<int, long>());
	a_script.add(chaiscript::type_conversion<float, double>());
	a_script.add(chaiscript::type_conversion<int, int32_t>());
	a_script.add(chaiscript::type_conversion<int, int64_t>());
	a_script.add(chaiscript::type_conversion<int16_t, int32_t>());
	a_script.add(chaiscript::type_conversion<int16_t, int64_t>());
	a_script.add(chaiscript::type_conversion<int32_t, int64_t>());

	//Narrowing conversions added too, because they'll still happen in the language but at lest we can make them more efficient:
	a_script.add(chaiscript::type_conversion<double, float>());
	a_script.add(chaiscript::type_conversion<long, int>());
	a_script.add(chaiscript::type_conversion<int32_t, int>());
	a_script.add(chaiscript::type_conversion<int64_t, int>());
	a_script.add(chaiscript::type_conversion<int32_t, int16_t>());
	a_script.add(chaiscript::type_conversion<int64_t, int16_t>());
	a_script.add(chaiscript::type_conversion<int64_t, int32_t>());

	a_script.add(chaiscript::fun([](int a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](size_t a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](float a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](double a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](bool a_from) {return MV::to_string(a_from); }), "to_string");
	
	a_script.add(chaiscript::fun([](const std::string& a_output) {
		MV::info("CHAISCRIPT: ", a_output);
	}), "log_chaiscript_output");
	a_script.eval("global print = fun(x){ log_chaiscript_output(to_string(x)); };");
	
	MV::TexturePoint::hook(a_script);
	MV::Color::hook(a_script);
	MV::Size<MV::PointPrecision>::hook(a_script);
	MV::Size<int>::hook(a_script, "i");
	MV::Point<MV::PointPrecision>::hook(a_script);
	MV::Point<int>::hook(a_script, "i");
	MV::BoxAABB<MV::PointPrecision>::hook(a_script);
	MV::BoxAABB<int>::hook(a_script, "i");

	MV::DynamicVariable::hook(a_script);

	MV::TexturePack::hook(a_script);
	MV::TextureDefinition::hook(a_script);
	MV::FileTextureDefinition::hook(a_script);
	MV::TextureHandle::hook(a_script);
	MV::SharedTextures::hook(a_script);

	Wallet::hook(a_script);
	InGamePlayer::hook(a_script);
	Team::hook(a_script);
	MV::Task::hook(a_script);
	GameData::hook(a_script);

	MV::PathNode::hook(a_script);
	MV::NavigationAgent::hook(a_script);

	MV::Scene::Node::hook(a_script);
	MV::Scene::Component::hook(a_script);
	MV::Scene::Drawable::hook(a_script);
	MV::Scene::Sprite::hook(a_script);
	MV::Scene::Spine::hook(a_script);
	MV::Scene::Text::hook(a_script);
	MV::Scene::PathMap::hook(a_script);
	MV::Scene::PathAgent::hook(a_script);
	MV::Scene::Emitter::hook(a_script, a_pool);
	MV::Scene::Clickable::hook(a_script, a_tapDevice);
	MV::Scene::Button::hook(a_script, a_tapDevice);

	MV::Client::hook(a_script);
	CreatePlayer::hook(a_script);
	NetworkAction::hook(a_script);
	LoginRequest::hook(a_script);
	LoginResponse::hook(a_script);
	FindMatchRequest::hook(a_script);

	MV::InterfaceManager::hook(a_script);
}

void StandardMessages::hook(chaiscript::ChaiScript& a_script) {
	a_script.add_global(chaiscript::var(this), "messages");
	MV::SignalRegister<void()>::hook(a_script);
	MV::SignalRegister<void(const std::string &)>::hook(a_script);
	MV::SignalRegister<void(bool, const std::string &)>::hook(a_script);
	a_script.add(chaiscript::fun(&StandardMessages::lobbyConnected), "lobbyConnected");
	a_script.add(chaiscript::fun(&StandardMessages::lobbyDisconnect), "lobbyDisconnect");
	a_script.add(chaiscript::fun(&StandardMessages::lobbyAuthenticated), "lobbyAuthenticated");
}
