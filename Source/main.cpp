#include "Game/gameEditor.h"
#include "ClickerGame/clickerGame.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
//#include "vld.h"

#include <memory>

bool isDone() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return true;
		}
	}
	return false;
}

struct TestClass {

	template <class Archive>
	void serialize(Archive & archive) {
		archive(CEREAL_NVP(test));
	}

	int test;
};

struct TestClassVersioned {
	
	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(test));
		std::cout << "VERSION: " << version << std::endl;
	}

	int test;
};

template<typename T>
T mixInOutT(T start, T end, float percent, float strength = 1.0f) {
	auto halfRange = (end - start) / 2.0f + start;
	if (percent < .5f)
	{
		return MV::mixIn(start, halfRange, percent * 2.0f, strength);
	}
	return MV::mixOut(halfRange, end, (percent - .5f) * 2.0f, strength);
}

template<typename T>
T mixOutInT(T start, T end, float percent, float strength = 1.0f) {
	auto halfRange = (end - start) / 2.0f + start;
	if (percent < .5f)
	{
		return MV::mixOut(start, halfRange, percent * 2.0f, strength);
	}
	return MV::mixIn(halfRange, end, (percent - .5f) * 2.0f, strength);
}

#include "cereal/archives/json.hpp"

int main(int, char *[]) {

	// 	pqxx::connection c("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123");
	// 	pqxx::work txn(c);
	// 
	// 
	// 	txn.exec(
	// 		"CREATE EXTENSION IF NOT EXISTS citext WITH SCHEMA public;"
	// 		"CREATE TABLE Instances ("
	// 		"	Id SERIAL primary key,"
	// 		"	Available boolean			default false,"
	// 		"	Host text						default '',"
	// 		"	Port integer				default 0,"
	// 		"	PlayerLeft	int				default 0,"
	// 		"	PlayerRight	int				default 0,"
	// 		"	LastUpdate timestamp without time zone default (now() at time zone 'utc'),"
	// 		"	Result JSON"
	// 		");");
	// 	txn.commit();

	// 	pqxx::result r = txn.exec(
	// 		"SELECT state "
	// 		"FROM players "
	// 		"WHERE email = " + txn.quote("maxmike@gmail.com"));
	// 
	// 	if (r.size() != 1)
	// 	{
	// 		std::cerr
	// 			<< "Expected 1 player with email " << txn.quote("maxmike@gmail.com") << ", "
	// 			<< "but found " << r.size() << std::endl;
	// 		return 1;
	// 	}
	// 
	// 	std::string status = r[0][0].c_str();
	// 	std::cout << "Updating employee #" << status << std::endl;


		/*
		pqxx::result r = txn.exec(
			"select column_name, data_type, character_maximum_length"
			"from INFORMATION_SCHEMA.COLUMNS where table_name = Players");

		txn.commit();


		if (r.size() != 1)
		{
			std::cerr
				<< "Expected 1 employee with name " << argv[1] << ", "
				<< "but found " << r.size() << std::endl;
			return 1;
		}
		*/

	std::cout << 0.0f << " => " << MV::mixInOut(-10.0f, 10.0f, 0.5f, 2) << ", " << MV::unmixInOut(-10.0f, 10.0f, 0.0f, 2) << ", " << MV::mixInOut(-10.0f, 10.0f, MV::unmixInOut(-10.0f, 10.0f, 0.0f, 2), 2) <<  std::endl;
	for (float strength = 1.0f; strength < 4.0f; ++strength) {
		std::cout << "_________________________\nIN: ";
		for (float i = 0.0f; i <= 1.0f; i += .2f) {
			std::cout << i << ": " << MV::mixIn(-10.0f, 10.0f, i, strength) << " => " << MV::unmixIn(-10.0f, 10.0f, MV::mixIn(-10.0f, 10.0f, i, strength), strength) << std::endl;
		}
		std::cout << "_________________________\nOUT: ";
		for (float i = 0.0f; i <= 1.0f; i += .2f) {
			std::cout << i << ": " << MV::mixOut(-10.0f, 10.0f, i, strength) << " => " << MV::unmixOut(-10.0f, 10.0f, MV::mixOut(-10.0f, 10.0f, i, strength), strength) << std::endl;
		}
		std::cout << "_________________________\nINout: ";
		for (float i = 0.0f; i <= 1.0f; i += .2f) {
			std::cout << i << ": " << mixInOutT(-10.0f, 10.0f, i, strength) << " => " << MV::unmixInOut(-10.0f, 10.0f, mixInOutT(-10.0f, 10.0f, i, strength), strength) << std::endl;
		}
		std::cout << "_________________________\nOUTIN: ";
		for (float i = 0.0f; i <= 1.0f; i += .2f) {
			std::cout << i << ": " << mixOutInT(-10.0f, 10.0f, i, strength) << " => " << MV::unmixOutIn(-10.0f, 10.0f, mixOutInT(-10.0f, 10.0f, i, strength), strength) << std::endl;
		}
	}
	//auto emailer = MV::Email::make("email-smtp.us-west-2.amazonaws.com", "587", { "AKIAIVINRAMKWEVUT6UQ", "AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw" });
	//emailer->send({ "jai", "jackaldurante@gmail.com", "Derv", "maxmike@gmail.com" }, "Testing new Interface", "Does this work too?");
	
	GameEditor menu;

	menu.start();
	
	return 0;
}