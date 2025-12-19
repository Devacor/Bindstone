#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/core/class_builder_serialization.hpp>  // Must be included after class_builder and archive headers
#include <memory>
#include <string>

namespace jai::foundry::tests {

// Mock context for dependency injection (similar to MV::JaiScriptContext)
struct test_deserialization_context {
    std::string service_name = "TestService";
    int dependency_value = 42;
};

// Test class with non-default constructor requiring context
class context_required_object {
public:
    context_required_object(const std::string& service, int value)
        : service_name_(service), value_(value), initialized_(true) {}

    std::string get_service_name() const { return service_name_; }
    int get_value() const { return value_; }
    bool is_initialized() const { return initialized_; }

    void set_data(const std::string& data) { data_ = data; }
    std::string get_data() const { return data_; }

private:
    std::string service_name_;
    int value_;
    bool initialized_ = false;
    std::string data_;
};

// Test class with non-default constructor requiring archive pre-reading
class property_preread_object {
public:
    property_preread_object(const std::string& constructor_id, int constructor_value)
        : id_(constructor_id), constructor_value_(constructor_value) {}

    std::string get_id() const { return id_; }
    int get_constructor_value() const { return constructor_value_; }

    void set_name(const std::string& name) { name_ = name; }
    std::string get_name() const { return name_; }

private:
    std::string id_;
    int constructor_value_;
    std::string name_;
};

// Test class with both context and archive dependencies
class complex_object {
public:
    complex_object(const std::string& service, const std::string& preread_id)
        : service_name_(service), id_(preread_id) {}

    std::string get_service_name() const { return service_name_; }
    std::string get_id() const { return id_; }

    void set_value(int v) { value_ = v; }
    int get_value() const { return value_; }

private:
    std::string service_name_;
    std::string id_;
    int value_ = 0;
};

class factory_deserialization_tests : public suite {
public:
    factory_deserialization_tests() : suite("Factory Deserialization Tests") {}

    void forge_tests() override {
        test("context_only_factory", [this]() {
            auto eng = engine::make();

            // Register class with context-only factory
            class_builder<context_required_object>(eng, "ContextRequiredObject")
                .method("get_service_name", &context_required_object::get_service_name)
                .method("get_value", &context_required_object::get_value)
                .method("is_initialized", &context_required_object::is_initialized)
                .method("set_data", &context_required_object::set_data)
                .method("get_data", &context_required_object::get_data)
                .property("data",
                    &context_required_object::get_data,
                    &context_required_object::set_data)
                .deserialization_factory<test_deserialization_context>([](test_deserialization_context* ctx) {
                    return std::make_shared<context_required_object>(ctx->service_name, ctx->dependency_value);
                })
                .build();

            // Create an object and serialize it
            auto original = std::make_shared<context_required_object>("OriginalService", 100);
            original->set_data("test_data");

            // Wrap in class_instance for serialization
            auto class_def = eng->get_class_definition("ContextRequiredObject");
            auto instance = class_def->create_instance();
            instance->set_field(eng->symbolize(class_constants::CPP_OBJECT_FIELD),
                script_value::make_cpp_object("ContextRequiredObject", class_def->get_type_id(), original, eng.get()));
            script_value obj_val = script_value::make_object("ContextRequiredObject", instance, eng.get());

            // Serialize to binary
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.write_value(obj_val);

            // Deserialize with context
            test_deserialization_context ctx;
            ctx.service_name = "DeserializedService";
            ctx.dependency_value = 777;

            serialization::binary_archive_reader reader(buffer, eng.get());
            reader.set_user_context(&ctx);

            script_value loaded_val = reader.read_value();

            // Verify object was constructed with context values
            auto loaded_instance = loaded_val.as<std::shared_ptr<class_instance>>();
            auto loaded_obj = loaded_instance->get_cpp_object_as<context_required_object>();

            check_eq(loaded_obj->is_initialized(), true);
            check_eq(loaded_obj->get_service_name(), std::string("DeserializedService"));
            check_eq(loaded_obj->get_value(), 777);
            check_eq(loaded_obj->get_data(), std::string("test_data"));  // Property was hydrated
        });

        test("archive_only_factory", [this]() {
            auto eng = engine::make();

            // Register class with archive-only factory (pre-reads properties)
            class_builder<property_preread_object>(eng, "PropertyPrereadObject")
                .method("get_id", &property_preread_object::get_id)
                .method("get_constructor_value", &property_preread_object::get_constructor_value)
                .method("get_name", &property_preread_object::get_name)
                .method("set_name", &property_preread_object::set_name)
                .property("id", &property_preread_object::get_id, nullptr)
                .property("constructor_value", &property_preread_object::get_constructor_value, nullptr)
                .property("name", &property_preread_object::get_name, &property_preread_object::set_name)
                .deserialization_factory([](serialization::archive_reader& archive) {
                    // Pre-read properties needed for construction
                    std::string id = archive.read_property<std::string>("id");
                    int value = archive.read_property<int>("constructor_value");
                    return std::make_shared<property_preread_object>(id, value);
                })
                .build();

            // Create an object and serialize it
            auto original = std::make_shared<property_preread_object>("obj_123", 456);
            original->set_name("TestObject");

            // Wrap in class_instance for serialization
            auto class_def = eng->get_class_definition("PropertyPrereadObject");
            auto instance = class_def->create_instance();
            instance->set_field(eng->symbolize(class_constants::CPP_OBJECT_FIELD),
                script_value::make_cpp_object("PropertyPrereadObject", class_def->get_type_id(), original, eng.get()));
            script_value obj_val = script_value::make_object("PropertyPrereadObject", instance, eng.get());

            // Serialize to binary
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.write_value(obj_val);

            // Deserialize
            serialization::binary_archive_reader reader(buffer, eng.get());
            script_value loaded_val = reader.read_value();

            // Verify object was constructed with pre-read values
            auto loaded_instance = loaded_val.as<std::shared_ptr<class_instance>>();
            auto loaded_obj = loaded_instance->get_cpp_object_as<property_preread_object>();

            check_eq(loaded_obj->get_id(), std::string("obj_123"));
            check_eq(loaded_obj->get_constructor_value(), 456);
            check_eq(loaded_obj->get_name(), std::string("TestObject"));  // Property was hydrated
        });

        test("context_and_archive_factory", [this]() {
            auto eng = engine::make();

            // Register class with both context and archive dependencies
            class_builder<complex_object>(eng, "ComplexObject")
                .method("get_service_name", &complex_object::get_service_name)
                .method("get_id", &complex_object::get_id)
                .method("get_value", &complex_object::get_value)
                .method("set_value", &complex_object::set_value)
                .property("service_name", &complex_object::get_service_name, nullptr)
                .property("id", &complex_object::get_id, nullptr)
                .property("value", &complex_object::get_value, &complex_object::set_value)
                .deserialization_factory<test_deserialization_context>(
                    [](test_deserialization_context* ctx, serialization::archive_reader& archive) {
                        // Get service from context, pre-read ID from archive
                        std::string id = archive.read_property<std::string>("id");
                        return std::make_shared<complex_object>(ctx->service_name, id);
                    })
                .build();

            // Create an object and serialize it
            auto original = std::make_shared<complex_object>("OriginalService", "original_id");
            original->set_value(999);

            // Wrap in class_instance for serialization
            auto class_def = eng->get_class_definition("ComplexObject");
            auto instance = class_def->create_instance();
            instance->set_field(eng->symbolize(class_constants::CPP_OBJECT_FIELD),
                script_value::make_cpp_object("ComplexObject", class_def->get_type_id(), original, eng.get()));
            script_value obj_val = script_value::make_object("ComplexObject", instance, eng.get());

            // Serialize to binary
            std::vector<uint8_t> buffer;
            serialization::binary_archive_writer writer(buffer, eng.get());
            writer.write_value(obj_val);

            // Deserialize with context
            test_deserialization_context ctx;
            ctx.service_name = "NewService";

            serialization::binary_archive_reader reader(buffer, eng.get());
            reader.set_user_context(&ctx);

            script_value loaded_val = reader.read_value();

            // Verify object was constructed with both context and pre-read values
            auto loaded_instance = loaded_val.as<std::shared_ptr<class_instance>>();
            auto loaded_obj = loaded_instance->get_cpp_object_as<complex_object>();

            check_eq(loaded_obj->get_service_name(), std::string("NewService"));  // From context
            check_eq(loaded_obj->get_id(), std::string("original_id"));  // Pre-read from archive
            check_eq(loaded_obj->get_value(), 999);  // Hydrated after construction
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::factory_deserialization_tests)
