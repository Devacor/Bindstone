#ifndef __MV_WALLET_H__
#define __MV_WALLET_H__

#include <jaiscript/signals/signal.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <string>
#include <array>

class Wallet {
public:
	enum CurrencyType { GAME, SOFT, HARD, TOTAL };
private:
	jai::signal_emitter<void(Wallet&)> onChangeSignal;
	jai::signal_emitter<void(Wallet&, CurrencyType, int64_t)> onChangeCurrencySignal;

public:
	jai::signal<void(Wallet&)> onChange;
	jai::signal<void(Wallet&, CurrencyType, int64_t)> onChangeCurrency;

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

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(values));
	}

private:
	std::vector<int64_t> values = { 0, 0, 0 };
	std::array<std::string, TOTAL> names = { "Gold", "Sweat", "Blood" };
};


#endif
