#include "Game/gameEditor.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
#include "Utility/cerealUtility.h"
//#include "vld.h"


#include "Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"

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

#include <tuple>
#include <string>

class BaseTest : public std::enable_shared_from_this<BaseTest> {
public:
	virtual void print() const {
		std::cout << value << std::endl;
	}

	int value;
};

namespace TEST {
	namespace Detail {
		template<class T>
		std::reference_wrapper<T> prepareForChaiscript(T& a_value) {
			return{ a_value };
		}
		template<class T>
		T* prepareForChaiscript(std::unique_ptr<T>& a_value) {
			return a_value.get();
		}
		template<class T>
		T* prepareForChaiscript(std::weak_ptr<T>& a_value) {
			return{ !a_value.expired() ? a_value.lock().get() : nullptr };
		}
		template<class T>
		std::shared_ptr<T> prepareForChaiscript(std::shared_ptr<T>& a_value) {
			return{ a_value };
		}
		template<class T>
		const std::shared_ptr<T> prepareForChaiscript(const std::shared_ptr<T>& a_value) {
			return{ a_value };
		}
		template<class T>
		T* prepareForChaiscript(T* a_value) {
			return a_value;
		}

		//may be able to convert these to string_view when chaiscript gets support and it becomes standard.
		inline std::string prepareForChaiscript(char* a_cstring) {
			return{ a_cstring };
		}

		inline std::string prepareForChaiscript(const char* a_cstring) {
			return{ a_cstring };
		}
	}

	template<class T, class Tuple, std::size_t N>
	struct TupleAggregator {
		static void addToVector(const Tuple& t, std::vector<T> &a_aggregate) {
			TupleAggregator<T, Tuple, N - 1>::addToVector(t, a_aggregate);
			a_aggregate.emplace_back(Detail::prepareForChaiscript(std::get<N - 1>(t)));
		}
	};

	template<class T, class Tuple>
	struct TupleAggregator<T, Tuple, 1> {
		static void addToVector(const Tuple& t, std::vector<T> &a_aggregate) {
			a_aggregate.emplace_back(Detail::prepareForChaiscript(std::get<0>(t)));
		}
	};

	//Converts a tuple to a vector of pointers to the elements in the tuple.
	//Works with boost::any or chaiscript::Boxed_Value etc.
	template<class T, class... Args>
	std::vector<T> toVector(const std::tuple<Args...>& t) {
		std::vector<T> result;
		TupleAggregator<T, decltype(t), sizeof...(Args)>::addToVector(t, result);
		return result;
	}


	template <typename ...Arg>
	void callScript(chaiscript::ChaiScript* scriptEnginePointer, const std::string & scriptCallback, Arg &&... a_parameters) {
		if (scriptEnginePointer && !scriptCallback.empty()) {
			std::map<std::string, chaiscript::Boxed_Value> localVariables;
			auto tupleReference = std::forward_as_tuple(std::forward<Arg>(a_parameters)...);
			auto parameterValues = toVector<chaiscript::Boxed_Value>(tupleReference);
			for (size_t i = 0; i < parameterValues.size(); ++i) {
				localVariables.emplace("arg_" + std::to_string(i), parameterValues[i]);
			}
			auto resetLocals = scriptEnginePointer->get_locals();
			SCOPE_EXIT{ scriptEnginePointer->set_locals(resetLocals); };
			scriptEnginePointer->set_locals(localVariables);
			scriptEnginePointer->eval(scriptCallback);
		}
		else if (!scriptCallback.empty()) {
			std::cerr << "Failed to run script in receiver, you need to supply a chaiscript engine handle!\n";
		}
	}
	void callScriptConst(chaiscript::ChaiScript* scriptEnginePointer, const std::string & scriptCallback, const std::shared_ptr<BaseTest> &a_test) {
		TEST::callScript(scriptEnginePointer, scriptCallback, std::weak_ptr<BaseTest>(a_test));
	}
}


int main(int, char *[]) {
	auto sharedTest = std::make_shared<BaseTest>();
	sharedTest->value = 10;
	chaiscript::ChaiScript chaiScript(MV::create_chaiscript_stdlib(), MV::chaiscript_module_paths(), MV::chaiscript_use_paths());

	chaiScript.add(chaiscript::user_type<BaseTest>(), "BaseTest");
	chaiScript.add(chaiscript::fun(&BaseTest::print), "print");

 	TEST::callScriptConst(&chaiScript, "eval_file(\"test.script\");", sharedTest);
// 	std::string content = "Hello World";
// 	auto scriptString = "puts('['); puts(arg_0); puts(']'); puts('['); puts(arg_1); puts(']'); puts('['); puts(arg_2); puts(\"]\n\");";
// 
// 	MV::Signal<void(const std::string&, const std::string&, const std::string&)> callbackTest;
// 	callbackTest.scriptEngine(&chaiScript);
// 	callbackTest.connect("test", scriptString);
// 	{
// 		auto connection2 = callbackTest.connect("puts(\"![\"); puts(arg_0); puts(\"]!\n\");");
// 		callbackTest(content, ":D :D :D", "TEST CHAR");
// 	}
// 	callbackTest("!!!", "VVV", "~~~");
// 
// 	auto jsonCallback = MV::toJson(callbackTest);
// 
// 	auto callbackLoadTest = MV::fromJson<MV::Signal<void(const std::string&, const std::string&, const std::string&)>>(jsonCallback, [&](cereal::JSONInputArchive& archive) {
// 		archive.add(cereal::make_nvp("script", &chaiScript));
// 	});
// 
// 	callbackLoadTest("LoadTest2", "DidThisWork?", "Maybe");
// 
// 	std::cout << std::endl;

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