#ifndef _BINDSTONE_ELEMENTS_H_
#define _BINDSTONE_ELEMENTS_H_

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>
#include "MV/Utility/generalUtility.h"
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/core/engine.hpp>

struct ElementDamageRule {
	std::string attacker;
	std::string defender;
	double multiplier = 1.0;
	template<class Archive>
	void serialize(Archive& archive) { archive(JAI_NVP(attacker), JAI_NVP(defender), JAI_NVP(multiplier)); }
};

struct ElementSynergyRule {
	std::string attacker;
	std::string affecting;
	double multiplier = 1.0;
	template<class Archive>
	void serialize(Archive& archive) { archive(JAI_NVP(attacker), JAI_NVP(affecting), JAI_NVP(multiplier)); }
};

struct ElementWardRule {
	std::string ward;
	std::string host;
	double scale = 1.0;
	template<class Archive>
	void serialize(Archive& archive) { archive(JAI_NVP(ward), JAI_NVP(host), JAI_NVP(scale)); }
};

// Catalogs/Elements.json holds every cross-element number: damage is attacker x defender, a synergy
// multiplies an attacker against an element already on the target (an active dot or a ward), and a
// ward scales how much of its host's damage an absorber takes. Unlisted pairs are 1.0.
class ElementTable {
public:
	ElementTable() {
		std::string contents = MV::fileContents("Catalogs/Elements.json");
		MV::require<MV::ResourceException>(!contents.empty(), "Failed to load Catalog: Elements");
		auto catalogEngine = jai::engine::make();
		jai::serialization::json_archive_reader archive(contents, catalogEngine.get());
		std::string rootType;
		uint32_t rootVersion = 0;
		archive.begin_object(rootType, rootVersion);
		serialize(archive);
		archive.end_object();
		for (auto& rule : damage) { damageTable[key(rule.attacker, rule.defender)] = rule.multiplier; }
		for (auto& rule : synergy) { synergyTable[key(rule.attacker, rule.affecting)] = rule.multiplier; }
		for (auto& rule : ward) { wardTable[key(rule.ward, rule.host)] = rule.scale; }
	}

	// "Water_DropletHurt" -> "water": the creature id prefix is the element when the catalog lists none
	static std::string elementOf(const std::string& a_creatureId) {
		auto result = a_creatureId.substr(0, a_creatureId.find('_'));
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	using Elements = std::vector<std::string>;

	// multi-typed units multiply: every attacker type against every defender type
	double damageMultiplier(const Elements& a_attacker, const Elements& a_defender) const { return product(damageTable, a_attacker, a_defender); }
	double synergyMultiplier(const Elements& a_attacker, const Elements& a_affecting) const { return product(synergyTable, a_attacker, a_affecting); }
	double wardScale(const Elements& a_ward, const Elements& a_host) const { return product(wardTable, a_ward, a_host); }

	template<class Archive>
	void serialize(Archive& archive) { archive(JAI_NVP(damage), JAI_NVP(synergy), JAI_NVP(ward)); }

private:
	static std::string key(const std::string& a_lhs, const std::string& a_rhs) { return a_lhs + '>' + a_rhs; }
	static double lookup(const std::map<std::string, double>& a_table, const std::string& a_lhs, const std::string& a_rhs) {
		auto found = a_table.find(key(a_lhs, a_rhs));
		return found == a_table.end() ? 1.0 : found->second;
	}
	static double product(const std::map<std::string, double>& a_table, const Elements& a_lhs, const Elements& a_rhs) {
		double result = 1.0;
		for (auto& lhs : a_lhs) { for (auto& rhs : a_rhs) { result *= lookup(a_table, lhs, rhs); } }
		return result;
	}

	std::vector<ElementDamageRule> damage;
	std::vector<ElementSynergyRule> synergy;
	std::vector<ElementWardRule> ward;
	std::map<std::string, double> damageTable;
	std::map<std::string, double> synergyTable;
	std::map<std::string, double> wardTable;
};

#endif
