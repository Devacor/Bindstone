#include "inspector.h"
#include "MV/Render/Scene/emitter.h"
#include "MV/Render/Scene/spineMV.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <sstream>
#include <typeindex>

namespace Workbench {

	namespace {
		// above the panels' Clickables: a picker tile takes the press that lands on it
		constexpr int64_t pickerPriority = 5000;
		constexpr size_t rowsPerListPage = 12;
		constexpr size_t pickerColumns = 6;
		constexpr size_t pickerRows = 4;

		// A property whose value is itself a property owner: its own properties show as a group under it.
		struct NestedOwner {
			std::function<void(jai::property_base*, const std::function<void(const std::string&, jai::property_base*)>&)> visit;
			std::function<jai::property_base*(jai::property_base*, const std::string&)> find;
		};

		template <typename OwnerType>
		NestedOwner nestedOwnerOf() {
			NestedOwner result;
			result.visit = [](jai::property_base* a_property, const std::function<void(const std::string&, jai::property_base*)>& a_visit) {
				a_property->value_as<OwnerType>()->visit_reflected_properties(a_visit);
			};
			result.find = [](jai::property_base* a_property, const std::string& a_name) {
				return a_property->value_as<OwnerType>()->find_reflected_property(a_name);
			};
			return result;
		}

		const std::map<std::type_index, NestedOwner>& nestedOwners() {
			static const std::map<std::type_index, NestedOwner> result {
				{ std::type_index(typeid(MV::Scene::EmitterSpawnProperties)), nestedOwnerOf<MV::Scene::EmitterSpawnProperties>() },
				{ std::type_index(typeid(MV::Scene::Spine::FileBundle)), nestedOwnerOf<MV::Scene::Spine::FileBundle>() }
			};
			return result;
		}

		std::string nodeName(const std::shared_ptr<MV::Scene::Node>& a_node) {
			return !a_node ? std::string("None") : a_node->id().empty() ? std::string("(unnamed node)") : a_node->id();
		}

		std::string countText(size_t a_count, const char* a_singular, const char* a_plural) {
			return std::to_string(a_count) + " " + (a_count == 1 ? a_singular : a_plural);
		}

		std::string fileNameOf(std::string a_path) {
			auto lastSlash = a_path.find_last_of("/\\");
			return lastSlash == std::string::npos ? a_path : a_path.substr(lastSlash + 1);
		}

		std::string textureName(const std::shared_ptr<MV::TextureHandle>& a_handle) {
			if (!a_handle || !a_handle->texture()) {
				return "None";
			}
			std::string result = fileNameOf(a_handle->texture()->name());
			if (!a_handle->name().empty() && a_handle->name() != result) {
				result += " : " + a_handle->name();
			}
			return result;
		}

		struct TextureSource {
			std::string title;
			std::vector<std::pair<std::string, std::function<std::shared_ptr<MV::TextureHandle>()>>> entries;
		};

		// Loose image files first, then each atlas pack: what the old editor's picker offered.
		std::vector<TextureSource> textureSources(MV::SharedTextures& a_textures) {
			std::vector<TextureSource> result;
			TextureSource files;
			files.title = "Image files";
			auto* textures = &a_textures;
			for (auto&& [fileId, repeat] : a_textures.fileIds()) {
				files.entries.emplace_back(fileNameOf(fileId), [textures, fileId = fileId, repeat = repeat]() {
					return textures->file(fileId, repeat)->makeHandle();
				});
			}
			result.push_back(std::move(files));
			for (auto&& packId : a_textures.packIds()) {
				TextureSource pack;
				pack.title = "Atlas: " + packId;
				auto found = a_textures.pack(packId);
				if (found) {
					for (auto&& handleId : found->handleIds()) {
						pack.entries.emplace_back(handleId, [found, handleId]() { return found->handle(handleId); });
					}
				}
				result.push_back(std::move(pack));
			}
			return result;
		}

		const char* drawTypeLabel(GLenum a_drawType) {
			static const char* const labels[] = { "Points", "Lines", "Line loop", "Line strip", "Triangles", "Triangle strip", "Triangle fan" };
			return a_drawType < std::size(labels) ? labels[a_drawType] : "Custom";
		}
	}

	std::string InspectorPanel::leafName(const std::string& a_path) {
		auto lastSlash = a_path.rfind('/');
		return lastSlash == std::string::npos ? a_path : a_path.substr(lastSlash + 1);
	}

	jai::property_base* InspectorPanel::resolveProperty(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_path) {
		size_t start = 0;
		jai::property_base* result = nullptr;
		while (start <= a_path.size()) {
			size_t end = a_path.find('/', start);
			std::string segment = a_path.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (!result) {
				result = a_component->find_reflected_property(segment);
			} else {
				auto nested = nestedOwners().find(result->value_type_id());
				result = nested == nestedOwners().end() ? nullptr : nested->second.find(result, segment);
			}
			if (!result || end == std::string::npos) {
				break;
			}
			start = end + 1;
		}
		return result;
	}

	std::vector<InspectorPanel::NodeReference> InspectorPanel::nodeReferences(const std::shared_ptr<MV::Scene::Component>& a_component) {
		std::vector<NodeReference> result;
		std::weak_ptr<MV::Scene::Component> weakComponent = a_component;
		auto assigning = [weakComponent](const std::string& a_path, auto a_value) {
			return std::function<void()>([weakComponent, a_path, a_value]() {
				auto locked = weakComponent.lock();
				auto* property = locked ? resolveProperty(locked, a_path) : nullptr;
				if (property && property->assign_value(a_value)) {
					notifyEdited(locked);
				}
			});
		};
		std::function<void(const std::string&, jai::property_base*)> consider = [&](const std::string& a_path, jai::property_base* a_property) {
			if (leafName(a_path) == "componentOwner") {
				return;
			}
			if (auto* held = a_property->value_as<std::shared_ptr<MV::Scene::Node>>()) {
				if (*held) {
					result.push_back({ *held, assigning(a_path, std::shared_ptr<MV::Scene::Node>()), assigning(a_path, *held) });
				}
			} else if (auto* watched = a_property->value_as<std::weak_ptr<MV::Scene::Node>>()) {
				if (auto target = watched->lock()) {
					result.push_back({ target, assigning(a_path, std::weak_ptr<MV::Scene::Node>()), assigning(a_path, *watched) });
				}
			} else if (auto nested = nestedOwners().find(a_property->value_type_id()); nested != nestedOwners().end()) {
				nested->second.visit(a_property, [&](const std::string& a_name, jai::property_base* a_nestedProperty) {
					consider(a_path + "/" + a_name, a_nestedProperty);
				});
			}
		};
		a_component->visit_reflected_properties(consider);

		if (auto drawable = std::dynamic_pointer_cast<MV::Scene::Drawable>(a_component); drawable && drawable->anchors().hasParent()) {
			std::weak_ptr<MV::Scene::Drawable> weakDrawable = drawable;
			std::weak_ptr<MV::Scene::Drawable> anchoredTo = drawable->anchors().parent();
			result.push_back({ drawable->anchors().parent()->owner(),
				[weakDrawable]() {
					if (auto locked = weakDrawable.lock()) {
						locked->anchors().removeFromParent();
					}
				},
				[weakDrawable, anchoredTo]() {
					if (auto locked = weakDrawable.lock()) {
						locked->anchors().parent(anchoredTo);
					}
				} });
		}
		return result;
	}

	bool InspectorPanel::addGroupHeader(const std::string& a_title, const std::string& a_key, const std::string& a_summary) {
		bool open = openGroups.count(a_key) && openGroups[a_key];
		auto row = contentNode->make(MV::guid("group_"));
		labelledRows.emplace_back(a_title, row);
		auto arrowHost = row->make("fold");
		arrowHost->position(MV::point(rowIndent - theme->px(2.0f), 0.0f));
		makeExpanderArrow(arrowHost, *services, *theme, MV::size(theme->px(16.0f), theme->fieldHeight), open);
		auto title = makeLabel(row, *services, *theme, "label", MV::size(labelWidth() - rowIndent - theme->px(14.0f), theme->fieldHeight), a_title);
		title->owner()->position(MV::point(rowIndent + theme->px(14.0f), 0.0f));
		auto summary = makeLabel(row, *services, *theme, "summary", MV::size(fieldWidth() - theme->px(5.0f), theme->fieldHeight), a_summary);
		summary->owner()->position(MV::point(labelWidth() + theme->pad + theme->px(5.0f), 0.0f));
		auto& mouse = *services->get<MV::TapDevice>();
		row->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(panelWidth, theme->fieldHeight))->onAccept.connect("toggle", [this, a_key, open](const std::shared_ptr<MV::Scene::Clickable>&) {
			openGroups[a_key] = !open;
			rebuild();
		});
		return open;
	}

	void InspectorPanel::addNodeReferenceRow(const std::string& a_label, const std::shared_ptr<MV::Scene::Node>& a_node, std::function<void(const std::shared_ptr<MV::Scene::Node>&)> a_assign) {
		auto row = makeRow(a_label);
		auto fieldHost = row->make("field");
		fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
		const float clearWidth = a_assign && a_node ? theme->fieldHeight + theme->px(2.0f) : 0.0f;
		MV::Size<> chipSize(fieldWidth() - clearWidth, theme->fieldHeight);
		std::weak_ptr<MV::Scene::Node> weakNode = a_node;
		auto chip = makeButton(fieldHost, *services, *theme, "reference", chipSize, nodeName(a_node), [this, weakNode]() {
			if (auto node = weakNode.lock(); node && onSelectNode) {
				onSelectNode(node);
			}
		});
		if (a_assign) {
			nodeDropZones.push_back({ chip->owner(), a_assign });
			if (a_node) {
				auto clear = makeIconButton(fieldHost, *services, *theme, "clear", MV::size(theme->fieldHeight, theme->fieldHeight), Icon::Close, [a_assign]() {
					a_assign(nullptr);
				}, "Clear");
				clear->owner()->position(MV::point(chipSize.width + theme->px(2.0f), 0.0f));
			}
		}
	}

	std::optional<MV::Point<int>> InspectorPanel::rowScreenPoint(const std::string& a_label, bool a_field) const {
		for (auto&& [label, weakRow] : labelledRows) {
			auto row = weakRow.lock();
			if (label == a_label && row) {
				auto field = a_field ? row->get("field", false) : nullptr;
				return (field ? field : row)->screenBounds().centerPoint();
			}
		}
		return std::nullopt;
	}

	std::optional<MV::Point<int>> InspectorPanel::texturePickerTileScreenPoint(size_t a_index) const {
		auto popup = popupNode ? popupNode->get("texturePopup", false) : nullptr;
		auto tiles = popup ? popup->get("tiles", false) : nullptr;
		if (!tiles || a_index >= static_cast<size_t>(std::distance(tiles->begin(), tiles->end()))) {
			return std::nullopt;
		}
		return (*std::next(tiles->begin(), static_cast<std::ptrdiff_t>(a_index)))->screenBounds().centerPoint();
	}

	bool InspectorPanel::dropNode(const std::shared_ptr<MV::Scene::Node>& a_node, const MV::Point<int>& a_screenPoint) {
		for (auto&& zone : nodeDropZones) {
			auto row = zone.row.lock();
			if (row && row->screenBounds().contains(a_screenPoint)) {
				auto assign = zone.assign;   // assigning rebuilds the rows and this list with them
				assign(a_node);
				return true;
			}
		}
		return false;
	}

	size_t InspectorPanel::addPager(const std::string& a_key, size_t a_count, size_t a_perPage) {
		const size_t pageCount = std::max<size_t>(1, (a_count + a_perPage - 1) / a_perPage);
		size_t& page = listPages[a_key];
		page = std::min(page, pageCount - 1);
		if (pageCount > 1) {
			auto row = makeRow("");
			auto host = row->make("pager");
			host->position(MV::point(labelWidth() + theme->pad, 0.0f));
			MV::Size<> stepSize(theme->fieldHeight * 1.4f, theme->fieldHeight);
			auto previous = makeIconButton(host, *services, *theme, "previous", stepSize, Icon::StepBack, [this, a_key]() {
				if (listPages[a_key] > 0) {
					--listPages[a_key];
					rebuild();
				}
			}, "Previous page");
			auto label = makeLabel(host, *services, *theme, "page", MV::size(theme->px(90.0f), theme->fieldHeight), std::to_string(page * a_perPage) + " - " + std::to_string(std::min(a_count, (page + 1) * a_perPage) - 1) + " of " + std::to_string(a_count));
			label->owner()->position(MV::point(stepSize.width + theme->px(6.0f), 0.0f));
			auto next = makeIconButton(host, *services, *theme, "next", stepSize, Icon::StepForward, [this, a_key, pageCount]() {
				if (listPages[a_key] + 1 < pageCount) {
					++listPages[a_key];
					rebuild();
				}
			}, "Next page");
			next->owner()->position(MV::point(stepSize.width + theme->px(100.0f), 0.0f));
			(void)previous;
		}
		return page * a_perPage;
	}

	// The neighbours a Clickable names for keyboard and controller moves: a row a link, each a node dropped on it.
	// A link left empty is worked out from the other focusable controls.
	void InspectorPanel::addFocusLinksGroup(const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
		size_t named = 0;
		for (int link = 0; link <= static_cast<int>(MV::FocusLink::DOWN_RIGHT); ++link) {
			named += a_clickable->focusLink(static_cast<MV::FocusLink>(link)).empty() ? 0 : 1;
		}
		if (!addGroupHeader("focus links", "focusLinks#" + a_clickable->id(), named == 0 ? std::string("automatic") : countText(named, "named", "named"))) {
			return;
		}
		rowIndent += theme->px(12.0f);
		std::weak_ptr<MV::Scene::Clickable> weakClickable = a_clickable;
		auto top = a_clickable->owner();
		while (top && top->parent()) {
			top = top->parent();
		}
		for (int index = 0; index <= static_cast<int>(MV::FocusLink::DOWN_RIGHT); ++index) {
			auto link = static_cast<MV::FocusLink>(index);
			std::string id = a_clickable->focusLink(link);
			std::shared_ptr<MV::Scene::Node> target;
			std::function<void(const std::shared_ptr<MV::Scene::Node>&)> search = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
				if (!target && a_node->id() == id && a_node->component<MV::Scene::Clickable>(false, false)) {
					target = a_node;
				}
				for (auto&& child : *a_node) {
					if (!target) {
						search(child);
					}
				}
			};
			if (!id.empty() && top) {
				search(top);
			}
			addNodeReferenceRow(MV::Scene::Clickable::focusLinkName(link), target, [this, weakClickable, link, id](const std::shared_ptr<MV::Scene::Node>& a_node) {
				std::string newId = a_node ? a_node->id() : std::string();
				auto apply = [weakClickable, link](const std::string& a_id) {
					if (auto locked = weakClickable.lock()) {
						locked->focusLink(link, a_id);
					}
				};
				apply(newId);
				history->record("focus link " + MV::Scene::Clickable::focusLinkName(link), [apply, newId]() { apply(newId); }, [apply, id]() { apply(id); });
				rebuild();
			});
		}
		rowIndent -= theme->px(12.0f);
	}

	void InspectorPanel::addAnchorsGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable) {
		auto& anchors = a_drawable->anchors();
		auto anchoredTo = anchors.hasParent() ? anchors.parent() : nullptr;
		std::string summary = !anchoredTo ? std::string("None") : "to " + nodeName(anchoredTo->owner()) + (anchoredTo->id().empty() ? "" : " : " + anchoredTo->id());
		if (!addGroupHeader("anchors", "anchors#" + a_drawable->id(), summary)) {
			return;
		}
		rowIndent += theme->px(12.0f);
		std::weak_ptr<MV::Scene::Drawable> weakDrawable = a_drawable;

		// the anchor's parent is a Drawable: a dropped node gives its first one
		addNodeReferenceRow("parent", anchoredTo ? anchoredTo->owner() : nullptr, [this, weakDrawable](const std::shared_ptr<MV::Scene::Node>& a_node) {
			auto drawable = weakDrawable.lock();
			if (!drawable) {
				return;
			}
			std::weak_ptr<MV::Scene::Drawable> oldParent = drawable->anchors().hasParent() ? drawable->anchors().parent() : nullptr;
			std::weak_ptr<MV::Scene::Drawable> newParent;
			if (a_node) {
				if (auto found = a_node->component<MV::Scene::Drawable>(false, false); found && found.self() != drawable) {
					newParent = found.self();
				} else {
					return;
				}
			}
			auto apply = [weakDrawable](const std::weak_ptr<MV::Scene::Drawable>& a_parent) {
				if (auto locked = weakDrawable.lock()) {
					if (a_parent.expired()) {
						locked->anchors().removeFromParent();
					} else {
						locked->anchors().parent(a_parent, true);
					}
				}
			};
			apply(newParent);
			history->record("anchor parent", [apply, newParent]() { apply(newParent); }, [apply, oldParent]() { apply(oldParent); });
			rebuild();
		});

		using BoxGetter = MV::BoxAABB<>(*)(MV::Scene::Anchors&);
		using BoxSetter = void(*)(MV::Scene::Anchors&, const MV::BoxAABB<>&);
		auto boxRows = [&](const std::string& a_label, BoxGetter a_get, BoxSetter a_set) {
			for (bool isMaximum : { false, true }) {
				std::string label = a_label + (isMaximum ? " max" : " min");
				auto row = makeRow(label);
				auto fieldHost = row->make("field");
				fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
				auto box = a_get(anchors);
				auto point = isMaximum ? box.maxPoint : box.minPoint;
				auto write = [weakDrawable, a_get, a_set, isMaximum](size_t a_axis, float a_value) {
					if (auto locked = weakDrawable.lock()) {
						auto current = a_get(locked->anchors());
						auto& corner = isMaximum ? current.maxPoint : current.minPoint;
						(a_axis == 0 ? corner.x : corner.y) = a_value;
						a_set(locked->anchors(), current);
					}
				};
				makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), { { "X", point.x }, { "Y", point.y } }, write,
					[this, write, label](size_t a_axis, float a_oldValue, float a_newValue) {
						history->record(label, [write, a_axis, a_newValue]() { write(a_axis, a_newValue); }, [write, a_axis, a_oldValue]() { write(a_axis, a_oldValue); });
					});
			}
		};
		boxRows("anchor", [](MV::Scene::Anchors& a_anchors) { return a_anchors.anchor(); }, [](MV::Scene::Anchors& a_anchors, const MV::BoxAABB<>& a_box) { a_anchors.anchor(a_box); });
		boxRows("offset", [](MV::Scene::Anchors& a_anchors) { return a_anchors.offset(); }, [](MV::Scene::Anchors& a_anchors, const MV::BoxAABB<>& a_box) { a_anchors.offset(a_box); });

		{
			auto row = makeRow("pivot");
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto pivot = anchors.pivot();
			auto write = [weakDrawable](size_t a_axis, float a_value) {
				if (auto locked = weakDrawable.lock()) {
					auto current = locked->anchors().pivot();
					(a_axis == 0 ? current.x : current.y) = a_value;
					locked->anchors().pivot(current);
				}
			};
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), { { "X", pivot.x }, { "Y", pivot.y } }, write,
				[this, write](size_t a_axis, float a_oldValue, float a_newValue) {
					history->record("pivot", [write, a_axis, a_newValue]() { write(a_axis, a_newValue); }, [write, a_axis, a_oldValue]() { write(a_axis, a_oldValue); });
				});
		}
		{
			auto row = makeRow("use position");
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto write = [weakDrawable](bool a_on) {
				if (auto locked = weakDrawable.lock()) {
					locked->anchors().usePosition(a_on);
				}
			};
			makeToggle(fieldHost, *services, *theme, "value", anchors.usePosition(), [this, write](bool a_on) {
				write(a_on);
				history->record("use position", [write, a_on]() { write(a_on); }, [write, a_on]() { write(!a_on); });
			});
		}
		rowIndent -= theme->px(12.0f);
	}

	// One row per texture slot: a thumbnail and name that open the picker, and x to clear the slot.
	void InspectorPanel::addTextureRows(const std::shared_ptr<MV::Scene::Drawable>& a_drawable) {
		auto* property = a_drawable->find_reflected_property("textures");
		auto* textures = property ? property->value_as<std::map<size_t, std::shared_ptr<MV::TextureHandle>>>() : nullptr;
		std::vector<size_t> slots;
		if (textures) {
			for (auto&& [slot, handle] : *textures) {
				slots.push_back(slot);
			}
		}
		if (slots.empty()) {
			slots.push_back(0);
		}
		std::weak_ptr<MV::Scene::Drawable> weakDrawable = a_drawable;
		auto& mouse = *services->get<MV::TapDevice>();
		for (size_t slot : slots) {
			auto handle = textures && textures->count(slot) ? textures->at(slot) : nullptr;
			auto row = makeRow(slots.size() == 1 ? std::string("texture") : "texture " + std::to_string(slot));
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			const float clearWidth = handle ? theme->fieldHeight + theme->px(2.0f) : 0.0f;
			MV::Size<> pickSize(fieldWidth() - clearWidth, theme->fieldHeight);
			auto pick = fieldHost->make("pick");
			attachOutlinedRect(pick, *services, *theme, pickSize, theme->buttonIdle(), theme->buttonOutline(), true);
			MV::Size<> thumbnailSize(theme->fieldHeight - theme->px(4.0f), theme->fieldHeight - theme->px(4.0f));
			if (handle && handle->texture()) {
				auto fitted = MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), thumbnailSize);
				auto image = pick->make("image");
				image->position(MV::point(theme->px(4.0f), theme->px(2.0f)) + MV::toPoint((thumbnailSize - fitted) / 2.0f));
				image->attach<MV::Scene::Sprite>()->bounds(fitted)->texture(handle);
			}
			auto nameHost = pick->make("name");
			nameHost->position(MV::point(thumbnailSize.width + theme->px(10.0f), 0.0f));
			makeLabel(nameHost, *services, *theme, "value", MV::size(pickSize.width - thumbnailSize.width - theme->px(12.0f), theme->fieldHeight), textureName(handle));
			pick->attach<MV::Scene::Clickable>(mouse)->bounds(pickSize)->onAccept.connect("pick", [this, weakDrawable, slot](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
				openTexturePicker(weakDrawable, slot, a_clickable->mouse().position());
			});
			if (handle) {
				auto clear = makeIconButton(fieldHost, *services, *theme, "clear", MV::size(theme->fieldHeight, theme->fieldHeight), Icon::Close, [this, weakDrawable, slot, handle]() {
					auto apply = [weakDrawable, slot](const std::shared_ptr<MV::TextureHandle>& a_handle) {
						if (auto locked = weakDrawable.lock()) {
							if (a_handle) {
								locked->texture(a_handle, slot);
							} else {
								locked->clearTexture(slot);
							}
						}
					};
					apply(nullptr);
					history->record("clear texture", [apply]() { apply(nullptr); }, [apply, handle]() { apply(handle); });
					rebuild();
				});
				clear->owner()->position(MV::point(pickSize.width + theme->px(2.0f), 0.0f));
			}
		}
	}

	void InspectorPanel::openTexturePicker(const std::weak_ptr<MV::Scene::Drawable>& a_drawable, size_t a_slot, const MV::Point<int>& a_screenPoint) {
		closeColorPopup(true);
		texturePicker.drawable = a_drawable;
		texturePicker.slot = a_slot;
		texturePicker.page = 0;
		texturePicker.position = contentNode->renderer().worldFromScreen(a_screenPoint);
		texturePicker.open = true;
		buildTexturePicker();
	}

	void InspectorPanel::closeTexturePicker() {
		texturePicker.open = false;
		if (popupNode) {
			if (auto popup = popupNode->get("texturePopup", false)) {
				popup->removeFromParent();
			}
		}
	}

	void InspectorPanel::buildTexturePicker() {
		if (!popupNode) {
			return;
		}
		if (auto existing = popupNode->get("texturePopup", false)) {
			existing->removeFromParent();
		}
		auto sources = textureSources(*services->get<MV::SharedTextures>());
		texturePicker.source = std::min(texturePicker.source, sources.size() - 1);
		auto& source = sources[texturePicker.source];
		const size_t perPage = pickerColumns * pickerRows;
		const size_t pageCount = std::max<size_t>(1, (source.entries.size() + perPage - 1) / perPage);
		texturePicker.page = std::min(texturePicker.page, pageCount - 1);

		const float tile = theme->px(56.0f);
		const float gap = theme->px(4.0f);
		const float inset = theme->px(8.0f);
		const float headerHeight = theme->fieldHeight;
		MV::Size<> popupSize(inset * 2.0f + pickerColumns * tile + (pickerColumns - 1) * gap, inset * 3.0f + headerHeight + pickerRows * tile + (pickerRows - 1) * gap);

		auto popup = popupNode->make("texturePopup");
		auto worldSize = contentNode->renderer().world().size();
		popup->position(MV::point(
			std::clamp(texturePicker.position.x - popupSize.width - theme->pad, 0.0f, std::max(0.0f, worldSize.width - popupSize.width)),
			std::clamp(texturePicker.position.y, 0.0f, std::max(0.0f, worldSize.height - popupSize.height))));
		auto shadow = popup->make("shadow");
		shadow->position(MV::point(theme->px(2.0f), theme->px(3.0f)));
		attachRoundedRect(shadow, *services, *theme, popupSize, MV::Color(0.0f, 0.0f, 0.0f, 0.45f));
		attachOutlinedRect(popup->make("body"), *services, *theme, popupSize, MV::Color(0x1b1b1b), theme->fieldOutline());
		auto& mouse = *services->get<MV::TapDevice>();
		// the body takes presses that miss a tile, so they do not reach the panel underneath
		popup->make("blocker")->attach<MV::Scene::Clickable>(mouse)->bounds(popupSize)->globalPriority(pickerPriority);

		int64_t priority = pickerPriority + 1;
		auto raise = [&priority](const std::shared_ptr<MV::Scene::Button>& a_button) {
			a_button->globalPriority(priority++);
			return a_button;
		};
		auto header = popup->make("header");
		header->position(MV::point(inset, inset));
		const float stepWidth = theme->fieldHeight * 1.4f;
		const float sourceWidth = popupSize.width - inset * 2.0f - stepWidth * 2.0f - theme->px(90.0f) - gap * 3.0f;
		raise(makeButton(header, *services, *theme, "source", MV::size(sourceWidth, headerHeight), source.title, [this, sourceCount = sources.size()]() {
			texturePicker.source = (texturePicker.source + 1) % sourceCount;
			texturePicker.page = 0;
			buildTexturePicker();
		}));
		float x = sourceWidth + gap;
		raise(makeIconButton(header, *services, *theme, "previous", MV::size(stepWidth, headerHeight), Icon::StepBack, [this]() {
			if (texturePicker.page > 0) {
				--texturePicker.page;
				buildTexturePicker();
			}
		}, "Previous page"))->owner()->position(MV::point(x, 0.0f));
		x += stepWidth + gap;
		auto pageLabel = makeLabel(header, *services, *theme, "page", MV::size(theme->px(90.0f), headerHeight), std::to_string(texturePicker.page + 1) + " / " + std::to_string(pageCount));
		pageLabel->justification(MV::TextJustification::CENTER);
		pageLabel->owner()->position(MV::point(x, 0.0f));
		x += theme->px(90.0f) + gap;
		raise(makeIconButton(header, *services, *theme, "next", MV::size(stepWidth, headerHeight), Icon::StepForward, [this, pageCount]() {
			if (texturePicker.page + 1 < pageCount) {
				++texturePicker.page;
				buildTexturePicker();
			}
		}, "Next page"))->owner()->position(MV::point(x, 0.0f));

		auto grid = popup->make("tiles");
		grid->position(MV::point(inset, inset * 2.0f + headerHeight));
		const size_t first = texturePicker.page * perPage;
		for (size_t index = first; index < std::min(source.entries.size(), first + perPage); ++index) {
			auto& entry = source.entries[index];
			auto cell = grid->make(MV::guid("tile_"));
			const size_t slotIndex = index - first;
			cell->position(MV::point((slotIndex % pickerColumns) * (tile + gap), (slotIndex / pickerColumns) * (tile + gap)));
			attachOutlinedRect(cell, *services, *theme, MV::size(tile, tile), theme->fieldBg(), theme->fieldOutline());
			std::shared_ptr<MV::TextureHandle> handle;
			try {
				handle = entry.second();
			} catch (...) {
			}
			if (handle) {
				auto inner = outlinedFillBounds(*theme, MV::size(tile, tile));
				auto fitted = MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), inner.size());
				auto image = cell->make("image");
				image->position(inner.minPoint + MV::toPoint((inner.size() - fitted) / 2.0f));
				image->attach<MV::Scene::Sprite>()->bounds(fitted)->texture(handle);
			}
			cell->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(tile, tile))->globalPriority(priority++)->onAccept.connect("choose", [this, handle](const std::shared_ptr<MV::Scene::Clickable>&) {
				auto weakDrawable = texturePicker.drawable;
				auto slot = texturePicker.slot;
				auto drawable = weakDrawable.lock();
				if (!drawable || !handle) {
					return;
				}
				auto previous = drawable->texture(slot);
				auto apply = [weakDrawable, slot](const std::shared_ptr<MV::TextureHandle>& a_handle) {
					if (auto locked = weakDrawable.lock()) {
						if (a_handle) {
							locked->texture(a_handle, slot);
						} else {
							locked->clearTexture(slot);
						}
					}
				};
				apply(handle);
				history->record("texture", [apply, handle]() { apply(handle); }, [apply, previous]() { apply(previous); });
				closeTexturePicker();
				rebuild();
			});
		}
	}

	void InspectorPanel::addPointsGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable) {
		auto points = a_drawable->getPoints();
		const std::string key = "points#" + a_drawable->id();
		if (!addGroupHeader("points", key, countText(points.size(), "point", "points"))) {
			return;
		}
		rowIndent += theme->px(12.0f);
		std::weak_ptr<MV::Scene::Drawable> weakDrawable = a_drawable;
		const size_t first = addPager(key, points.size(), rowsPerListPage);
		for (size_t index = first; index < std::min(points.size(), first + rowsPerListPage); ++index) {
			std::string label = "[" + std::to_string(index) + "]";
			auto row = makeRow(label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto write = [weakDrawable, index](size_t a_axis, float a_value) {
				if (auto locked = weakDrawable.lock(); locked && index < locked->pointSize()) {
					auto point = locked->getPoints()[index];
					(a_axis == 0 ? point.x : a_axis == 1 ? point.y : point.z) = a_value;
					locked->setPoint(index, point);
				}
			};
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), { { "X", points[index].x }, { "Y", points[index].y }, { "Z", points[index].z } }, write,
				[this, write, label](size_t a_axis, float a_oldValue, float a_newValue) {
					history->record("point " + label, [write, a_axis, a_newValue]() { write(a_axis, a_newValue); }, [write, a_axis, a_oldValue]() { write(a_axis, a_oldValue); });
				});
		}
		rowIndent -= theme->px(12.0f);
	}

	// Indices show a triangle to a row.
	void InspectorPanel::addIndicesGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable) {
		std::vector<GLuint> indices = a_drawable->pointIndices();
		const std::string key = "indices#" + a_drawable->id();
		if (!addGroupHeader("vertexIndices", key, countText(indices.size(), "index", "indices"))) {
			return;
		}
		rowIndent += theme->px(12.0f);
		std::weak_ptr<MV::Scene::Drawable> weakDrawable = a_drawable;
		const size_t triangleCount = (indices.size() + 2) / 3;
		const size_t first = addPager(key, triangleCount, rowsPerListPage);
		for (size_t triangle = first; triangle < std::min(triangleCount, first + rowsPerListPage); ++triangle) {
			std::string label = "[" + std::to_string(triangle) + "]";
			auto row = makeRow(label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			std::vector<std::pair<std::string, float>> corners;
			for (size_t corner = 0; corner < 3 && triangle * 3 + corner < indices.size(); ++corner) {
				corners.emplace_back(std::string(1, static_cast<char>('A' + corner)), static_cast<float>(indices[triangle * 3 + corner]));
			}
			auto write = [weakDrawable, triangle](size_t a_corner, float a_value) {
				if (auto locked = weakDrawable.lock()) {
					std::vector<GLuint> current = locked->pointIndices();
					const size_t at = triangle * 3 + a_corner;
					if (at < current.size() && a_value >= 0.0f && static_cast<size_t>(a_value) < locked->pointSize()) {
						current[at] = static_cast<GLuint>(a_value);
						locked->setPoints(locked->getPoints(), current);
					}
				}
			};
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), corners, write,
				[this, write, label](size_t a_corner, float a_oldValue, float a_newValue) {
					history->record("triangle " + label, [write, a_corner, a_newValue]() { write(a_corner, a_newValue); }, [write, a_corner, a_oldValue]() { write(a_corner, a_oldValue); });
				});
		}
		rowIndent -= theme->px(12.0f);
	}

	bool InspectorPanel::addCustomEditor(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_path, jai::property_base* a_property) {
		const auto typeId = a_property->value_type_id();
		const std::string label = leafName(a_path);
		std::weak_ptr<MV::Scene::Component> weakComponent = a_component;
		auto drawable = std::dynamic_pointer_cast<MV::Scene::Drawable>(a_component);
		const bool topLevel = label == a_path;

		if (topLevel && label == "componentOwner") {
			addNodeReferenceRow("owner", a_component->owner(), nullptr);
			return true;
		}
		if (drawable && typeId == std::type_index(typeid(MV::Scene::Anchors))) {
			addAnchorsGroup(drawable);
			return true;
		}
		if (auto clickable = std::dynamic_pointer_cast<MV::Scene::Clickable>(a_component); clickable && topLevel && label == "focusLinks") {
			addFocusLinksGroup(clickable);
			return true;
		}
		if (drawable && topLevel && typeId == std::type_index(typeid(std::map<size_t, std::shared_ptr<MV::TextureHandle>>))) {
			addTextureRows(drawable);
			return true;
		}
		if (drawable && topLevel && typeId == std::type_index(typeid(std::vector<MV::DrawPoint>))) {
			addPointsGroup(drawable);
			return true;
		}
		if (drawable && topLevel && typeId == std::type_index(typeid(std::vector<GLuint>))) {
			addIndicesGroup(drawable);
			return true;
		}
		if (auto sprite = std::dynamic_pointer_cast<MV::Scene::Sprite>(a_component); sprite && topLevel && label == "subdivisions") {
			auto row = makeRow(label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			std::weak_ptr<MV::Scene::Sprite> weakSprite = sprite;
			// subdivide() rebuilds the grid of points; the raw value alone would not
			auto write = [weakSprite](float a_value) {
				if (auto locked = weakSprite.lock()) {
					locked->subdivide(static_cast<uint16_t>(std::clamp(a_value, 0.0f, 64.0f)));
				}
			};
			makeScrubField(fieldHost, *services, *theme, *focus, "value", MV::size(fieldWidth(), theme->fieldHeight), static_cast<float>(sprite->subdivisions()), write,
				[this, write](float a_oldValue, float a_newValue) {
					history->record("subdivisions", [write, a_newValue]() { write(a_newValue); }, [write, a_oldValue]() { write(a_oldValue); });
				});
			return true;
		}
		if (drawable && topLevel && label == "drawType") {
			GLenum current = *a_property->value_as<GLenum>();
			if (current > 6) {
				addReadOnlyRow(label, drawTypeLabel(current));
				return true;
			}
			auto row = makeRow(label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			std::vector<std::string> labels;
			for (GLenum drawType = 0; drawType <= 6; ++drawType) {
				labels.push_back(drawTypeLabel(drawType));
			}
			auto apply = propertyApplier<GLenum>(weakComponent, a_path);
			makeCycleButton(fieldHost, *services, *theme, "value", MV::size(fieldWidth(), theme->fieldHeight), std::move(labels), static_cast<int>(current), [this, weakComponent, a_path, apply, previous = std::make_shared<GLenum>(current)](int a_index) {
				GLenum chosen = static_cast<GLenum>(a_index);
				apply(chosen);
				recordPropertyEdit<GLenum>(weakComponent, a_path, *previous, chosen);
				*previous = chosen;
			});
			return true;
		}
		if (typeId == std::type_index(typeid(std::shared_ptr<MV::Scene::Node>))) {
			auto apply = propertyApplier<std::shared_ptr<MV::Scene::Node>>(weakComponent, a_path);
			auto current = *a_property->value_as<std::shared_ptr<MV::Scene::Node>>();
			addNodeReferenceRow(label, current, [this, weakComponent, a_path, apply, current](const std::shared_ptr<MV::Scene::Node>& a_node) {
				apply(a_node);
				recordPropertyEdit<std::shared_ptr<MV::Scene::Node>>(weakComponent, a_path, current, a_node);
				rebuild();
			});
			return true;
		}
		if (typeId == std::type_index(typeid(std::weak_ptr<MV::Scene::Node>))) {
			auto apply = propertyApplier<std::weak_ptr<MV::Scene::Node>>(weakComponent, a_path);
			auto current = *a_property->value_as<std::weak_ptr<MV::Scene::Node>>();
			addNodeReferenceRow(label, current.lock(), [this, weakComponent, a_path, apply, current](const std::shared_ptr<MV::Scene::Node>& a_node) {
				apply(a_node);
				recordPropertyEdit<std::weak_ptr<MV::Scene::Node>>(weakComponent, a_path, current, std::weak_ptr<MV::Scene::Node>(a_node));
				rebuild();
			});
			return true;
		}
		if (typeId == std::type_index(typeid(MV::Size<>))) {
			auto row = makeRow(label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			const MV::Size<> current = *a_property->value_as<MV::Size<>>();
			auto axisSet = [](MV::Size<> a_size, size_t a_axis, float a_value) {
				(a_axis == 0 ? a_size.width : a_size.height) = a_value;
				return a_size;
			};
			auto apply = propertyApplier<MV::Size<>>(weakComponent, a_path);
			auto read = [weakComponent, a_path]() {
				auto locked = weakComponent.lock();
				auto* property = locked ? resolveProperty(locked, a_path) : nullptr;
				return property ? *property->value_as<MV::Size<>>() : MV::Size<>();
			};
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), { { "W", current.width }, { "H", current.height } },
				[apply, read, axisSet](size_t a_axis, float a_value) { apply(axisSet(read(), a_axis, a_value)); },
				[this, weakComponent, a_path, read, axisSet](size_t a_axis, float a_oldValue, float a_newValue) {
					recordPropertyEdit<MV::Size<>>(weakComponent, a_path, axisSet(read(), a_axis, a_oldValue), axisSet(read(), a_axis, a_newValue));
				});
			return true;
		}
		if (auto nested = nestedOwners().find(typeId); nested != nestedOwners().end()) {
			std::vector<std::pair<std::string, jai::property_base*>> children;
			nested->second.visit(a_property, [&](const std::string& a_name, jai::property_base* a_child) {
				children.emplace_back(a_name, a_child);
			});
			if (addGroupHeader(label, "group#" + a_component->id() + "/" + a_path, countText(children.size(), "property", "properties"))) {
				rowIndent += theme->px(12.0f);
				for (auto&& [name, child] : children) {
					addPropertyRow(a_component, a_path + "/" + name, child);
				}
				rowIndent -= theme->px(12.0f);
			}
			return true;
		}
		return false;
	}

}
