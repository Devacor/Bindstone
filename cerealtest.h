#include <strstream>

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"

#include "cereal/archives/json.hpp"
#include "cereal/types/polymorphic.hpp"

#include <memory>

struct A : std::enable_shared_from_this<A>
{
	typedef std::vector<std::shared_ptr<A>> DrawListVectorType;
	DrawListVectorType drawListVector;
	static int i;
	int id;
	A():id(i++){}

	template <class Archive>
	void serialize(Archive & ar)
	{
		ar(CEREAL_NVP(drawListVector)); //If we don't actually save the drawListVector it's fine.  If we do, we'll get a crash in the destructor.
	}
	virtual ~A(){
		std::cout << "Destructing: " << id << std::endl;
		for(auto item : drawListVector){
			std::cout << "iterating over: " << item->id << std::endl;
		}
	}
};

int A::i = 0;

struct B : A
{
	template <class Archive>
	void serialize(Archive & ar)
	{
		ar(CEREAL_NVP(drawListVector)); //If we don't actually save the drawListVector it's fine.  If we do, we'll get a crash in the destructor.
	}
};

CEREAL_REGISTER_TYPE(B);

void saveTest(){
	{
		cereal::JSONOutputArchive ar(std::cout);
		std::shared_ptr<A> a = std::make_shared<A>();
		std::shared_ptr<A> b = std::make_shared<B>();
		std::shared_ptr<A> c = std::make_shared<A>();
		a->drawListVector.push_back(b);

		b->drawListVector.push_back(c);

		ar(a);

		auto x = b->shared_from_this();
		a->drawListVector.clear();
		a.reset();
		std::cout << "grood1" << std::endl;
		b->drawListVector.clear();
		b.reset();
		std::cout << "grood2" << std::endl;
		c.reset();
		std::cout << "grood3" << std::endl;
	}
	std::cout << "grood4" << std::endl;
}
