#ifndef __MV_CREATURE_H__
#define __MV_CREATURE_H__

#include "MV/Render/package.h"
#include "Game/wallet.h"
#include "Game/Interface/guiFactories.h"
#include "MV/Utility/generalUtility.h"
#include <jaiscript/signals/signal.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/core/engine.hpp>
#include <string>
#include <memory>
#include <unordered_map>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include "Game/Instance/team.h"
#include "Game/standardScriptMethods.h"
#include "Game/elements.h"

#include "MV/Network/dynamicVariable.h"
#include "MV/Network/networkObject.h"

class GameInstance;
struct InGamePlayer;
struct CreatureData;
class Creature;

namespace jai { template<typename T> class dynamic_binder; }

struct CreatureData {
	std::string id;

	std::string name;
	std::string description;

	float moveSpeed = 0.0f;
	float actionSpeed = 0.0f;
	float castSpeed = 0.0f;
	float attackInterval = 0.0f;   // seconds between attacks (0 = the attack animation's own length); statuses multiply on top
	float attackRange = 0.0f;   // cells; 0 = no attack

	int health = 0;
	float defense = 0.0f;
	float will = 0.0f;
	float strength = 0.0f;
	float ability = 0.0f;

	// every type the unit carries; multipliers multiply across them (an earth+metal unit takes
	// water x earth and water x metal from a water hit)
	std::vector<std::string> elements;

	bool isServer = false;

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(id), JAI_NVP(name), JAI_NVP(description),
			JAI_NVP(moveSpeed), JAI_NVP(actionSpeed), JAI_NVP(castSpeed), JAI_NVP(attackInterval), JAI_NVP(attackRange),
			JAI_NVP(health), JAI_NVP(defense), JAI_NVP(will),
			JAI_NVP(strength), JAI_NVP(ability), JAI_NVP(elements));
	}

	StandardScriptMethods<Creature>& script(MV::Script&a_script) const {
		return scriptMethods.loadScript(a_script, "Creatures", id, isServer);
	}

private:
	mutable StandardScriptMethods<Creature> scriptMethods;
};

template <typename DataType>
class Catalog {
	friend jai::access;
	friend MV::Script;
public:
	Catalog<DataType>(const std::string &a_catalogType, bool a_isServer) :
		isServer(a_isServer) {

		std::string path = "Catalogs/"s + a_catalogType + ".json"s;
		std::string contents = MV::fileContents(path);
        MV::require<MV::ResourceException>(!contents.empty(), "Failed to load Catalog: ", a_catalogType);

		// Catalogs load before the shared engine is connected to Services; plain data needs no shared state
		auto catalogEngine = jai::engine::make();
		jai::serialization::json_archive_reader archive(contents, catalogEngine.get());
		std::string rootType;
		uint32_t rootVersion = 0;
		archive.begin_object(rootType, rootVersion); // ar(obj) is inline; enter the document root ourselves
		serialize(archive);
		archive.end_object();
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

	bool has(const std::string &a_id) const {
		for (auto&& item : dataCollection) {
			if (item.id == a_id) {
				return true;
			}
		}
		return false;
	}

	std::vector<std::string> ids() const {
		std::vector<std::string> result;
		result.reserve(dataCollection.size());
		for (auto&& item : dataCollection) {
			result.push_back(item.id);
		}
		return result;
	}

	size_t size() const {
		return dataCollection.size();
	}

	const std::vector<DataType>& all() const {
		return dataCollection;
	}
private:
	Catalog< DataType>() {
	}

	template<class Archive>
	void serialize(Archive& archive) {
		archive(jai::serialization::make_nvp("data", dataCollection));
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
	void target(const MV::Point<> &a_location, float a_range, MV::PathMode a_mode, std::function<void(TargetPolicy &)> a_succeed, std::function<void(TargetPolicy &)> a_fail = std::function<void(TargetPolicy &)>());

	std::shared_ptr<ServerCreature> self() const;

	bool active() const;

	void update(double a_dt);

	void stun(float a_duration);

	void root(float a_duration);

	void clear() { clearTarget(); }
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
	MV::PathMode pathMode = MV::PathMode::search;
	// the cell the current chase path leads to; a new path is only bought when the target leaves it
	MV::Point<int> chaseCell;
	bool chasing = false;
	bool arrived = false;
	std::function<void(TargetPolicy &)> succeed;
	std::function<void(TargetPolicy &)> fail;

	float stunDuration = 0.0f;
	float rootDuration = 0.0f;

	std::shared_ptr<jai::receiver<void(std::shared_ptr<Creature>)>> arriveReceiver;
	std::shared_ptr<jai::receiver<void(std::shared_ptr<Creature>)>> blockedReceiver;
	std::shared_ptr<jai::receiver<void(std::shared_ptr<Creature>)>> stopReceiver;

	std::shared_ptr<jai::receiver<void(std::shared_ptr<Creature>)>> selfDeathReceiver;
	std::shared_ptr<jai::receiver<void(std::shared_ptr<Creature>)>> targetDeathReceiver;
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
	MV::DeltaVariable<bool> hidden = false;

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
		hidden = a_other->hidden;
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

	template<class Archive>
	void serialize(Archive& archive) {
		if constexpr (jai::serialization::is_save<Archive>) {
			// Save forces all delta variables to serialize
			const_cast<MV::DeltaVariable<std::string>&>(creatureTypeId).serialize(archive, "creatureTypeId", true);
			const_cast<MV::DeltaVariable<int32_t>&>(health).serialize(archive, "health", true);
			const_cast<MV::DeltaVariable<std::string>&>(animationName).serialize(archive, "animationName", true);
			archive.serialize("animationTime", animationTime);
			archive.serialize("animationLoops", animationLoops);
			const_cast<MV::DeltaVariable<int32_t>&>(buildingSlot).serialize(archive, "slot", true);
			const_cast<MV::DeltaVariable<MV::Point<MV::PointPrecision>>&>(position).serialize(archive, "position", true);
			const_cast<MV::DeltaVariable<bool>&>(hidden).serialize(archive, "hidden", true);
			const_cast<MV::DeltaVariable<std::map<std::string, MV::DynamicVariable>>&>(variables).serialize(archive, "variables", true);
		} else {
			creatureTypeId.serialize(archive, "creatureTypeId");
			health.serialize(archive, "health");
			animationName.serialize(archive, "animationName");
			hidden.serialize(archive, "hidden");
			archive.serialize("animationTime", animationTime);
			archive.serialize("animationLoops", animationLoops);
			buildingSlot.serialize(archive, "slot");
			position.serialize(archive, "position");
			variables.serialize(archive, "variables");
		}
	}
};

class Creature : public MV::Scene::Component {
	friend MV::Script;
public:
	typedef void CallbackSignature(std::shared_ptr<Creature>);
	typedef jai::signal<CallbackSignature>::shared_receiver_type SharedReceiverType;

protected:
	jai::signal_emitter<CallbackSignature> onStatusSignal;
	jai::signal_emitter<void(std::shared_ptr<Creature>, int)> onHealthChangeSignal;
	jai::signal_emitter<CallbackSignature> onDeathSignal;
	jai::signal_emitter<CallbackSignature> onFallSignal;

public:
	jai::signal<CallbackSignature> onStatus;
	jai::signal<void(std::shared_ptr<Creature>, int)> onHealthChange;
	jai::signal<CallbackSignature> onDeath;
	jai::signal<CallbackSignature> onFall;

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

	std::vector<std::string> elements() const { return statTemplate.elements.empty() ? std::vector<std::string>{ ElementTable::elementOf(statTemplate.id) } : statTemplate.elements; }
	std::string element() const { return elements().front(); }

	int health() const { return *state->self()->health; }

	static void jai_auto_bind(jai::dynamic_binder<Creature>& builder);

protected:
	Creature(const std::weak_ptr<MV::Scene::Node> &a_owner, GameInstance& a_gameInstance, const std::string& a_skin, const CreatureData& a_statTemplate, std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_state);

	std::string skin;

	GameInstance& gameInstance;

	const CreatureData& statTemplate;


	std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> state;

	std::shared_ptr<MV::Scene::Spine> spineAnimator;
};

class ServerCreature : public Creature {
	friend MV::Scene::Node;
	friend TargetPolicy;
	friend MV::Script;
private:

	jai::signal_emitter<CallbackSignature> onArriveSignal;
	jai::signal_emitter<void(std::shared_ptr<Creature>, int, int64_t)> onDamagedSignal;
	jai::signal_emitter<CallbackSignature> onBlockedSignal;
	jai::signal_emitter<CallbackSignature> onStopSignal;
	jai::signal_emitter<CallbackSignature> onStartSignal;

public:
	jai::signal<CallbackSignature> onArrive;
	// a direct hit landed (weapon, missile, sweep, burst): (self, points taken, attacker id); ticks never fire it
	jai::signal<void(std::shared_ptr<Creature>, int, int64_t)> onDamaged;
	jai::signal<CallbackSignature> onBlocked;
	jai::signal<CallbackSignature> onStop;
	jai::signal<CallbackSignature> onStart;

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
                                auto self = std::static_pointer_cast<Creature>(shared_from_this());
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

	// Combat status shared by every creature script. Damage is scaled by the element table (attacker
	// x defender, then any synergy with an element already on this unit), then routes through the
	// shield, the absorber (an ally taking a share), and health; timed effects tick in update; movement
	// is moveSpeed x multiplier; stun/root durations are scaled while an absorber protects this unit.
	int damage(int a_amount, int64_t a_sourceId);
	int tickDamage(int a_amount, int64_t a_sourceId);
	void heal(int a_amount) { changeHealth(std::max(0, a_amount)); }
	void shield(int a_amount) { shieldPoints = std::max(shieldPoints, a_amount); }
	int shieldAmount() const { return shieldPoints; }
	void dot(double a_perSecond, double a_seconds, int64_t a_sourceId);
	void hot(double a_perSecond, double a_seconds) { timedEffects.push_back({ a_perSecond, a_seconds, 0, 0.0, {} }); }
	void slow(double a_fraction, double a_seconds) { apply(slowEffect, a_fraction, scaledDuration(a_seconds)); }
	void haste(double a_fraction, double a_seconds) { apply(hasteEffect, a_fraction, a_seconds); }
	// slows and hastes are separate positive fractions (the stronger of each kind wins) and multiply
	void attackHaste(double a_fraction, double a_seconds) { apply(attackHasteEffect, a_fraction, a_seconds); }
	void attackSlow(double a_fraction, double a_seconds) { apply(attackSlowEffect, a_fraction, scaledDuration(a_seconds)); }
	void abilityHaste(double a_fraction, double a_seconds) { apply(abilityHasteEffect, a_fraction, a_seconds); }
	void abilitySlow(double a_fraction, double a_seconds) { apply(abilitySlowEffect, a_fraction, scaledDuration(a_seconds)); }
	double moveSpeedMultiplier() const { return std::max(0.05, (1.0 - slowEffect.fraction) * (1.0 + hasteEffect.fraction)); }
	// the attack animation is scaled so one attack takes attackInterval seconds, then statuses multiply
	double attackSpeedMultiplier() const {
		double base = 1.0;
		if (statTemplate.attackInterval > 0.0f && spineAnimator) {
			double animationSeconds = spineAnimator->animationDuration("attack");
			if (animationSeconds > 0.0) { base = animationSeconds / statTemplate.attackInterval; }
		}
		return std::max(0.1, base * (1.0 - attackSlowEffect.fraction) * (1.0 + attackHasteEffect.fraction));
	}
	double abilitySpeedMultiplier() const { return std::max(0.1, (1.0 - abilitySlowEffect.fraction) * (1.0 + abilityHasteEffect.fraction)); }
	void moveSpeed(double a_base) { baseMoveSpeed = a_base; }
	double moveSpeed() const { return baseMoveSpeed; }
	void stun(double a_seconds);
	void root(double a_seconds);
	void visible(bool a_value);
	bool visible() const { return !*state->self()->hidden; }
	void taunt(int64_t a_attackerId, double a_seconds) { tauntId = a_attackerId; tauntRemaining = scaledDuration(a_seconds); }
	int64_t tauntedBy() const { return tauntRemaining > 0.0 ? tauntId : 0; }
	void untargetable(bool a_value) { untargetableFlag = a_value; }
	bool untargetable() const { return untargetableFlag; }
	void absorbFor(int64_t a_hostId, double a_fraction, double a_statusScale);
	int64_t spawnCreature(const std::string& a_creatureId);

	static void jai_auto_bind(jai::dynamic_binder<ServerCreature>& builder);

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

	void animateDeathAndRemove(); // in creature.cpp: fires the script death hook (needs GameInstance)

	virtual void updateImplementation(double a_delta) override;

	struct TimedEffect { double perSecond; double remaining; int64_t source; double carry; std::vector<std::string> elements; };
	struct TimedFraction { double fraction = 0.0; double remaining = 0.0; };
	static void apply(TimedFraction& a_effect, double a_fraction, double a_seconds) {
		if (a_fraction >= a_effect.fraction) { a_effect.fraction = a_fraction; }
		a_effect.remaining = std::max(a_effect.remaining, a_seconds);
	}
	int damageScaled(int a_amount, const std::vector<std::string>& a_attackerElements, int64_t a_sourceId, bool a_reactive);
	double elementMultiplier(const std::vector<std::string>& a_attackerElements) const;
	double scaledDuration(double a_seconds) const;
	std::shared_ptr<ServerCreature> serverCreature(int64_t a_id) const;
	void updateStatus(double a_dt);

	std::shared_ptr<MV::Scene::PathAgent> pathAgent;

	TargetPolicy targeting;

	std::vector<TimedEffect> timedEffects;
	TimedFraction slowEffect;
	TimedFraction hasteEffect;
	TimedFraction attackHasteEffect;
	TimedFraction attackSlowEffect;
	TimedFraction abilityHasteEffect;
	TimedFraction abilitySlowEffect;
	int shieldPoints = 0;
	int64_t tauntId = 0;
	double tauntRemaining = 0.0;
	bool untargetableFlag = false;
	int64_t absorberId = 0;
	double absorbFraction = 0.0;
	double absorbStatusScale = 1.0;
	double baseMoveSpeed = 0.0;
	double appliedMoveSpeed = -1.0;
};

class ClientCreature : public Creature {
	friend MV::Scene::Node;
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
                auto self = std::static_pointer_cast<Creature>(shared_from_this());
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
	void animateDeathAndRemove(); // in creature.cpp: fires the script death hook (needs GameInstance)

	void onNetworkSynchronize();
	void onAnimationChanged();

	virtual void updateImplementation(double a_delta) override;

	void buildHealthBar();
	void refreshHealthBar();
	std::shared_ptr<MV::Scene::Node> healthBar;
	std::shared_ptr<MV::Scene::Drawable> healthFill;

	MV::Point<> startClientPosition;
	double accumulatedDuration = 0.0f;
	MV::TimeDeltaAggregate networkDelta;
};

#endif
