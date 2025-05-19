#ifndef _MV_PROPERTIES_H_
#define _MV_PROPERTIES_H_

#include <string>
#include <vector>
#include "cereal/cereal.hpp"

namespace MV {

    class MvPropertyBase;

    class MvPropertyList {
    public:
        void add(MvPropertyBase* a_property) {
            properties.push_back(a_property);
        }

        const std::vector<MvPropertyBase*>& all() const { return properties; }
    private:
        std::vector<MvPropertyBase*> properties;
    };

    class PropertyHost {
    public:
        MvPropertyList Properties;
    };

    class MvPropertyBase {
    public:
        MvPropertyBase() = default;
        MvPropertyBase(MvPropertyList& a_list, const std::string& a_name) {
            registerWith(a_list, a_name);
        }
        virtual ~MvPropertyBase() = default;

        const std::string& name() const { return propertyName; }
    protected:
        void registerWith(MvPropertyList& a_list, const std::string& a_name) {
            propertyName = a_name;
            a_list.add(this);
        }
    private:
        std::string propertyName;
    };

    template<typename T>
    class Property : public MvPropertyBase {
    public:
        using value_type = T;
        Property() = default;
        Property(MvPropertyList& a_list, const std::string& a_name, const T& a_default = T{})
            : MvPropertyBase(a_list, a_name), value(a_default) {}

        Property& operator=(const T& a_val) { value = a_val; return *this; }
        operator const T&() const { return value; }
        const T& get() const { return value; }
        T& get() { return value; }

        template<class Archive>
        T save_minimal(const Archive&) const { return value; }

        template<class Archive>
        void load_minimal(const Archive&, const T& a_val) { value = a_val; }
    private:
        T value{};
    };

}

#endif
