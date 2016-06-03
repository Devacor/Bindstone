#include "Game/gameEditor.h"
#include "ClickerGame/clickerGame.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
#include "vld.h"

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
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(CEREAL_NVP(test));
		std::cout << "VERSION: " << version << std::endl;
	}

	int test;
};

#include "cereal/archives/json.hpp"

int main(int, char *[]){

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


	//auto emailer = MV::Email::make("email-smtp.us-west-2.amazonaws.com", "587", { "AKIAIVINRAMKWEVUT6UQ", "AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw" });
	//emailer->send({ "jai", "jackaldurante@gmail.com", "Derv", "maxmike@gmail.com" }, "Testing new Interface", "Does this work too?");
	
	GameEditor menu;

	menu.start();
	
	return 0;
}