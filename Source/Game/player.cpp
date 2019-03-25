#include "player.h"
#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/types/polymorphic.hpp"

chaiscript::ChaiScript& InGamePlayer::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<InGamePlayer>(), "Player");

// 	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
// 		return a_self.add(CurrencyType::GAME, a_amount);
// 	}), "add");
// 	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
// 		return a_self.remove(CurrencyType::GAME, a_amount);
// 	}), "remove");
// 	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
// 		return a_self.value(CurrencyType::GAME, a_amount);
// 	}), "value");
// 	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
// 		return a_self.hasEnough(CurrencyType::GAME, a_amount);
// 	}), "hasEnough");

	return a_script;
}