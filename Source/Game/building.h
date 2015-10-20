#ifndef __MV_BUILDING_H__
#define __MV_BUILDING_H__

#include "Render/package.h"
#include <string>
namespace MV {
	class Wallet {
	public:
		enum CurrencyType { GAME, SOFT, HARD, TOTAL };
	private:
		Signal<void(Wallet&)> onChangeSignal;
		Signal<void(Wallet&, CurrencyType, int64_t)> onChangeCurrencySignal;

	public:
		SignalRegister<void(Wallet&)> onChange;
		SignalRegister<void(Wallet&, CurrencyType, int64_t)> onChangeCurrency;

		Wallet() :
			onChange(onChangeSignal),
			onChangeCurrency(onChangeCurrencySignal) {
		}

		std::string name(CurrencyType a_type) const {
			return names[static_cast<int>(a_type)];
		}

		int64_t value(CurrencyType a_type) const {
			return values[static_cast<int>(a_type)];
		}

		Wallet& value(CurrencyType a_type, size_t a_newValue) {
			require<RangeException>(a_newValue >= 0, "Negative amount supplied to value: ", a_newValue);
			auto difference = a_newValue - values[static_cast<int>(a_type)];
			onChangeCurrencySignal(*this, a_type, difference);
			onChangeSignal(*this);
			return *this;
		}

		int64_t add(CurrencyType a_type, size_t a_amount) {
			require<RangeException>(a_amount >= 0, "Negative amount supplied to add: ", a_amount);
			values[static_cast<int>(a_type)] += a_amount;
			onChangeCurrencySignal(*this, a_type, a_amount);
			onChangeSignal(*this);
			return values[static_cast<int>(a_type)];
		}

		Wallet& add(const Wallet& a_cost) {
			for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
				values[i] += a_cost.values[i];
			}
			for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
				onChangeCurrencySignal(*this, static_cast<CurrencyType>(i), a_cost.values[i]);
			}
			onChangeSignal(*this);
			return *this;
		}

		bool remove(CurrencyType a_type, size_t a_amount) {
			if (hasEnough(a_type, a_amount)) {
				values[static_cast<int>(a_type)] -= a_amount;
				onChangeCurrencySignal(*this, a_type, -(static_cast<int64_t>(a_amount)));
				return true;
			} else {
				return false;
			}
		}

		bool remove(const Wallet& a_cost) {
			if (hasEnough(a_cost)) {
				for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
					values[i] -= a_cost.values[i];
				}
				for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
					onChangeCurrencySignal(*this, static_cast<CurrencyType>(i), -a_cost.values[i]);
				}
				onChangeSignal(*this);
				return true;
			} else {
				return false;
			}
		}

		bool hasEnough(CurrencyType a_type, size_t a_amount) const {
			return (values[static_cast<int>(a_type)] - a_amount) >= 0;
		}

		bool hasEnough(const Wallet& a_cost) const {
			for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
				if (values[i] - a_cost.values[i] < 0) {
					return false;
				}
			}
			return true;
		}

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(values)
			);
		}
	private:
		std::vector<int64_t> values = { 0, 0, 0 };
		std::vector<std::string> names = { "Gold", "Sweat", "Blood" };
	};

	struct MissileData {
		std::string launchAsset;
		std::string asset;
		std::string landAsset;

		float range;
		float damage;
		float speed;
	};

	struct CreatureData {
		std::string name;
		std::string description;

		std::string icon;
		std::string asset;

		float speed;

		int health;
		float defense;
		float resistance;
		float strength;
		float ability;

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(name),
				CEREAL_NVP(description),
				CEREAL_NVP(icon),
				CEREAL_NVP(asset),
				CEREAL_NVP(speed),
				CEREAL_NVP(health),
				CEREAL_NVP(defense),
				CEREAL_NVP(resistance),
				CEREAL_NVP(strength),
				CEREAL_NVP(ability)
			);
		}
	};

	struct WaveData {
		double spawnDelay;
		std::vector<std::pair<CreatureData, double>> creatures;

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(spawnDelay),
				CEREAL_NVP(creatures)
			);
		}
	};

	struct BuildTree {
		WaveData wave;
		std::string icon;
		std::string asset;

		std::string name;
		std::string description;

		int64_t cost;

		std::vector<std::shared_ptr<BuildTree>> path;

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(name),
				CEREAL_NVP(description),
				CEREAL_NVP(cost),
				CEREAL_NVP(icon),
				CEREAL_NVP(asset),
				CEREAL_NVP(wave),
				CEREAL_NVP(path)
			);
		}
	};

	struct BuildingData {
		std::vector<Wallet> storeCosts;
		std::string icon;

		BuildTree upgrades;

		std::string id;

		std::string name;
		std::string description;

		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				CEREAL_NVP(id),
				CEREAL_NVP(icon), 
				CEREAL_NVP(name),
				CEREAL_NVP(description),
				CEREAL_NVP(storeCosts)
			);
		}
	};

	class BuildingIndex {
	public:
		template <class Archive>
		void serialize(Archive & archive) {
			archive(
				cereal::make_nvp("buildings", buildingList)
			);
		}
	private:
		std::vector<BuildingData> buildingList;
	};

	class Player {
		Wallet balance;
		std::vector<std::string> unlockedBuildings;

		std::string name;
		std::string email;
		std::string passwordHash;
	};

	class Building : public MV::Scene::Component {
		friend MV::Scene::Node;
		friend cereal::access;

	public:
		ComponentDerivedAccessors(Building)

			virtual void updateImplementation(double a_delta) override {};

	protected:
		Building(const std::weak_ptr<MV::Scene::Node> &a_owner) :
			Component(a_owner) {
		}

		virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<MV::Scene::Node> &a_parent) {
			return cloneHelper(a_parent->attach<Building>().self());
		}

		virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
			Component::cloneHelper(a_clone);
			auto creatureClone = std::static_pointer_cast<Building>(a_clone);
			return a_clone;
		}

		void upgrade(int index = 0) {

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
		static void load_and_construct(Archive & archive, cereal::construct<Building> &construct) {
			construct(std::shared_ptr<Node>());
			archive(
				cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
				);
			construct->initialize();
		}

	};
}
#endif