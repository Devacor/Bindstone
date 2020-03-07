#ifndef __MV_WALLET_H__
#define __MV_WALLET_H__

#include "MV/Utility/signal.hpp"
#include "cereal/cereal.hpp"
#include <string>
#include <array>

class Wallet {
public:
	enum CurrencyType { GAME, SOFT, HARD, TOTAL };
private:
	MV::Signal<void(Wallet&)> onChangeSignal;
	MV::Signal<void(Wallet&, CurrencyType, int64_t)> onChangeCurrencySignal;

public:
	MV::SignalRegister<void(Wallet&)> onChange;
	MV::SignalRegister<void(Wallet&, CurrencyType, int64_t)> onChangeCurrency;

	Wallet();
	Wallet(const std::array<int64_t, TOTAL> &a_initialValues);
	Wallet(const Wallet& a_rhs);;

	Wallet& operator=(const Wallet& a_rhs);

	std::string name(CurrencyType a_type) const {
		return names[static_cast<int>(a_type)];
	}

	int64_t value(CurrencyType a_type) const {
		return values[static_cast<int>(a_type)];
	}

	Wallet& value(CurrencyType a_type, int64_t a_newValue);

	int64_t add(CurrencyType a_type, int64_t a_amount);
	Wallet& add(const Wallet& a_cost);

	bool remove(CurrencyType a_type, int64_t a_amount);
	bool remove(const Wallet& a_cost);

	bool hasEnough(CurrencyType a_type, int64_t a_amount) const;
	bool hasEnough(const Wallet& a_cost) const;

	template <class Archive>
	void serialize(Archive & archive) {
		archive(
			CEREAL_NVP(values)
		);
	}

	//only attach game hooks
	static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);
private:
	std::vector<int64_t> values = { 0, 0, 0 };
	std::array<std::string, TOTAL> names = { "Gold", "Sweat", "Blood" };
};


#endif
