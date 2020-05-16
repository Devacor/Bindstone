#ifndef __MV_CREATURE_H__
#define __MV_CREATURE_H__

#include "MV/Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/signal.hpp"
#include <string>
#include <memory>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include "Game/Instance/team.h"
#include "Game/standardScriptMethods.h"

#include "MV/Network/dynamicVariable.h"
#include "MV/Network/networkObject.h"

class GameInstance;
struct InGamePlayer;
struct CreatureData;
class Creature;

struct CreatureData {
	std::string id;

	std::string name;
	std::string description;

	float moveSpeed = 0.0f;
	float actionSpeed = 0.0f;
	float castSpeed = 0.0f;

	int health = 0;
	float defense = 0.0f;
	float will = 0.0f;
	float strength = 0.0f;
	float ability = 0.0f;

	bool isServer = false;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(id),
			CEREAL_NVP(name),
			CEREAL_NVP(description),
			CEREAL_NVP(moveSpeed),
			CEREAL_NVP(actionSpeed),
			CEREAL_NVP(castSpeed),
			CEREAL_NVP(health),
			CEREAL_NVP(defense),
			CEREAL_NVP(will),
			CEREAL_NVP(strength),
			CEREAL_NVP(ability)
		);
	}

	StandardScriptMethods<Creature>& script(MV::Script&a_script) const {
		return scriptMethods.loadScript(a_script, "Creatures", id, isServer);
	}

private:
	mutable StandardScriptMethods<Creature> scriptMethods;
};

#define CREATURE_CATALOG_VERSION 0
#define BUILDING_CATALOG_VERSION 0

template <typename DataType>
class Catalog {
	friend cereal::access;
	friend MV::Script;
public:
	Catalog<DataType>(const std::string &a_catalogType, bool a_isServer, std::uint32_t a_serializeVersion = 0) :
		isServer(a_isServer) {

		std::string path = "Catalogs/"s + a_catalogType + ".json"s;
		std::string contents = MV::fileContents(path);
        MV::require<MV::ResourceException>(!contents.empty(), "Failed to load Catalog: ", a_catalogType);

		std::stringstream stream(contents);
		cereal::JSONInputArchive archive(stream);
		serialize(archive, a_serializeVersion);
	}

	const DataType& data(const std::string &a_id) const {
		for (auto&& item : dataCollection) {
			if (item.id == a_id) {
				return item;
			}
		}

		MV::require<MV::ResourceException>(false, "Failed to locate : ", a_id);
		throw; //suppress no return warning.
	}
private:
	Catalog< DataType>() {
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(
			cereal::make_nvp("data", dataCollection)
		);
		for (auto&& item : dataCollection) {
			item.isServer = isServer;
		}
	}
	bool isServer;
	std::vector<DataType> dataCollection;
};

class ServerCreature;
class TargetPolicy {
	friend MV::Script;
public:
	TargetPolicy(ServerCreature *a_self);

	~TargetPolicy();

	void target(int64_t a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail = std::function<void(TargetPolicy &)>());

	void target(const MV::Point<> &a_location, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail = std::function<void(TargetPolicy &)>());

	std::shared_ptr<ServerCreature> self() const;

	bool active() const;

	void update(double a_dt);

	void stun(float a_duration);

	void root(float a_duration);

	bool stunned() const {
		return stunDuration > 0.0f;
	}

	bool rooted() const {
		return rootDuration > 0.0f;
	}
private:
	void clearTarget();

	void stopAndFail();

	void callSucceed();

	void callFail();

	void clearCallbacks();

	void registerPathfindingListeners();

	ServerCreature* selfCreature = nullptr;
	ServerCreature* targetCreature = nullptr;

	float range = 0.0f;
	bool arrived = false;
	std::function<void(TargetPolicy &)> succeed;
	std::function<void(TargetPolicy &)> fail;

	float stunDuration = 0.0f;
	float rootDuration = 0.0f;

	std::shared_ptr<MV::Receiver<void(std::shared_ptr<Creature>)>> arriveReceiver;
	std::shared_ptr<MV::Receiver<void(std::shared_ptr<Creature>)>> blockedReceiver;
	std::shared_ptr<MV::Receiver<void(std::shared_ptr<Creature>)>> stopReceiver;

	std::shared_ptr<MV::Receiver<void(std::shared_ptr<Creature>)>> selfDeathReceiver;
	std::shared_ptr<MV::Receiver<void(std::shared_ptr<Creature>)>> targetDeathReceiver;
};

struct CreatureNetworkState {
	int64_t netId = 0;
	bool dying = false;

	std::function<void()> onNetworkDeath;
	std::function<void()> onNetworkSynchronize;
	std::function<void()> onAnimationChanged;

	MV::DeltaVariable<std::string> creatureTypeId;
	MV::DeltaVariable<int32_t> buildingSlot;

	MV::DeltaVariable<int32_t> health = 0;

	MV::DeltaVariable<MV::Point<MV::PointPrecision>> position;

	MV::DeltaVariable<std::string> animationName;

	bool animationLoops = false;
	double animationTime = 0.0;

	MV::DeltaVariable<std::map<std::string, MV::DynamicVariable>> variables;

	CreatureNetworkState() {
	}

	CreatureNetworkState(const CreatureData& a_template, int32_t a_buildingSlot) :
		creatureTypeId(a_template.id),
		buildingSlot(a_buildingSlot),
		health(a_template.health){
	}

	void synchronize(std::shared_ptr<CreatureNetworkState> a_other) {
		variables = a_other->variables;
		health = a_other->health;
		position = a_other->position;
		bool newAnimationBasedOnTime = a_other->animationTime < animationTime;
		animationTime = a_other->animationTime;
		if (animationName != a_other->animationName || animationLoops != a_other->animationLoops || newAnimationBasedOnTime) {
			animationName = a_other->animationName;
			animationLoops = a_other->animationLoops;
			if (onAnimationChanged) {
				onAnimationChanged();
			}
		}

		if (onNetworkSynchronize) {
			onNetworkSynchronize();
		}
	}

	void destroy(std::shared_ptr<CreatureNetworkState> a_other) {
		std::cout << "Killing: " << creatureTypeId << std::endl;
		dying = true;
		position = a_other->position;
		animationName = a_other->animationName;
		animationLoops = a_other->animationLoops;
		animationTime = a_other->animationTime;
		variables = a_other->variables;
		if (onNetworkDeath) {
			onNetworkDeath();
		} else {
			MV::warning("Failed to call onNetworkDeath for [", creatureTypeId, "]!");
		}
	}

	template <class Archive>
	void serialize(Archive& archive, std::uint32_t const /*version*/) {
		creatureTypeId.serialize(archive, "creatureTypeId");
		health.serialize(archive, "health");
		animationName.serialize(archive, "animationName");
		archive(
			cereal::make_nvp("animationTime", animationTime),
			cereal::make_nvp("animationLoops", animationLoops)
		);
		buildingSlot.serialize(archive, "slot");
		position.serialize(archive, "position");
		variables.serialize(archive, "variables");
	}
};

class Creature : public MV::Scene::Component {
	friend MV::Script;
public:
	typedef void CallbackSignature(std::shared_ptr<Creature>);
	typedef MV::SignalRegister<CallbackSignature>::SharedReceiverType SharedReceiverType;

protected:
	MV::Signal<CallbackSignature> onStatusSignal;
	MV::Signal<void(std::shared_ptr<Creature>, int)> onHealthChangeSignal;
	MV::Signal<CallbackSignature> onDeathSignal;
	MV::Signal<CallbackSignature> onFallSignal;

public:
	MV::SignalRegister<CallbackSignature> onStatus;
	MV::SignalRegister<void(std::shared_ptr<Creature>, int)> onHealthChange;
	MV::SignalRegister<CallbackSignature> onDeath;
	MV::SignalRegister<CallbackSignature> onFall;

	ComponentDerivedAccessors(Creature)

	~Creature() { MV::info("Creature Died: [", netId(), "]"); }

	std::shared_ptr<InGamePlayer> player();

	std::string assetPath() const;

	bool alive() const {
		return !state->self()->dying;
	}

	virtual bool changeHealth(int amount) = 0;

	int64_t netId() const {
		return state->id();
	}

	int32_t buildingSlot() const {
		return *state->self()->buildingSlot;
	}

	std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> networkState() const {
		return state;
	}

	std::shared_ptr<MV::Scene::Spine> spine() {
		return spineAnimator;
	}

protected:
	Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const CreatureData& a_statTemplate, std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_state);

	std::string skin;

	GameInstance& gameInstance;

	const CreatureData& statTemplate;

	std::map<std::string, chaiscript::Boxed_Value> localVariables;

	std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> state;

	std::shared_ptr<MV::Scene::Spine> spineAnimator;
};

class ServerCreature : public Creature {
	friend MV::Scene::Node;
	friend cereal::access;
	friend TargetPolicy;
	friend MV::Script;
private:

	MV::Signal<CallbackSignature> onArriveSignal;
	MV::Signal<CallbackSignature> onBlockedSignal;
	MV::Signal<CallbackSignature> onStopSignal;
	MV::Signal<CallbackSignature> onStartSignal;

public:
	MV::SignalRegister<CallbackSignature> onArrive;
	MV::SignalRegister<CallbackSignature> onBlocked;
	MV::SignalRegister<CallbackSignature> onStop;
	MV::SignalRegister<CallbackSignature> onStart;

	ComponentDerivedAccessors(ServerCreature)

	void fall() {
		auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
		
		onFallSignal(self);
		flagForDeath();
	}

	//return true if alive
	virtual bool changeHealth(int amount) override {
		if (alive()) {
			auto health = state->self()->health;
			auto newHealth = std::max(std::min(*health + amount, statTemplate.health), 0);
			amount = *state->self()->health - newHealth;

			if (health != newHealth) {
				state->modify()->health = newHealth;
				auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
				onHealthChangeSignal(self, amount);
			}

			if (newHealth <= 0) {
				flagForDeath();
				return false;
			}
			return true;
		} else {
			return false;
		}
	}

	std::shared_ptr<MV::Scene::PathAgent> agent() {
		return pathAgent;
	}

protected:
	ServerCreature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, int a_buildingSlot, GameInstance& a_gameInstance);
	ServerCreature(const std::weak_ptr<MV::Scene::Node> &a_owner, const CreatureData& a_statTemplate, int a_buildingSlot, GameInstance& a_gameInstance);
	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<ServerCreature>(statTemplate.id, buildingSlot(), gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<ServerCreature>(a_clone);
		return a_clone;
	}

private:

	void flagForDeath() {
		if (!state->destroyed()) {
			auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
			state->self()->health = 0;
			state->self()->dying = true;
			state->self()->animationName = "die";
			state->self()->animationLoops = false;
			state->self()->animationTime = 0.0;
			state->destroy();

			animateDeathAndRemove();
		}
	}

	void animateDeathAndRemove() {
		auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
		agent()->stop();
		onDeathSignal(self);
		spineAnimator->animate(*state->self()->animationName, false);
		spineAnimator->onEnd.connect("!", [&](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
			if (a_self->track(a_track).name() == state->self()->animationName) {
				owner()->removeFromParent();
			}
		});
	}

	virtual void updateImplementation(double a_delta) override;

	std::shared_ptr<MV::Scene::PathAgent> pathAgent;

	TargetPolicy targeting;
};

class ClientCreature : public Creature {
	friend MV::Scene::Node;
	friend cereal::access;
	friend MV::Script;
public:
	ComponentDerivedAccessors(ClientCreature)

	//return true if alive
	virtual bool changeHealth(int amount) override {
		if (alive()) {
			auto health = state->self()->health;
			auto newHealth = std::max(std::min(*health + amount, statTemplate.health), 0);
			amount = *state->self()->health - newHealth;

			if (*health != newHealth) {
				state->modify()->health = newHealth;
				auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
				onHealthChangeSignal(self, amount);
			}

			if (newHealth <= 0) {
				return false;
			}
			return true;
		} else {
			return false;
		}
	}

protected:
	ClientCreature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> &a_state, GameInstance& a_gameInstance);

	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<ClientCreature>(state, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<ClientCreature>(a_clone);
		return a_clone;
	}

private:
	void animateDeathAndRemove() {
		auto self = std::static_pointer_cast<ServerCreature>(shared_from_this());
		onDeathSignal(self);
		task().cancel();
		if (owner()->position() != *state->self()->position) {
			task().now("Tween", [&](MV::Task&, double a_dt) {
				owner()->position(MV::moveToward(owner()->position(), *state->self()->position, static_cast<MV::PointPrecision>(a_dt) * 200.0f));
				return owner()->position() != *state->self()->position;
			});
		}
		task().then("animateAway", [&](MV::Task&) {
			spineAnimator->animate(*state->self()->animationName, false);
			spineAnimator->onEnd.connect("!", [&](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
				if (a_self->track(a_track).name() == state->self()->animationName) {
					owner()->removeFromParent();
				}
			});
		});
	}

	void onNetworkSynchronize();
	void onAnimationChanged();
	
	virtual void updateImplementation(double a_delta) override;

	MV::Point<> startClientPosition;
	double accumulatedDuration = 0.0f;
	MV::TimeDeltaAggregate networkDelta;
};

#endif
