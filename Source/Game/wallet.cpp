#include "wallet.h"
#include "MV/Utility/chaiscriptUtility.h"
#include "MV/Utility/require.hpp"

Wallet::Wallet() :
	onChange(onChangeSignal),
	onChangeCurrency(onChangeCurrencySignal) {
}

Wallet::Wallet(const std::array<int64_t, 3> &a_initialValues) :
	onChange(onChangeSignal),
	onChangeCurrency(onChangeCurrencySignal),
	values(a_initialValues.begin(), a_initialValues.end()) {
}

Wallet::Wallet(const Wallet& a_rhs) : Wallet() {
	values = a_rhs.values;
	names = a_rhs.names;
}

Wallet& Wallet::operator=(const Wallet& a_rhs) {
	auto oldValues = values;
	values = a_rhs.values;
	bool changed = false;
	for (size_t i = 0; i < TOTAL; ++i) {
		if (oldValues[i] != values[i]) {
			changed = true;
			onChangeCurrencySignal(*this, static_cast<CurrencyType>(i), values[i] - oldValues[i]);
		}
	}
	if (changed) {
		onChangeSignal(*this);
	}
	return *this;
}

Wallet& Wallet::value(CurrencyType a_type, int64_t a_newValue) {
	MV::require<MV::RangeException>(a_newValue >= 0, "Negative amount supplied to value: ", a_newValue);
	auto difference = a_newValue - values[static_cast<int>(a_type)];
	onChangeCurrencySignal(*this, a_type, difference);
	onChangeSignal(*this);
	return *this;
}

int64_t Wallet::add(CurrencyType a_type, int64_t a_amount) {
	MV::require<MV::RangeException>(a_amount >= 0, "Negative amount supplied to add: ", a_amount);
	values[static_cast<int>(a_type)] += a_amount;
	onChangeCurrencySignal(*this, a_type, a_amount);
	onChangeSignal(*this);
	return values[static_cast<int>(a_type)];
}

Wallet& Wallet::add(const Wallet& a_cost) {
	for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
		values[i] += a_cost.values[i];
	}
	for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
		onChangeCurrencySignal(*this, static_cast<CurrencyType>(i), a_cost.values[i]);
	}
	onChangeSignal(*this);
	return *this;
}

bool Wallet::remove(CurrencyType a_type, int64_t a_amount) {
	MV::require<MV::RangeException>(a_amount >= 0, "Negative amount supplied to remove: ", a_amount);
	if (hasEnough(a_type, a_amount)) {
		values[static_cast<int>(a_type)] -= a_amount;
		onChangeCurrencySignal(*this, a_type, -(static_cast<int64_t>(a_amount)));
		return true;
	} else {
		return false;
	}
}

bool Wallet::remove(const Wallet& a_cost) {
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

bool Wallet::hasEnough(CurrencyType a_type, int64_t a_amount) const {
	MV::require<MV::RangeException>(a_amount >= 0, "Negative amount supplied to remove: ", a_amount);
	return (values[static_cast<int>(a_type)] - a_amount) >= 0;
}

bool Wallet::hasEnough(const Wallet& a_cost) const {
	for (int i = 0; i < static_cast<int>(TOTAL); ++i) {
		if (values[i] - a_cost.values[i] < 0) {
			return false;
		}
	}
	return true;
}

chaiscript::ChaiScript& Wallet::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<Wallet>(), "Wallet");

	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
		return a_self.add(CurrencyType::GAME, a_amount);
	}), "add");
	a_script.add(chaiscript::fun([](Wallet &a_self, size_t a_amount) {
		return a_self.remove(CurrencyType::GAME, a_amount);
	}), "remove");
	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
		return a_self.value(CurrencyType::GAME, a_amount);
	}), "value");
	a_script.add(chaiscript::fun([](Wallet &a_self, int64_t a_amount) {
		return a_self.hasEnough(CurrencyType::GAME, a_amount);
	}), "hasEnough");

	return a_script;
}

