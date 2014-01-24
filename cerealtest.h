#include <strstream>

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"

#include "cereal/archives/json.hpp"
#include <cereal/types/polymorphic.hpp>

#include <memory>

class Handle;

struct Node {
	virtual ~Node(){}
	std::map<std::string, std::shared_ptr<Node>> children;
	std::shared_ptr<Handle> handle;

	template <class Archive>
	void serialize(Archive & archive){
		archive(CEREAL_NVP(handle), CEREAL_NVP(children));
	}
};

struct DerivedNode : public Node {
};

CEREAL_REGISTER_TYPE(Node);
CEREAL_REGISTER_TYPE(DerivedNode);

class Definition;
struct Handle {
	Handle(){}
	Handle(int id):id(id){}
	int id = 0;
	std::shared_ptr<Definition> definition;

	template <class Archive>
	void serialize(Archive & archive){
		archive(CEREAL_NVP(id), CEREAL_NVP(definition));
	}
};

struct Definition {
	virtual ~Definition(){}
	Definition(){}
	Definition(int id):id(id){}
	int id = 0;
	std::vector<std::weak_ptr<Handle>> handles;

	template <class Archive>
	void serialize(Archive & archive){
		archive(CEREAL_NVP(id), CEREAL_NVP(handles));
	}
};

struct DerivedDefinition : public Definition {
	DerivedDefinition(){}
	DerivedDefinition(int id):Definition(id){}
};

CEREAL_REGISTER_TYPE(Definition);
CEREAL_REGISTER_TYPE(DerivedDefinition);

void saveTest(){
	std::stringstream stream;
	{
		std::shared_ptr<Node> arm = std::make_shared<DerivedNode>();
		std::shared_ptr<Node> rock1 = std::make_shared<DerivedNode>();
		std::shared_ptr<Node> rock2 = std::make_shared<DerivedNode>();
		arm->children["rock1"] = rock1;
		arm->children["rock2"] = rock2;

		std::shared_ptr<Definition> definition = std::make_shared<DerivedDefinition>(1);
		rock1->handle = std::make_shared<Handle>(1);
		rock2->handle = std::make_shared<Handle>(2);
		rock1->handle->definition = definition;
		rock2->handle->definition = definition;

		cereal::JSONOutputArchive archive(stream);
		archive(cereal::make_nvp("arm", arm));
	}
	std::cout << stream.str() << std::endl;
	std::cout << std::endl;
	{
		cereal::JSONInputArchive archive(stream);
		std::shared_ptr<Node> arm;
		archive(cereal::make_nvp("arm", arm));

		cereal::JSONOutputArchive outArchive(stream);
		outArchive(cereal::make_nvp("arm", arm));
		
		std::cout << stream.str() << std::endl;
		std::cout << std::endl;

		archive(cereal::make_nvp("arm", arm));
		std::cout << "Arm Child 0 Definition ID: " << arm->children["rock1"]->handle->definition->id << std::endl;
		std::cout << "Arm Child 1 Definition ID: " << arm->children["rock2"]->handle->definition->id << std::endl;
	}

	std::cout << "Done" << std::endl;
}
