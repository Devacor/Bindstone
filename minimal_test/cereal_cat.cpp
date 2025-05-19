#include <iostream>
#include <sstream>
#include <string>
#include "cereal/archives/json.hpp"
#include "cereal/types/string.hpp"
#include "MV/Utility/properties.h"

class Cat {
public:
    int age{0};
    float weight{0.0f};
    std::string name;
    bool happy{false};

    Cat() = default;
    Cat(int a, float w, const std::string& n, bool h)
        : age(a), weight(w), name(n), happy(h) {}

    template<class Archive>
    void save(Archive& ar) const {
        ar(CEREAL_NVP(age), CEREAL_NVP(weight), CEREAL_NVP(name), CEREAL_NVP(happy));
    }

    template<class Archive>
    void load(Archive& ar) {
        ar(CEREAL_NVP(age), CEREAL_NVP(weight), CEREAL_NVP(name), CEREAL_NVP(happy));
    }

    template<class Archive>
    static void load_and_construct(Archive& ar, cereal::construct<Cat>& construct) {
        construct();
        construct->load(ar);
    }
};

class Cat2 : public MV::PropertyHost {
public:
    MV::MvProperty<int> age{Properties, "age", 0};
    MV::MvProperty<float> weight{Properties, "weight", 0.0f};
    MV::MvProperty<std::string> name{Properties, "name", ""};
    MV::MvProperty<bool> happy{Properties, "happy", false};

    Cat2() = default;

    template<class Archive>
    void save(Archive& ar) const {
        ar(CEREAL_NVP(age), CEREAL_NVP(weight), CEREAL_NVP(name), CEREAL_NVP(happy));
    }

    template<class Archive>
    void load(Archive& ar) {
        ar(CEREAL_NVP(age), CEREAL_NVP(weight), CEREAL_NVP(name), CEREAL_NVP(happy));
    }

    template<class Archive>
    static void load_and_construct(Archive& ar, cereal::construct<Cat2>& construct) {
        construct();
        construct->load(ar);
    }
};

int main() {
    // create a Cat instance with basic fields
    Cat original{5, 4.2f, "Whiskers", true};

    // serialize Cat to JSON
    std::stringstream ss;
    {
        cereal::JSONOutputArchive oar(ss);
        oar(cereal::make_nvp("cat", original));
    }

    std::string data = ss.str();

    // rename Cat to Cat2 in the archive text
    std::string modified = data;
    size_t pos = 0;
    while ((pos = modified.find("Cat")) != std::string::npos) {
        modified.replace(pos, 3, "Cat2");
    }

    // load using Cat2 which uses properties
    Cat2 loaded;
    {
        std::stringstream in(modified);
        cereal::JSONInputArchive iar(in);
        iar(cereal::make_nvp("cat", loaded));
    }

    std::cout << "Loaded Cat2->age=" << loaded.age.get()
              << " weight=" << loaded.weight.get()
              << " name=" << loaded.name.get()
              << " happy=" << loaded.happy.get() << "\n";

    // serialize Cat2 back to JSON and rename for Cat load
    std::stringstream ss2;
    {
        cereal::JSONOutputArchive oar(ss2);
        oar(cereal::make_nvp("cat", loaded));
    }
    std::string data2 = ss2.str();
    std::string modified2 = data2;
    while ((pos = modified2.find("Cat2")) != std::string::npos) {
        modified2.replace(pos, 4, "Cat");
    }

    Cat loadedBack;
    {
        std::stringstream in(modified2);
        cereal::JSONInputArchive iar(in);
        iar(cereal::make_nvp("cat", loadedBack));
    }

    std::cout << "Loaded Cat->age=" << loadedBack.age
              << " weight=" << loadedBack.weight
              << " name=" << loadedBack.name
              << " happy=" << loadedBack.happy << "\n";

    return 0;
}

