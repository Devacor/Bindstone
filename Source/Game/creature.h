#ifndef __MV_CREATURE_H__
#define __MV_CREATURE_H__

#include "Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "chaiscript/chaiscript.hpp"
#include "Utility/generalUtility.h"
#include "Utility/signal.hpp"
#include <string>
#include <memory>
#include "ArtificialIntelligence/pathfinding.h"

class GameInstance;
struct Player;
struct CreatureData;
class Creature;

struct CreatureScriptMethods {
	friend CreatureData;

	void spawn(std::shared_ptr<Creature> a_creature) const {
		if (scriptSpawn) { scriptSpawn(a_creature); }
	}

	void update(std::shared_ptr<Creature> a_creature, double a_dt) const {
		if (scriptUpdate) { scriptUpdate(a_creature, a_dt); }
	}

	void death(std::shared_ptr<Creature> a_creature) const {
		if (scriptDeath) { scriptDeath(a_creature); }
	}

	static chaiscript::ChaiScript& CreatureScriptMethods::hook(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<CreatureScriptMethods>(), "CreatureScriptMethods");

		a_script.add(chaiscript::fun(&CreatureScriptMethods::scriptSpawn), "spawn");
		a_script.add(chaiscript::fun(&CreatureScriptMethods::scriptUpdate), "update");
		a_script.add(chaiscript::fun(&CreatureScriptMethods::scriptDeath), "death");
		return a_script;
	}

	CreatureScriptMethods& loadScript(chaiscript::ChaiScript &a_script, const std::string &a_id);
private:
	std::function<void(std::shared_ptr<Creature>)> scriptSpawn;
	std::function<void(std::shared_ptr<Creature>, double)> scriptUpdate;
	std::function<void(std::shared_ptr<Creature>)> scriptDeath;

	std::string scriptContents = "NIL";
};

struct CreatureData {
	std::string id;

	std::string name;
	std::string description;

	float moveSpeed;
	float actionSpeed;
	float castSpeed;

	int health;
	float defense;
	float will;
	float strength;
	float ability;

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
		CreatureScriptMethods::hook(a_script);

		a_script.add(chaiscript::user_type<CreatureData>(), "CreatureData");

		a_script.add(chaiscript::fun(&CreatureData::id), "id");
		a_script.add(chaiscript::fun(&CreatureData::name), "name");

		a_script.add(chaiscript::fun(&CreatureData::description), "description");

		a_script.add(chaiscript::fun(&CreatureData::moveSpeed), "moveSpeed");
		a_script.add(chaiscript::fun(&CreatureData::actionSpeed), "actionSpeed");
		a_script.add(chaiscript::fun(&CreatureData::castSpeed), "castSpeed");

		a_script.add(chaiscript::fun(&CreatureData::health), "health");
		a_script.add(chaiscript::fun(&CreatureData::defense), "defense");
		a_script.add(chaiscript::fun(&CreatureData::will), "will");
		a_script.add(chaiscript::fun(&CreatureData::strength), "strength");
		a_script.add(chaiscript::fun(&CreatureData::ability), "ability");

		return a_script;
	}

	template <class Archive>
	void save(Archive & archive) const {
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

	template <class Archive>
	void load(Archive & archive) {
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

	CreatureScriptMethods& script(chaiscript::ChaiScript &a_script) const {
		return scriptMethods.loadScript(a_script, id);
	}

private:
	mutable CreatureScriptMethods scriptMethods;
};


class CreatureCatalog {
	friend cereal::access;
public:
	CreatureCatalog(const std::string &a_filename) {
		std::ifstream instream(a_filename);
		cereal::JSONInputArchive archive(instream);

		archive(cereal::make_nvp("creatures", creatureList));
	}

	const CreatureData& data(const std::string &a_id) const {
		for (auto&& creature : creatureList) {
			if (creature.id == a_id) {
				return creature;
			}
		}

		MV::require<MV::ResourceException>(false, "Failed to locate creature: ", a_id);
		return nullResult; //avoid warnings/errors
	}
private:
	CreatureCatalog() {
	}

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			cereal::make_nvp("creatures", creatureList)
		);
	}
	CreatureData nullResult;
	std::vector<CreatureData> creatureList;
};

class Creature;
class TargetPolicy {
public:
	TargetPolicy(Creature *a_self);

	~TargetPolicy();

	void target(Creature* a_target, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail = std::function<void(TargetPolicy &)>());

	void target(const MV::Point<> &a_location, float a_range, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail = std::function<void(TargetPolicy &)>());

	std::shared_ptr<Creature> self() const;

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

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
private:
	void clearTarget();

	void stopAndFail();

	void callSucceed();

	void callFail();

	void clearCallbacks();

	void registerPathfindingListeners();

	Creature* selfCreature;
	Creature* targetCreature;

	float range;
	bool arrived = false;
	std::function<void(TargetPolicy &)> succeed;
	std::function<void(TargetPolicy &)> fail;

	float stunDuration = 0.0f;
	float rootDuration = 0.0f;

	std::shared_ptr<MV::Reciever<void(std::shared_ptr<Creature>)>> arriveReceiver;
	std::shared_ptr<MV::Reciever<void(std::shared_ptr<Creature>)>> blockedReceiver;
	std::shared_ptr<MV::Reciever<void(std::shared_ptr<Creature>)>> stopReceiver;

	std::shared_ptr<MV::Reciever<void(std::shared_ptr<Creature>)>> selfDeathReceiver;
	std::shared_ptr<MV::Reciever<void(std::shared_ptr<Creature>)>> targetDeathReceiver;
};

class Creature : public MV::Scene::Component, private MV::InstanceCounter<Creature> {
	friend MV::Scene::Node;
	friend cereal::access;
public:
	typedef void CallbackSignature(std::shared_ptr<Creature>);
	typedef MV::SignalRegister<CallbackSignature>::SharedRecieverType SharedRecieverType;

private:
	MV::Signal<CallbackSignature> onArriveSignal;
	MV::Signal<CallbackSignature> onBlockedSignal;
	MV::Signal<CallbackSignature> onStopSignal;
	MV::Signal<CallbackSignature> onStartSignal;

	MV::Signal<CallbackSignature> onStatusSignal;
	MV::Signal<void(std::shared_ptr<Creature>, int)> onHealthChangeSignal;
	MV::Signal<CallbackSignature> onDeathSignal;
	MV::Signal<CallbackSignature> onFallSignal;
public:
	MV::SignalRegister<CallbackSignature> onArrive;
	MV::SignalRegister<CallbackSignature> onBlocked;
	MV::SignalRegister<CallbackSignature> onStop;
	MV::SignalRegister<CallbackSignature> onStart;

	MV::SignalRegister<CallbackSignature> onStatus;
	MV::SignalRegister<void(std::shared_ptr<Creature>, int)> onHealthChange;
	MV::SignalRegister<CallbackSignature> onDeath;
	MV::SignalRegister<CallbackSignature> onFall;

	ComponentDerivedAccessors(Creature)

	std::string assetPath() const;
	~Creature() { std::cout << "Creature died!" << std::endl; }

	using InstanceCounter<Creature>::activeInstanceCount;
	using InstanceCounter<Creature>::totalInstanceCount;

	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, GameInstance& gameInstance);

	bool alive() const {
		return !flaggedForDeath && health > 0;
	}

	void fall() {
		auto self = std::static_pointer_cast<Creature>(shared_from_this());
		
		onFallSignal(self);
		flagForDeath();
	}

	//return true if alive
	bool changeHealth(int amount) {
		if (!flaggedForDeath) {
			auto newHealth = std::max(std::min(health + amount, statTemplate.health), 0);
			amount = health - newHealth;

			if (health != newHealth) {
				health = newHealth;

				auto self = std::static_pointer_cast<Creature>(shared_from_this());
				onHealthChangeSignal(self, amount);
			}

			if (health <= 0) {
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
	Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const std::string &a_id, const std::string &a_skin, const std::shared_ptr<Player> &a_player, GameInstance& a_gameInstance);
	Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, const CreatureData &a_stats, const std::string &a_skin, const std::shared_ptr<Player> &a_player, GameInstance& a_gameInstance);

	virtual void initialize() override;

	virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
		return cloneHelper(a_parent->attach<Creature>(statTemplate, skin, owningPlayer, gameInstance).self());
	}

	virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
		Component::cloneHelper(a_clone);
		auto creatureClone = std::static_pointer_cast<Creature>(a_clone);
		return a_clone;
	}

private:

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			//CEREAL_NVP(shouldDraw),
			cereal::make_nvp("Component", cereal::base_class<Component>(this))
		);
	}

	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Creature> &construct) {
		construct(std::shared_ptr<Node>());
		archive(
			cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
		);
		construct->initialize();
	}

	void flagForDeath() {
		if (!flaggedForDeath) {
			auto self = std::static_pointer_cast<Creature>(shared_from_this());
			health = 0;
			flaggedForDeath = true;

			agent()->stop();
			onDeathSignal(self);
			auto spine = owner()->componentInChildren<MV::Scene::Spine>();
			spine->animate("die", false);
			spine->onEnd.connect("!", [&](std::shared_ptr<MV::Scene::Spine> a_self, int a_track) {
				owner()->removeFromParent();
			});
		}
	}

	virtual void updateImplementation(double a_delta) override;

	const CreatureData& statTemplate;
	std::string skin;

	int health;

	bool flaggedForDeath = false;

	std::shared_ptr<Player> owningPlayer;
	GameInstance& gameInstance;

	std::shared_ptr<MV::Scene::PathAgent> pathAgent;

	std::map<std::string, chaiscript::Boxed_Value> variables;

	TargetPolicy targeting;

	static size_t idCounter;
};

#endif