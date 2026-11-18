#ifndef _MV_SCENE_BINDSNAPINSTANCE_H_
#define _MV_SCENE_BINDSNAPINSTANCE_H_

#include <map>
#include <string>
#include <vector>
#include "node.h"
#include "MV/Utility/services.hpp"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		// One property a linked instance holds a value of its own for. node is the path of node ids below the
		// instance's node ("" for that node itself), component the component's type name ("" for the node's own
		// properties, "Sprite#1" for the second Sprite on a node), and value the property's saved form as JSON text.
		struct BindSnapOverride {
			std::string node;
			std::string component;
			std::string property;
			std::string value;

			bool operator==(const BindSnapOverride&) const = default;

			template<class Archive>
			void serialize(Archive& a_archive) {
				a_archive(
					jai::serialization::make_nvp("node", node),
					jai::serialization::make_nvp("component", component),
					jai::serialization::make_nvp("property", property),
					jai::serialization::make_nvp("value", value));
			}
		};

		// Connected to a Services by an editor. Instances built with those services remember their content as it
		// was built, so captureOverrides can tell what was changed since. Without it nothing is remembered and
		// an instance's overrides stay as they were loaded.
		struct BindSnapTracking {};

		// Links its node to a file holding a node: a .bindsnap by convention, a .scene as well. The file's root gives the node its components and children, which are
		// the node's own from then on but are not saved with it: the scene keeps the file's name and the overrides,
		// the properties this instance holds its own value of. The node's id and transform are the instance's.
		// Whatever else is put on the node (a component, a child of the node itself) is saved with the node as usual;
		// what is added deeper, under a child the file gave, is not saved.
		// Everything on the node that is not saved counts as the file's content.
		class BindSnapInstance : public jai::property_owner<BindSnapInstance, Component> {
			friend Node;
			friend jai::access;

		public:
			static void jai_auto_bind(jai::dynamic_binder<BindSnapInstance>& a_builder);

			ComponentDerivedAccessors(BindSnapInstance)

			// The file, named as Node::make takes it. Setting it drops the overrides and builds that file's content.
			std::string source() const { return sourceFile; }
			std::shared_ptr<BindSnapInstance> source(const std::string& a_fileName);
			// As source, for a node being placed: the node also takes the position, rotation and scale of the file's root.
			std::shared_ptr<BindSnapInstance> placeSource(const std::string& a_fileName);
			// The file was renamed or moved: the link follows it, and nothing on the node changes.
			std::shared_ptr<BindSnapInstance> retarget(const std::string& a_fileName) {
				sourceFile = a_fileName;
				return std::static_pointer_cast<BindSnapInstance>(shared_from_this());
			}
			// The id the file's root had when the content was last built from it.
			const std::string& sourceRootId() const { return rootId; }
			// Links a node that already holds the file's content, as when the file was just written from it:
			// everything on the node becomes the file's and nothing is loaded.
			std::shared_ptr<BindSnapInstance> sourceOfPresentContent(const std::string& a_fileName);

			const std::vector<BindSnapOverride>& overrides() const { return *overrideList; }
			// Replaces the overrides and rebuilds the content with them.
			std::shared_ptr<BindSnapInstance> overrides(const std::vector<BindSnapOverride>& a_overrides);
			// The file's content is on the node. False until the node is loaded or a source is set, and after a file
			// that could not be read.
			bool built() const { return isBuilt; }

			// Adds to the overrides each saved property of the content that changed since it was built, or since this
			// was last called. A property holding other objects (a node, a component) is never one. An editor calls
			// it before a scene is saved; it needs BindSnapTracking.
			void captureOverrides();
			// Reads the file again and rebuilds the content, keeping what the instance had changed.
			bool refresh();
			// Takes the content as it stands for unchanged, as when an editor has put it in its own resting state.
			void markUnchanged() { rememberContent(); }
			// Drops the overrides and rebuilds the content as the file has it.
			bool revert();
			// Writes the node as it stands, content and all, as a plain .bindsnap with no link in it. What is on the
			// node is then that file's content and there are no overrides. The link is not changed: writing to the
			// source applies this instance to it.
			void saveContentAs(const std::string& a_fileName);
			// Cuts the link: the content becomes the node's own, saved with it, and this component leaves the node.
			void unpack();

			// The file's components and children as they are on the node.
			std::vector<std::shared_ptr<Component>> contentComponents() const;
			std::vector<std::shared_ptr<Node>> contentChildren() const;
			// A file's name as links compare it: forward slashes, and no "./" or "Assets/" before it.
			static std::string sourceName(const std::string& a_fileName);
			// The nearest instance whose content a node is, or is under; null for a node that is saved as it is.
			static std::shared_ptr<BindSnapInstance> holding(const std::shared_ptr<Node>& a_node);

		protected:
			BindSnapInstance(const std::weak_ptr<Node>& a_owner, MV::Services& a_services);

			template<class Archive>
			void save(Archive& archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<BindSnapInstance>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services);
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) override;

			void postLoadInitialize() override;

		private:
			bool build(bool a_takeRootTransform = false);
			void removeContent();
			std::string withOverrides(const std::string& a_fileText);
			void rememberContent();
			template<typename Visit>
			void visitContent(Visit&& a_visit);

			JAI_NAMED_PROPERTY((std::string), "source", sourceFile);
			JAI_NAMED_PROPERTY((std::vector<BindSnapOverride>), "overrides", overrideList);

			MV::Services& services;
			std::string rootId;
			bool isBuilt = false;
			bool tracked = false;
			// node, component and property to the saved value last seen there
			std::map<std::string, std::string> asLastSeen;
		};

	}
}

#endif
