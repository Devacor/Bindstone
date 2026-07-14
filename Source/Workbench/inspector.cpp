#include "inspector.h"
#include "gizmos.h"
#include <sstream>
#include <typeindex>

namespace Workbench {

	namespace {
		std::string friendlyTypeName(std::string a_name) {
			for (auto&& prefix : { "class ", "struct ", "enum " }) {
				if (a_name.rfind(prefix, 0) == 0) {
					a_name = a_name.substr(std::string(prefix).size());
				}
			}
			auto lastColon = a_name.rfind("::");
			if (lastColon != std::string::npos) {
				a_name = a_name.substr(lastColon + 2);
			}
			return a_name;
		}

		std::vector<float> parseFloats(const std::string& a_text) {
			std::vector<float> values;
			std::stringstream stream(a_text);
			float value = 0.0f;
			while (stream >> value) {
				values.push_back(value);
			}
			return values;
		}

		std::string formatFloats(std::initializer_list<float> a_values) {
			std::ostringstream out;
			bool first = true;
			for (auto value : a_values) {
				if (!first) { out << " "; }
				out << value;
				first = false;
			}
			return out.str();
		}
	}

	void InspectorPanel::build() {
		rebuild();
	}

	void InspectorPanel::inspect(const std::shared_ptr<MV::Scene::Node>& a_node) {
		inspectedNode = a_node;
		rebuild();
	}

	void InspectorPanel::rebuild() {
		if (!contentNode) {
			return;
		}
		focus->cancel();
		contentNode->clear();
		float y = 0.0f;
		auto node = inspectedNode.lock();
		if (!node) {
			makeLabel(contentNode, *services, *theme, "empty", MV::size(200.0f, theme->rowHeight), "Nothing selected");
			builtHeight = theme->rowHeight;
			return;
		}
		if (addSectionHeader("Node: " + (node->id().empty() ? std::string("(unnamed)") : node->id()), "node", y)) {
			buildNodeSection(y);
		}
		if (auto components = node->property_value<std::vector<std::shared_ptr<MV::Scene::Component>>>("childComponents")) {
			for (auto&& component : *components) {
				buildComponentSection(component, y);
			}
		}
		builtHeight = y;
	}

	// Returns true when the section is expanded; draws the Unity-style foldout header.
	bool InspectorPanel::addSectionHeader(const std::string& a_title, const std::string& a_key, float& a_y, const std::shared_ptr<MV::Scene::Component>& a_gizmoComponent) {
		bool expanded = collapsedSections.count(a_key) ? !collapsedSections[a_key] : true;
		auto row = contentNode->make(MV::guid("header_"));
		row->position(MV::point(0.0f, a_y));
		row->attach<MV::Scene::Sprite>()->bounds(MV::size(panelWidth, theme->rowHeight))->color(theme->header());
		auto triangle = makeLabel(row, *services, *theme, "fold", MV::size(14.0f, theme->rowHeight), expanded ? "v" : ">");
		triangle->owner()->position(MV::point(theme->pad, 0.0f));
		auto title = makeLabel(row, *services, *theme, "text", MV::size(panelWidth - 24.0f, theme->rowHeight), a_title);
		title->owner()->position(MV::point(20.0f, 0.0f));
		auto& mouse = *services->get<MV::TapDevice>();
		row->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(panelWidth, theme->rowHeight))->onAccept.connect("toggle", [this, a_key](const std::shared_ptr<MV::Scene::Clickable>&) {
			bool nowCollapsed = collapsedSections.count(a_key) ? !collapsedSections[a_key] : true;
			collapsedSections[a_key] = nowCollapsed;
			rebuild();
		});
		if (a_gizmoComponent && onEditComponent && GizmoLayer::supports(a_gizmoComponent)) {
			bool active = componentGizmoActive && componentGizmoActive(a_gizmoComponent);
			float buttonWidth = theme->rowHeight * 1.5f;
			auto gizmoButton = makeButton(row, *services, *theme, "gizmo", MV::size(buttonWidth, theme->rowHeight - 4.0f), active ? "[G]" : "G", [this, a_gizmoComponent]() {
				onEditComponent(a_gizmoComponent);
			});
			gizmoButton->owner()->position(MV::point(panelWidth - buttonWidth - theme->pad, 2.0f));
		}
		a_y += theme->rowHeight + 2.0f;
		return expanded;
	}

	std::shared_ptr<MV::Scene::Node> InspectorPanel::makeRow(const std::string& a_label, float& a_y) {
		auto row = contentNode->make(MV::guid("row_"));
		row->position(MV::point(0.0f, a_y));
		makeLabel(row, *services, *theme, "label", MV::size(labelWidth(), theme->fieldHeight), a_label);
		a_y += theme->fieldHeight + 3.0f;
		return row;
	}

	void InspectorPanel::buildNodeSection(float& a_y) {
		auto node = inspectedNode.lock();
		std::weak_ptr<MV::Scene::Node> weakNode = node;
		MV::Size<> fieldSize(fieldWidth(), theme->fieldHeight);

		auto fieldAt = [&](const std::string& a_label, const std::string& a_value, std::function<void(const std::string&)> a_commit) {
			auto row = makeRow(a_label, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			makeTextField(fieldHost, *services, *theme, *focus, "value", fieldSize, a_value, std::move(a_commit));
		};

		fieldAt("id", node->id(), [this, weakNode](const std::string& a_text) {
			if (auto locked = weakNode.lock()) {
				std::string oldId = locked->id();
				if (oldId == a_text) {
					return;
				}
				locked->id(a_text);
				history->record("id",
					[weakNode, a_text]() { if (auto node = weakNode.lock()) { node->id(a_text); } },
					[weakNode, oldId]() { if (auto node = weakNode.lock()) { node->id(oldId); } });
			}
		});

		auto vectorAt = [&](const std::string& a_label, std::initializer_list<std::pair<std::string, float>> a_axes, std::function<void(size_t, float)> a_commit) {
			auto row = makeRow(a_label, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), a_axes, a_commit,
				[this, a_label, a_commit](size_t a_axis, float a_oldValue, float a_newValue) {
					history->record(a_label,
						[a_commit, a_axis, a_newValue]() { a_commit(a_axis, a_newValue); },
						[a_commit, a_axis, a_oldValue]() { a_commit(a_axis, a_oldValue); });
				});
		};

		auto position = node->position();
		vectorAt("position", { { "X", position.x }, { "Y", position.y }, { "Z", position.z } }, [weakNode](size_t a_axis, float a_value) {
			if (auto locked = weakNode.lock()) {
				auto current = locked->position();
				(a_axis == 0 ? current.x : a_axis == 1 ? current.y : current.z) = a_value;
				locked->position(current);
			}
		});
		auto rotation = node->rotation();
		vectorAt("rotation", { { "X", rotation.x }, { "Y", rotation.y }, { "Z", rotation.z } }, [weakNode](size_t a_axis, float a_value) {
			if (auto locked = weakNode.lock()) {
				auto current = locked->rotation();
				(a_axis == 0 ? current.x : a_axis == 1 ? current.y : current.z) = a_value;
				locked->rotation(current);
			}
		});
		auto scale = node->scale();
		vectorAt("scale", { { "X", scale.x }, { "Y", scale.y }, { "Z", scale.z } }, [weakNode](size_t a_axis, float a_value) {
			if (auto locked = weakNode.lock()) {
				auto current = locked->scale();
				(a_axis == 0 ? current.x : a_axis == 1 ? current.y : current.z) = a_value;
				locked->scale(current);
			}
		});

		auto scrubAt = [&](const std::string& a_label, float a_value, std::function<void(float)> a_commit) {
			auto row = makeRow(a_label, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			makeScrubField(fieldHost, *services, *theme, *focus, "value", MV::size(fieldWidth(), theme->fieldHeight), a_value, a_commit,
				[this, a_label, a_commit](float a_oldValue, float a_newValue) {
					history->record(a_label,
						[a_commit, a_newValue]() { a_commit(a_newValue); },
						[a_commit, a_oldValue]() { a_commit(a_oldValue); });
				});
		};
		scrubAt("alpha", node->alpha(), [weakNode](float a_value) {
			if (auto locked = weakNode.lock()) { locked->alpha(a_value); }
		});
		scrubAt("depth", node->depth(), [weakNode](float a_value) {
			if (auto locked = weakNode.lock()) { locked->depth(a_value); }
		});
	}

	void InspectorPanel::buildComponentSection(const std::shared_ptr<MV::Scene::Component>& a_component, float& a_y) {
		std::string typeName = friendlyTypeName(typeid(*a_component).name());
		std::string sectionKey = typeName + (a_component->id().empty() ? "" : "#" + a_component->id());
		if (!addSectionHeader(typeName, sectionKey, a_y, a_component)) {
			return;
		}
		std::vector<std::pair<std::string, jai::property_base*>> properties;
		a_component->visit_reflected_properties([&](const std::string& a_name, jai::property_base* a_property) {
			properties.emplace_back(a_name, a_property);
		});
		for (auto&& [name, property] : properties) {
			addPropertyRow(a_component, name, property, a_y);
		}
	}

	void InspectorPanel::notifyEdited(const std::shared_ptr<MV::Scene::Component>& a_component) {
		if (auto owner = a_component->owner()) {
			owner->postLoadStep();
		}
	}

	void InspectorPanel::openColorPopup(const std::weak_ptr<MV::Scene::Component>& a_component, const std::string& a_propertyName, jai::property_base* a_property, const std::weak_ptr<MV::Scene::Sprite>& a_swatch, const MV::Point<int>& a_screenPoint) {
		if (!popupNode) {
			return;
		}
		popupNode->clear();
		auto popup = popupNode->make("colorPopup");
		float paletteSize = 220.0f * theme->scaleFactor();
		auto world = contentNode->renderer().worldFromScreen(a_screenPoint);
		auto worldSize = contentNode->renderer().world().size();
		popup->position(MV::point(
			std::clamp(world.x - paletteSize - theme->pad, 0.0f, std::max(0.0f, worldSize.width - paletteSize)),
			std::clamp(world.y, 0.0f, std::max(0.0f, worldSize.height - paletteSize))));

		auto& mouse = *services->get<MV::TapDevice>();
		auto palette = popup->attach<MV::Scene::Palette>(mouse)->bounds(MV::size(paletteSize, paletteSize));
		MV::Color colorAtOpen;
		if (auto current = a_property->value_as<MV::Color>()) {
			colorAtOpen = *current;
			palette->color(*current);
		}
		palette->onColorChange.connect("change", [this, a_component, a_property, a_swatch](const std::shared_ptr<MV::Scene::Palette>& a_palette) {
			if (auto locked = a_component.lock()) {
				auto color = a_palette->color();
				if (a_property->assign_value(color)) {
					notifyEdited(locked);
					if (auto swatch = a_swatch.lock()) {
						swatch->color(color);
					}
				}
			}
		});
		// One command for the whole picker session, recorded when the popup closes.
		palette->onSwatchClicked.connect("close", [this, a_component, a_propertyName, a_property, colorAtOpen](const std::shared_ptr<MV::Scene::Palette>& a_palette) {
			if (auto locked = a_component.lock()) {
				auto finalColor = *a_property->value_as<MV::Color>();
				bool changed = finalColor.R != colorAtOpen.R || finalColor.G != colorAtOpen.G || finalColor.B != colorAtOpen.B || finalColor.A != colorAtOpen.A;
				if (changed) {
					recordPropertyEdit<MV::Color>(a_component, a_propertyName, colorAtOpen, finalColor);
				}
			}
			a_palette->owner()->removeFromParent();
		});
	}

	void InspectorPanel::addPropertyRow(const std::shared_ptr<MV::Scene::Component>& a_component, const std::string& a_name, jai::property_base* a_property, float& a_y) {
		auto typeId = a_property->value_type_id();
		MV::Size<> fieldSize(fieldWidth(), theme->fieldHeight);
		std::weak_ptr<MV::Scene::Component> weakComponent = a_component;

		auto commitAndNotify = [this, weakComponent](auto a_write) {
			return [this, weakComponent, a_write](const std::string& a_text) {
				if (auto locked = weakComponent.lock()) {
					if (a_write(a_text)) {
						notifyEdited(locked);
					}
				}
			};
		};

		auto numericField = [&](auto a_sample) {
			using ValueType = decltype(a_sample);
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			ValueType current = *a_property->value_as<ValueType>();
			auto apply = propertyApplier<ValueType>(weakComponent, a_name);
			makeScrubField(fieldHost, *services, *theme, *focus, "value", fieldSize, static_cast<float>(current),
				[apply](float a_value) {
					apply(static_cast<ValueType>(a_value));
				},
				[this, weakComponent, propertyName = a_name](float a_oldValue, float a_newValue) {
					recordPropertyEdit<ValueType>(weakComponent, propertyName, static_cast<ValueType>(a_oldValue), static_cast<ValueType>(a_newValue));
				});
		};

		auto enumField = [&](auto a_sample, std::vector<std::string> a_labels) {
			using EnumType = decltype(a_sample);
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			int current = static_cast<int>(*a_property->value_as<EnumType>());
			auto apply = propertyApplier<EnumType>(weakComponent, a_name);
			makeCycleButton(fieldHost, *services, *theme, "value", fieldSize, std::move(a_labels), current, [this, weakComponent, a_property, apply, propertyName = a_name](int a_index) {
				EnumType oldValue = *a_property->value_as<EnumType>();
				EnumType newValue = static_cast<EnumType>(a_index);
				apply(newValue);
				recordPropertyEdit<EnumType>(weakComponent, propertyName, oldValue, newValue);
			});
		};

		if (typeId == std::type_index(typeid(bool))) {
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto apply = propertyApplier<bool>(weakComponent, a_name);
			makeToggle(fieldHost, *services, *theme, "value", *a_property->value_as<bool>(), [this, weakComponent, apply, propertyName = a_name](bool a_on) {
				apply(a_on);
				recordPropertyEdit<bool>(weakComponent, propertyName, !a_on, a_on);
			});
		} else if (typeId == std::type_index(typeid(float))) {
			numericField(float{});
		} else if (typeId == std::type_index(typeid(double))) {
			numericField(double{});
		} else if (typeId == std::type_index(typeid(int32_t))) {
			numericField(int32_t{});
		} else if (typeId == std::type_index(typeid(int64_t))) {
			numericField(int64_t{});
		} else if (typeId == std::type_index(typeid(uint32_t))) {
			numericField(uint32_t{});
		} else if (typeId == std::type_index(typeid(std::string))) {
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto apply = propertyApplier<std::string>(weakComponent, a_name);
			makeTextField(fieldHost, *services, *theme, *focus, "value", fieldSize, *a_property->value_as<std::string>(),
				[this, weakComponent, a_property, apply, propertyName = a_name](const std::string& a_text) {
					if (auto locked = weakComponent.lock()) {
						std::string oldValue = *a_property->value_as<std::string>();
						if (oldValue == a_text) {
							return;
						}
						apply(a_text);
						recordPropertyEdit<std::string>(weakComponent, propertyName, oldValue, a_text);
					}
				});
		} else if (typeId == std::type_index(typeid(MV::Color))) {
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			const MV::Color current = *a_property->value_as<MV::Color>();

			float swatchSize = theme->fieldHeight;
			auto swatchNode = fieldHost->make("swatch");
			auto swatchSprite = attachRoundedRect(swatchNode, *services, *theme, MV::size(swatchSize, swatchSize), current);
			std::weak_ptr<MV::Scene::Sprite> weakSwatch = swatchSprite;
			auto& mouse = *services->get<MV::TapDevice>();
			swatchNode->attach<MV::Scene::Clickable>(mouse)->bounds(MV::size(swatchSize, swatchSize))->onAccept.connect("pick", [this, weakComponent, a_property, weakSwatch, propertyName = a_name](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
				openColorPopup(weakComponent, propertyName, a_property, weakSwatch, a_clickable->owner()->screenPosition());
			});

			auto vectorHost = fieldHost->make("channels");
			vectorHost->position(MV::point(swatchSize + theme->pad, 0.0f));
			auto channelSet = [](MV::Color a_color, size_t a_axis, float a_value) {
				(a_axis == 0 ? a_color.R : a_axis == 1 ? a_color.G : a_axis == 2 ? a_color.B : a_color.A) = a_value;
				return a_color;
			};
			makeVectorRow(vectorHost, *services, *theme, *focus, fieldWidth() - swatchSize - theme->pad, { { "R", current.R }, { "G", current.G }, { "B", current.B }, { "A", current.A } },
				[this, weakComponent, a_property, weakSwatch, channelSet](size_t a_axis, float a_value) {
					if (auto locked = weakComponent.lock()) {
						auto color = channelSet(*a_property->value_as<MV::Color>(), a_axis, a_value);
						if (a_property->assign_value(color)) {
							notifyEdited(locked);
							if (auto swatch = weakSwatch.lock()) {
								swatch->color(color);
							}
						}
					}
				},
				[this, weakComponent, a_property, channelSet, propertyName = a_name](size_t a_axis, float a_oldValue, float a_newValue) {
					if (auto locked = weakComponent.lock()) {
						auto currentColor = *a_property->value_as<MV::Color>();
						recordPropertyEdit<MV::Color>(weakComponent, propertyName, channelSet(currentColor, a_axis, a_oldValue), channelSet(currentColor, a_axis, a_newValue));
					}
				});
		} else if (typeId == std::type_index(typeid(MV::Point<>))) {
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			const MV::Point<> current = *a_property->value_as<MV::Point<>>();
			auto axisSet = [](MV::Point<> a_point, size_t a_axis, float a_value) {
				(a_axis == 0 ? a_point.x : a_axis == 1 ? a_point.y : a_point.z) = a_value;
				return a_point;
			};
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), { { "X", current.x }, { "Y", current.y }, { "Z", current.z } },
				[this, weakComponent, a_property, axisSet](size_t a_axis, float a_value) {
					if (auto locked = weakComponent.lock()) {
						if (a_property->assign_value(axisSet(*a_property->value_as<MV::Point<>>(), a_axis, a_value))) {
							notifyEdited(locked);
						}
					}
				},
				[this, weakComponent, a_property, axisSet, propertyName = a_name](size_t a_axis, float a_oldValue, float a_newValue) {
					if (auto locked = weakComponent.lock()) {
						auto currentPoint = *a_property->value_as<MV::Point<>>();
						recordPropertyEdit<MV::Point<>>(weakComponent, propertyName, axisSet(currentPoint, a_axis, a_oldValue), axisSet(currentPoint, a_axis, a_newValue));
					}
				});
		} else if (typeId == std::type_index(typeid(MV::Scale))) {
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			const MV::Scale current = *a_property->value_as<MV::Scale>();
			auto axisSet = [](MV::Scale a_scale, size_t a_axis, float a_value) {
				(a_axis == 0 ? a_scale.x : a_axis == 1 ? a_scale.y : a_scale.z) = a_value;
				return a_scale;
			};
			makeVectorRow(fieldHost, *services, *theme, *focus, fieldWidth(), { { "X", current.x }, { "Y", current.y }, { "Z", current.z } },
				[this, weakComponent, a_property, axisSet](size_t a_axis, float a_value) {
					if (auto locked = weakComponent.lock()) {
						if (a_property->assign_value(axisSet(*a_property->value_as<MV::Scale>(), a_axis, a_value))) {
							notifyEdited(locked);
						}
					}
				},
				[this, weakComponent, a_property, axisSet, propertyName = a_name](size_t a_axis, float a_oldValue, float a_newValue) {
					if (auto locked = weakComponent.lock()) {
						auto currentScale = *a_property->value_as<MV::Scale>();
						recordPropertyEdit<MV::Scale>(weakComponent, propertyName, axisSet(currentScale, a_axis, a_oldValue), axisSet(currentScale, a_axis, a_newValue));
					}
				});
		} else if (typeId == std::type_index(typeid(MV::BlendMode))) {
			enumField(MV::BlendMode{}, { "Default", "Multiply", "Add", "Screen" });
		} else if (typeId == std::type_index(typeid(MV::TextWrapMethod))) {
			enumField(MV::TextWrapMethod{}, { "None", "Scale", "Hard", "Soft" });
		} else if (typeId == std::type_index(typeid(MV::TextJustification))) {
			enumField(MV::TextJustification{}, { "Left", "Center", "Right" });
		} else if (typeId == std::type_index(typeid(MV::Scene::Clickable::BoundsType))) {
			enumField(MV::Scene::Clickable::BoundsType{}, { "Local", "Node", "Children", "NodeChildren", "None" });
		} else {
			auto row = makeRow(a_name, a_y);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			makeLabel(fieldHost, *services, *theme, "value", fieldSize, "(" + friendlyTypeName(typeId.name()) + ")");
		}
	}

}
