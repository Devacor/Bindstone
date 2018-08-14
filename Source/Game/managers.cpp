#include "managers.h"
#include "player.h"
#include "Instance/gameInstance.h"
#include "Interface/interfaceManager.h"
#include "Game/NetworkLayer/package.h"


void bindstoneScriptHook(chaiscript::ChaiScript &a_script, MV::TapDevice &a_tapDevice, MV::ThreadPool &a_pool) {
	MV::TexturePoint::hook(a_script);
	MV::Color::hook(a_script);
	MV::Size<MV::PointPrecision>::hook(a_script);
	MV::Size<int>::hook(a_script, "i");
	MV::Point<MV::PointPrecision>::hook(a_script);
	MV::Point<int>::hook(a_script, "i");
	MV::BoxAABB<MV::PointPrecision>::hook(a_script);
	MV::BoxAABB<int>::hook(a_script, "i");

	MV::TexturePack::hook(a_script);
	MV::TextureDefinition::hook(a_script);
	MV::FileTextureDefinition::hook(a_script);
	MV::TextureHandle::hook(a_script);
	MV::SharedTextures::hook(a_script);

	Wallet::hook(a_script);
	Player::hook(a_script);
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

	MV::Client::hook(a_script);
	CreatePlayer::hook(a_script);
	NetworkAction::hook(a_script);
	LoginRequest::hook(a_script);
	LoginResponse::hook(a_script);
	FindMatchRequest::hook(a_script);

	MV::InterfaceManager::hook(a_script);

	a_script.add(chaiscript::fun([](int a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](size_t a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](float a_from) {return MV::to_string(a_from); }), "to_string");
	a_script.add(chaiscript::fun([](double a_from) {return MV::to_string(a_from); }), "to_string");
}
