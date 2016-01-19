#include "player.h"

chaiscript::ChaiScript& Player::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<Player>(), "Player");

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