#include "Game/gameEditor.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
//#include "vld.h"

struct TestObject {
	TestObject() { std::cout << "\nConstructor\n"; }
	~TestObject() { std::cout << "\nDestructor\n"; }
	TestObject(TestObject&);
	TestObject(TestObject&&) { std::cout << "\nMove\n"; }
	TestObject& operator=(const TestObject&) { std::cout << "\nAssign\n"; }

	int payload = 0;
};

TestObject::TestObject(TestObject&) {
	std::cout << "\nCopy\n"; 
	payload++;
}

bool isDone() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return true;
		}
	}
	return false;
}

int main(int, char *[]) {
	chaiscript::ChaiScript chaiScript(MV::create_chaiscript_stdlib());

	std::string content = "Hello World";
	std::string* contentPtr = &content;
	auto scriptString = "puts('['); puts(arg_0); puts(']'); puts('['); puts(arg_1); puts(']'); puts('['); puts(arg_2); puts(']');";

	MV::Signal<void(const std::string&, std::string*, const char *)> callbackTestSignal;
	MV::SignalRegister<void(const std::string&, std::string*, const char*)> callbackTest(callbackTestSignal);
	callbackTest.script(scriptString);
	callbackTest.scriptEngine(&chaiScript);

	callbackTestSignal(content, contentPtr, "TEST CHAR");

	std::cout << std::endl;

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