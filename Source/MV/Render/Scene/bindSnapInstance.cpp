#include "bindSnapInstance.h"

#include <algorithm>
#include <map>
#include <set>
#include <typeindex>

#include "MV/Utility/log.h"
#include "MV/Utility/scopeGuard.hpp"
#include "MV/Utility/generalUtility.h"
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/properties/property_serialization.hpp>

namespace MV {
	namespace Scene {

		namespace {
			// One property written and read as an object of one key, the way its owner's archive holds it.
			struct SinglePropertyRecord {
				jai::property_base* property = nullptr;

				template<class Archive>
				void save(Archive& a_archive, std::uint32_t const) const {
					jai::serialization::any_archive_writer erased(a_archive);
					property->serialize(erased);
				}

			};

			std::string trimmed(const std::string& a_text) {
				const char* blank = " \t\r\n";
				const auto first = a_text.find_first_not_of(blank);
				if (first == std::string::npos) {
					return {};
				}
				return a_text.substr(first, a_text.find_last_not_of(blank) - first + 1);
			}

			// The saved form of a property's value as JSON text; empty when it writes nothing.
			std::string savedValue(jai::property_base* a_property, MV::Services& a_services) {
				SinglePropertyRecord record { a_property };
				const std::string object = jai::to_json(*a_services.get<jai::engine>(), record, a_services, 0);
				const std::string key = "\"" + a_property->name() + "\"";
				const auto keyAt = object.find(key);
				const auto close = object.rfind('}');
				if (keyAt == std::string::npos || close == std::string::npos) {
					return {};
				}
				const auto colon = object.find(':', keyAt + key.size());
				if (colon == std::string::npos || colon > close) {
					return {};
				}
				return trimmed(object.substr(colon + 1, close - colon - 1));
			}

			// The JSON text of a scene file as values with their places in it, so one can be swapped for another
			// before the text is loaded. Only what finding a node, a component and a property needs.
			class JsonText {
			public:
				struct Value {
					size_t begin = 0;
					size_t end = 0;
					bool object = false;
					std::vector<std::pair<std::string, size_t>> members;
					std::vector<size_t> elements;
				};

				explicit JsonText(const std::string& a_text) : text(a_text) {
					size_t at = 0;
					rootValue = parse(at);
					for (size_t index = 0; index < values.size(); ++index) {
						if (member(index, "$val") != none) {
							auto id = member(index, "$id");
							if (id != none) {
								definitions[raw(id)] = index;
							}
						}
					}
				}

				static constexpr size_t none = static_cast<size_t>(-1);

				size_t root() const { return rootValue; }
				const Value& value(size_t a_index) const { return values[a_index]; }

				size_t member(size_t a_index, const std::string& a_key) const {
					if (a_index == none) {
						return none;
					}
					for (auto&& [key, index] : values[a_index].members) {
						if (key == a_key) {
							return index;
						}
					}
					return none;
				}

				std::string raw(size_t a_index) const {
					return a_index == none ? std::string() : text.substr(values[a_index].begin, values[a_index].end - values[a_index].begin);
				}

				// a string value without its quotes
				std::string string(size_t a_index) const {
					auto quoted = raw(a_index);
					return quoted.size() >= 2 && quoted.front() == '"' ? quoted.substr(1, quoted.size() - 2) : quoted;
				}

				// The object holding $type and $val for an entry that is one or that only names one by $id.
				size_t definition(size_t a_index) const {
					if (a_index == none || member(a_index, "$val") != none) {
						return a_index;
					}
					auto found = definitions.find(raw(member(a_index, "$id")));
					return found == definitions.end() ? none : found->second;
				}

				size_t childNode(size_t a_node, const std::string& a_id) const {
					auto children = member(member(a_node, "$val"), "childNodes");
					if (children == none) {
						return none;
					}
					for (auto entry : values[children].elements) {
						auto child = definition(entry);
						if (child != none && string(member(member(child, "$val"), "nodeId")) == a_id) {
							return child;
						}
					}
					return none;
				}

				// By address: the type name, with #1 for the second of a type on the node.
				size_t component(size_t a_node, const std::string& a_address) const {
					auto components = member(member(a_node, "$val"), "childComponents");
					if (components == none) {
						return none;
					}
					std::map<std::string, int> seen;
					for (auto entry : values[components].elements) {
						auto found = definition(entry);
						if (found == none) {
							continue;
						}
						auto name = string(member(found, "$type"));
						const int index = seen[name]++;
						if ((index == 0 ? name : name + "#" + std::to_string(index)) == a_address) {
							return found;
						}
					}
					return none;
				}

			private:
				void skipBlank(size_t& a_at) const {
					while (a_at < text.size() && (text[a_at] == ' ' || text[a_at] == '\t' || text[a_at] == '\r' || text[a_at] == '\n')) {
						++a_at;
					}
				}

				void skipString(size_t& a_at) const {
					++a_at;
					while (a_at < text.size() && text[a_at] != '"') {
						a_at += text[a_at] == '\\' ? 2 : 1;
					}
					++a_at;
				}

				size_t parse(size_t& a_at) {
					skipBlank(a_at);
					const size_t index = values.size();
					values.emplace_back();
					values[index].begin = a_at;
					if (a_at < text.size() && text[a_at] == '{') {
						values[index].object = true;
						++a_at;
						skipBlank(a_at);
						while (a_at < text.size() && text[a_at] != '}') {
							const size_t keyBegin = a_at;
							skipString(a_at);
							std::string key = text.substr(keyBegin + 1, a_at - keyBegin - 2);
							skipBlank(a_at);
							++a_at;
							const size_t memberIndex = parse(a_at);
							values[index].members.emplace_back(std::move(key), memberIndex);
							skipBlank(a_at);
							if (a_at < text.size() && text[a_at] == ',') {
								++a_at;
								skipBlank(a_at);
							}
						}
						++a_at;
					} else if (a_at < text.size() && text[a_at] == '[') {
						++a_at;
						skipBlank(a_at);
						while (a_at < text.size() && text[a_at] != ']') {
							const size_t element = parse(a_at);
							values[index].elements.push_back(element);
							skipBlank(a_at);
							if (a_at < text.size() && text[a_at] == ',') {
								++a_at;
								skipBlank(a_at);
							}
						}
						++a_at;
					} else if (a_at < text.size() && text[a_at] == '"') {
						skipString(a_at);
					} else {
						while (a_at < text.size() && text[a_at] != ',' && text[a_at] != '}' && text[a_at] != ']' && text[a_at] != ' ' && text[a_at] != '\t' && text[a_at] != '\r' && text[a_at] != '\n') {
							++a_at;
						}
					}
					values[index].end = std::min(a_at, text.size());
					return index;
				}

				const std::string& text;
				std::vector<Value> values;
				std::map<std::string, size_t> definitions;
				size_t rootValue = none;
			};

			// A value holding other objects is rebuilt, not assigned, when it is read back: never an override.
			bool holdsObjects(const std::string& a_value) {
				return a_value.find("\"$id\"") != std::string::npos || a_value.find("\"$ref\"") != std::string::npos;
			}

			std::string typeNameOf(const std::shared_ptr<Component>& a_component) {
				auto& held = *a_component;
				if (auto registered = jai::type_name_registry::instance().get_name(std::type_index(typeid(held)))) {
					return std::string(*registered);
				}
				std::string name = typeid(held).name();
				auto lastColon = name.rfind("::");
				return lastColon == std::string::npos ? name : name.substr(lastColon + 2);
			}

			// "Sprite" for the first Sprite in a list and "Sprite#1" for the second.
			std::vector<std::string> componentAddresses(const std::vector<std::shared_ptr<Component>>& a_components) {
				std::vector<std::string> result;
				std::map<std::string, int> seen;
				for (auto&& component : a_components) {
					auto name = typeNameOf(component);
					const int index = seen[name]++;
					result.push_back(index == 0 ? name : name + "#" + std::to_string(index));
				}
				return result;
			}

			std::vector<std::shared_ptr<Component>> componentsOf(const std::shared_ptr<Node>& a_node) {
				std::vector<std::shared_ptr<Component>> result;
				for (auto&& component : a_node->components<Component>(false)) {
					std::visit([&](const auto& a_safe) { result.push_back(a_safe.self()); }, component);
				}
				return result;
			}

			bool structural(const std::string& a_property) {
				return a_property == "childNodes" || a_property == "childComponents";
			}

			std::string keyOf(const std::string& a_node, const std::string& a_component, const std::string& a_property) {
				return a_node + "\n" + a_component + "\n" + a_property;
			}

			// Each saved property an override may name: of a list of components at a path, and of the nodes below.
			// Owner is Node or Component; each roots its own property_owner chain.
			template<typename Owner, typename Visit>
			void visitProperties(Owner& a_owner, const std::string& a_node, const std::string& a_component, Visit& a_visit) {
				a_owner.visit_reflected_properties([&](const std::string& a_name, jai::property_base* a_property) {
					if (a_property->allow_save() && !structural(a_name)) {
						a_visit(a_node, a_component, a_name, a_property);
					}
				});
			}

			template<typename Visit>
			void visitComponents(const std::vector<std::shared_ptr<Component>>& a_components, const std::string& a_node, Visit& a_visit) {
				auto addresses = componentAddresses(a_components);
				for (size_t index = 0; index < a_components.size(); ++index) {
					visitProperties(*a_components[index], a_node, addresses[index], a_visit);
				}
			}

			template<typename Visit>
			void visitNode(const std::shared_ptr<Node>& a_node, const std::string& a_path, Visit& a_visit) {
				visitProperties(*a_node, a_path, "", a_visit);
				visitComponents(componentsOf(a_node), a_path, a_visit);
				for (auto&& child : *a_node) {
					visitNode(child, a_path + "/" + child->id(), a_visit);
				}
			}
		}

		void BindSnapInstance::jai_auto_bind(jai::dynamic_binder<BindSnapInstance>& a_builder) {
			a_builder.method("source", static_cast<std::string(BindSnapInstance::*)() const>(&BindSnapInstance::source));
			a_builder.method("source", static_cast<std::shared_ptr<BindSnapInstance>(BindSnapInstance::*)(const std::string&)>(&BindSnapInstance::source), jai::serial);
			a_builder.method("built", &BindSnapInstance::built);
			a_builder.method("refresh", &BindSnapInstance::refresh, jai::serial);
			a_builder.method("revert", &BindSnapInstance::revert, jai::serial);
			a_builder.method("unpack", &BindSnapInstance::unpack, jai::serial);
		}

		BindSnapInstance::BindSnapInstance(const std::weak_ptr<Node>& a_owner, MV::Services& a_services) :
			jai::property_owner<BindSnapInstance, Component>(a_owner),
			services(a_services) {
		}

		std::shared_ptr<BindSnapInstance> BindSnapInstance::source(const std::string& a_fileName) {
			auto self = std::static_pointer_cast<BindSnapInstance>(shared_from_this());
			removeContent();
			sourceFile = a_fileName;
			overrideList = std::vector<BindSnapOverride>();
			build();
			return self;
		}

		std::shared_ptr<BindSnapInstance> BindSnapInstance::sourceOfPresentContent(const std::string& a_fileName) {
			auto self = std::static_pointer_cast<BindSnapInstance>(shared_from_this());
			sourceFile = a_fileName;
			overrideList = std::vector<BindSnapOverride>();
			if (ownerIsAlive()) {
				for (auto&& component : componentsOf(owner())) {
					if (component != self) {
						component->serializable(false);
					}
				}
				for (auto&& child : *owner()) {
					child->serializable(false);
				}
				isBuilt = true;
				rememberContent();
			}
			return self;
		}

		std::vector<std::shared_ptr<Component>> BindSnapInstance::contentComponents() const {
			std::vector<std::shared_ptr<Component>> result;
			if (isBuilt && ownerIsAlive()) {
				for (auto&& component : componentsOf(owner())) {
					if (!component->serializable() && component.get() != this) {
						result.push_back(component);
					}
				}
			}
			return result;
		}

		std::vector<std::shared_ptr<Node>> BindSnapInstance::contentChildren() const {
			std::vector<std::shared_ptr<Node>> result;
			if (isBuilt && ownerIsAlive()) {
				for (auto&& child : *owner()) {
					if (!child->serializable()) {
						result.push_back(child);
					}
				}
			}
			return result;
		}

		std::shared_ptr<BindSnapInstance> BindSnapInstance::holding(const std::shared_ptr<Node>& a_node) {
			if (!a_node) {
				return nullptr;
			}
			auto child = a_node;
			for (auto above = a_node->parent(); above; child = above, above = above->parent()) {
				if (!child->serializable()) {
					auto instance = above->component<BindSnapInstance>(false, false);
					if (instance && instance->built()) {
						return instance.self();
					}
				}
			}
			return nullptr;
		}

		bool BindSnapInstance::build() {
			if (isBuilt || sourceFile->empty() || !ownerIsAlive()) {
				return isBuilt;
			}
			std::shared_ptr<Node> loaded;
			try {
				auto fileText = fileContents(sourceFile);
				require<ResourceException>(!fileText.empty(), "File not found: ", *sourceFile);
				try {
					// the overrides go into the file's text, so the content loads with them as if the file held them
					loaded = Node::loadText(withOverrides(fileText), sourceFile, services, false);
				} catch (const std::exception& a_error) {
					if (overrideList->empty()) {
						throw;
					}
					MV::warning("BindSnapInstance [", owner()->id(), "] ", *sourceFile, " did not load with its overrides, which are dropped: ", a_error.what());
					overrideList = std::vector<BindSnapOverride>();
					loaded = Node::loadText(fileText, sourceFile, services, false);
				}
			} catch (const std::exception& a_error) {
				MV::error("BindSnapInstance [", owner()->id(), "] could not read ", *sourceFile, ": ", a_error.what());
				return false;
			}
			owner()->adoptLoaded(loaded);
			isBuilt = true;
			rememberContent();
			return true;
		}

		std::string BindSnapInstance::withOverrides(const std::string& a_fileText) {
			if (overrideList->empty()) {
				return a_fileText;
			}
			const std::string instanceId = ownerIsAlive() ? owner()->id() : std::string();
			JsonText file(a_fileText);
			struct Swap {
				size_t begin;
				size_t end;
				std::string text;
			};
			std::vector<Swap> swaps;
			std::vector<BindSnapOverride> kept;
			for (auto&& entry : *overrideList) {
				auto node = file.definition(file.root());
				size_t at = 0;
				while (node != JsonText::none && at < entry.node.size()) {
					auto slash = entry.node.find('/', at);
					node = file.childNode(node, entry.node.substr(at, slash == std::string::npos ? std::string::npos : slash - at));
					at = slash == std::string::npos ? entry.node.size() : slash + 1;
				}
				// the root's own properties are the instance's node's, never the file's
				const bool rootItself = entry.node.empty() && entry.component.empty();
				auto owner = rootItself || node == JsonText::none ? JsonText::none : entry.component.empty() ? node : file.component(node, entry.component);
				auto held = file.member(owner, "$val");
				if (held == JsonText::none) {
					MV::warning("BindSnapInstance [", instanceId, "] ", *sourceFile, " no longer has ", entry.node, entry.component.empty() ? "" : " ", entry.component, ": its override of ", entry.property, " is dropped");
					continue;
				}
				auto present = file.member(held, entry.property);
				if (present != JsonText::none) {
					swaps.push_back({ file.value(present).begin, file.value(present).end, entry.value });
				} else {
					const size_t inside = file.value(held).begin + 1;
					swaps.push_back({ inside, inside, "\"" + entry.property + "\": " + entry.value + (file.value(held).members.empty() ? "" : ",") });
				}
				kept.push_back(entry);
			}
			overrideList = kept;
			// from the end of the text back, so each place is still where it was found
			std::stable_sort(swaps.begin(), swaps.end(), [](const Swap& a_first, const Swap& a_second) { return a_first.begin > a_second.begin; });
			std::string result = a_fileText;
			for (auto&& swap : swaps) {
				result.replace(swap.begin, swap.end - swap.begin, swap.text);
			}
			return result;
		}

		void BindSnapInstance::removeContent() {
			if (isBuilt && ownerIsAlive()) {
				auto node = owner();
				for (auto&& child : contentChildren()) {
					node->remove(child);
				}
				for (auto&& component : contentComponents()) {
					node->detach(component);
				}
			}
			isBuilt = false;
			asLastSeen.clear();
		}

		template<typename Visit>
		void BindSnapInstance::visitContent(Visit&& a_visit) {
			visitComponents(contentComponents(), "", a_visit);
			for (auto&& child : contentChildren()) {
				visitNode(child, child->id(), a_visit);
			}
		}

		void BindSnapInstance::rememberContent() {
			asLastSeen.clear();
			tracked = services.get<BindSnapTracking>(false) != nullptr;
			if (!tracked) {
				return;
			}
			visitContent([&](const std::string& a_node, const std::string& a_component, const std::string& a_name, jai::property_base* a_property) {
				auto value = savedValue(a_property, services);
				if (!value.empty() && !holdsObjects(value)) {
					asLastSeen[keyOf(a_node, a_component, a_name)] = value;
				}
			});
		}

		void BindSnapInstance::captureOverrides() {
			if (!isBuilt || !tracked || !ownerIsAlive()) {
				return;
			}
			std::vector<BindSnapOverride> result = overrideList.get();
			std::set<std::string> present;
			visitContent([&](const std::string& a_node, const std::string& a_component, const std::string& a_name, jai::property_base* a_property) {
				const auto key = keyOf(a_node, a_component, a_name);
				auto seen = asLastSeen.find(key);
				if (seen == asLastSeen.end()) {
					return;
				}
				present.insert(key);
				auto value = savedValue(a_property, services);
				if (value == seen->second || value.empty() || holdsObjects(value)) {
					return;
				}
				seen->second = value;
				auto existing = std::find_if(result.begin(), result.end(), [&](const BindSnapOverride& a_entry) {
					return a_entry.node == a_node && a_entry.component == a_component && a_entry.property == a_name;
				});
				if (existing != result.end()) {
					existing->value = value;
				} else {
					result.push_back({ a_node, a_component, a_name, value });
				}
			});
			// what was deleted from the content since takes its overrides with it
			std::erase_if(result, [&](const BindSnapOverride& a_entry) {
				return !present.contains(keyOf(a_entry.node, a_entry.component, a_entry.property));
			});
			overrideList = result;
		}

		bool BindSnapInstance::refresh() {
			auto self = shared_from_this();
			captureOverrides();
			removeContent();
			return build();
		}

		bool BindSnapInstance::revert() {
			auto self = shared_from_this();
			removeContent();
			overrideList = std::vector<BindSnapOverride>();
			return build();
		}

		void BindSnapInstance::saveContentAs(const std::string& a_fileName) {
			if (!ownerIsAlive()) {
				return;
			}
			auto self = shared_from_this();
			auto node = owner();
			auto everything = componentsOf(node);
			for (auto&& component : everything) {
				component->serializable(component != self);
			}
			for (auto&& child : *node) {
				child->serializable(true);
			}
			SCOPE_EXIT {
				for (auto&& component : everything) {
					component->serializable(component == self);
				}
				for (auto&& child : *node) {
					child->serializable(false);
				}
			};
			node->saveJai(a_fileName, services, false);
			overrideList = std::vector<BindSnapOverride>();
			isBuilt = true;
			rememberContent();
		}

		void BindSnapInstance::unpack() {
			auto self = shared_from_this();
			if (!ownerIsAlive()) {
				return;
			}
			auto node = owner();
			for (auto&& component : contentComponents()) {
				component->serializable(true);
			}
			for (auto&& child : contentChildren()) {
				child->serializable(true);
			}
			isBuilt = false;
			asLastSeen.clear();
			node->detach(self);
		}

		std::shared_ptr<Component> BindSnapInstance::cloneImplementation(const std::shared_ptr<Node>& a_parent) {
			// a clone does not carry what is not saved: it builds the file itself, with this instance's changes
			captureOverrides();
			return cloneHelper(a_parent->attach<BindSnapInstance>(services).self());
		}

		void BindSnapInstance::postLoadInitialize() {
			build();
		}

	}
}
