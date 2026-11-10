// Programs the serialization headers reject. tools/ci/gates.sh headers compiles this file once per `must_not_compile N: text`
// line below with -DJAI_MUST_NOT_COMPILE=N and wants the compile to fail with that text, and once with no case, which
// builds: the uses under `#else` are the same types through the routes whose error waits for run time.
//
// must_not_compile 1: declares serialize() but jai::access cannot reach it
// must_not_compile 2: declares save() but jai::access cannot reach it
// must_not_compile 3: declares load() but jai::access cannot reach it
// must_not_compile 4: declares load_and_construct() but jai::access cannot reach it
// must_not_compile 5: the archive cannot write this type, it has no serialization route
// must_not_compile 6: the archive cannot read this type, it has no serialization route
// must_not_compile 7: public member save is not an archive function
// must_not_compile 8: the archive cannot write this type, it has no serialization route
// must_not_compile 9: declares serialize() but jai::access cannot reach it
// must_not_compile 10: declares load_and_construct() but jai::access cannot reach it
// must_not_compile 11: the archive cannot construct this type on a load
// must_not_compile 12: declares load_and_construct() but jai::access cannot reach it
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/properties.hpp>
#include <jaiscript/properties/property_serialization.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/serialization/construct.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace must_not_compile {

class ForgottenFriendSerialize {
	int capacity = 20;
	template<class Archive> void serialize(Archive& a_archive) { a_archive(JAI_NVP(capacity)); }
};

class ForgottenFriendSaveLoad {
	int capacity = 20;
	template<class Archive> void save(Archive& a_archive) const { a_archive(JAI_NVP(capacity)); }
	template<class Archive> void load(Archive& a_archive) { a_archive(JAI_NVP(capacity)); }
};

class ForgottenFriendConstruct {
public:
	explicit ForgottenFriendConstruct(int a_capacity) : capacity(a_capacity) {}
	template<class Archive> void serialize(Archive& a_archive) { a_archive(JAI_NVP(capacity)); }
private:
	int capacity;
	template<class Archive>
	static void load_and_construct(Archive& a_archive, jai::serialization::construct<ForgottenFriendConstruct>& a_construct) {
		a_construct(0);
		a_construct->serialize(a_archive);
	}
};

class ForgottenFriendRegistered {
public:
	explicit ForgottenFriendRegistered(int a_capacity) : capacity(a_capacity) {}
	virtual ~ForgottenFriendRegistered() = default;
	template<class Archive> void serialize(Archive& a_archive) { a_archive(JAI_NVP(capacity)); }
private:
	int capacity;
	template<class Archive>
	static void load_and_construct(Archive& a_archive, jai::serialization::construct<ForgottenFriendRegistered>& a_construct) {
		a_construct(0);
		a_construct->serialize(a_archive);
	}
};

class InheritsConstruct : public ForgottenFriendRegistered {
public:
	explicit InheritsConstruct(int a_capacity) : ForgottenFriendRegistered(a_capacity) {}
};

class RegisteredBase {
	friend jai::access;
public:
	explicit RegisteredBase(int a_capacity) : capacity(a_capacity) {}
	virtual ~RegisteredBase() = default;
	template<class Archive> void serialize(Archive& a_archive) { a_archive(JAI_NVP(capacity)); }
private:
	int capacity;
	template<class Archive>
	static void load_and_construct(Archive& a_archive, jai::serialization::construct<RegisteredBase>& a_construct) {
		a_construct(0);
		a_construct->serialize(a_archive);
	}
};

class BuiltAtRunTime : public RegisteredBase {
public:
	explicit BuiltAtRunTime(int a_capacity) : RegisteredBase(a_capacity) {}
};

struct NoRoute {
	int capacity = 20;
};

struct NoWayToConstruct {
	explicit NoWayToConstruct(int a_capacity) : capacity(a_capacity) {}
	int capacity;
	template<class Archive> void serialize(Archive& a_archive) { a_archive(JAI_NVP(capacity)); }
};

struct SavesToAPath {
	virtual ~SavesToAPath() = default;
	int capacity = 20;
	void save(const std::string&) const {}
	bool load() { return true; }
};

class Owner : public jai::property_owner<Owner> {
public:
	JAI_PROPERTY((ForgottenFriendSerialize), forgotten);
	JAI_TRANSIENT_PROPERTY((NoRoute), scratch);
	JAI_TRANSIENT_PROPERTY((std::vector<SavesToAPath>), paths);
	JAI_TRANSIENT_PROPERTY((std::shared_ptr<NoWayToConstruct>), unbuilt);
};

inline std::string exercise(jai::engine& a_engine) {
#if JAI_MUST_NOT_COMPILE == 1
	return jai::to_json(a_engine, ForgottenFriendSerialize{});
#elif JAI_MUST_NOT_COMPILE == 2
	return jai::to_json(a_engine, ForgottenFriendSaveLoad{});
#elif JAI_MUST_NOT_COMPILE == 3
	jai::from_json<ForgottenFriendSaveLoad>(a_engine, "{}");
	return {};
#elif JAI_MUST_NOT_COMPILE == 4
	jai::from_json<std::shared_ptr<ForgottenFriendConstruct>>(a_engine, "{}");
	return {};
#elif JAI_MUST_NOT_COMPILE == 5
	return jai::to_json(a_engine, NoRoute{});
#elif JAI_MUST_NOT_COMPILE == 6
	jai::from_binary<NoRoute>(a_engine, {});
	return {};
#elif JAI_MUST_NOT_COMPILE == 7
	return jai::to_json(a_engine, SavesToAPath{});
#elif JAI_MUST_NOT_COMPILE == 8
	return jai::to_json(a_engine, std::map<std::string, std::vector<NoRoute>>{});
#elif JAI_MUST_NOT_COMPILE == 9
	jai::serialization::json_archive_writer writer(0, &a_engine);
	ForgottenFriendSerialize forgotten;
	writer.begin_object();
	writer(JAI_NVP(forgotten));
	writer.end_object();
	return writer.str();
#elif JAI_MUST_NOT_COMPILE == 10
	static jai::registrar<ForgottenFriendRegistered, void> registered("ForgottenFriendRegistered");
	return {};
#elif JAI_MUST_NOT_COMPILE == 11
	jai::from_json<std::shared_ptr<NoWayToConstruct>>(a_engine, "{}");
	return {};
#elif JAI_MUST_NOT_COMPILE == 12
	static jai::registrar<InheritsConstruct, void, jai::bases<ForgottenFriendRegistered>> registered("InheritsConstruct");
	return {};
#else
	static jai::registrar<SavesToAPath, void> boundOnly("SavesToAPath");
	static jai::registrar<BuiltAtRunTime, void, jai::bases<RegisteredBase>> builtAtRunTime("BuiltAtRunTime");
	return jai::to_json(a_engine, Owner{});
#endif
}

}
