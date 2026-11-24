#include "inspector.h"
#include "MV/Render/material.h"

namespace Workbench {

	namespace {
		const std::vector<std::string> BLEND_LABELS { "default", "multiply", "add", "screen" };

		std::string textureLabel(const std::shared_ptr<MV::TextureHandle>& a_handle) {
			if (!a_handle || !a_handle->texture()) {
				return "(none)";
			}
			return std::filesystem::path(a_handle->texture()->name()).filename().string();
		}

		template <typename ParameterT, typename ValueT>
		std::function<void(ValueT)> parameterWriter(const std::shared_ptr<MV::MaterialParameter>& a_parameter) {
			std::weak_ptr<ParameterT> weakParameter = std::static_pointer_cast<ParameterT>(a_parameter);
			return [weakParameter](ValueT a_value) {
				if (auto locked = weakParameter.lock()) {
					locked->value = a_value;
				}
			};
		}
	}

	void InspectorPanel::addParameterField(const std::shared_ptr<MV::Scene::Node>& a_fieldHost, float a_width, const std::shared_ptr<MV::MaterialParameter>& a_parameter) {
		const MV::Size<> size(a_width, theme->fieldHeight);
		const std::string name = a_parameter->name();
		if (auto floatParameter = std::dynamic_pointer_cast<MV::FloatParameter>(a_parameter)) {
			auto write = parameterWriter<MV::FloatParameter, float>(a_parameter);
			makeScrubField(a_fieldHost, *services, *theme, *focus, "value", size, floatParameter->value.get(), write,
				[this, write, name](float a_oldValue, float a_newValue) {
					history->record(name, [write, a_newValue]() { write(a_newValue); }, [write, a_oldValue]() { write(a_oldValue); });
				});
		} else if (std::dynamic_pointer_cast<MV::Vec2Parameter>(a_parameter) || std::dynamic_pointer_cast<MV::Vec3Parameter>(a_parameter)) {
			const bool threeAxes = std::dynamic_pointer_cast<MV::Vec3Parameter>(a_parameter) != nullptr;
			std::weak_ptr<MV::MaterialParameter> weakParameter = a_parameter;
			auto read = [weakParameter]() {
				auto locked = weakParameter.lock();
				if (auto vec2 = std::dynamic_pointer_cast<MV::Vec2Parameter>(locked)) {
					return vec2->value.get();
				}
				auto vec3 = std::dynamic_pointer_cast<MV::Vec3Parameter>(locked);
				return vec3 ? vec3->value.get() : MV::Point<>();
			};
			auto write = [weakParameter](size_t a_axis, float a_value) {
				auto locked = weakParameter.lock();
				auto setAxis = [&](MV::Point<> a_point) {
					(a_axis == 0 ? a_point.x : a_axis == 1 ? a_point.y : a_point.z) = a_value;
					return a_point;
				};
				if (auto vec2 = std::dynamic_pointer_cast<MV::Vec2Parameter>(locked)) {
					vec2->value = setAxis(vec2->value.get());
				} else if (auto vec3 = std::dynamic_pointer_cast<MV::Vec3Parameter>(locked)) {
					vec3->value = setAxis(vec3->value.get());
				}
			};
			const auto current = read();
			std::vector<std::pair<std::string, float>> axes { { "X", current.x }, { "Y", current.y } };
			if (threeAxes) {
				axes.push_back({ "Z", current.z });
			}
			makeVectorRow(a_fieldHost, *services, *theme, *focus, a_width, axes, write,
				[this, write, name](size_t a_axis, float a_oldValue, float a_newValue) {
					history->record(name, [write, a_axis, a_newValue]() { write(a_axis, a_newValue); }, [write, a_axis, a_oldValue]() { write(a_axis, a_oldValue); });
				});
		} else if (auto colorParameter = std::dynamic_pointer_cast<MV::ColorParameter>(a_parameter)) {
			std::weak_ptr<MV::ColorParameter> weakColor = colorParameter;
			auto write = [weakColor](size_t a_channel, float a_value) {
				if (auto locked = weakColor.lock()) {
					auto color = locked->value.get();
					(a_channel == 0 ? color.R : a_channel == 1 ? color.G : a_channel == 2 ? color.B : color.A) = a_value;
					locked->value = color;
				}
			};
			const auto current = colorParameter->value.get();
			makeVectorRow(a_fieldHost, *services, *theme, *focus, a_width, { { "R", current.R }, { "G", current.G }, { "B", current.B }, { "A", current.A } }, write,
				[this, write, name](size_t a_channel, float a_oldValue, float a_newValue) {
					history->record(name, [write, a_channel, a_newValue]() { write(a_channel, a_newValue); }, [write, a_channel, a_oldValue]() { write(a_channel, a_oldValue); });
				});
		} else if (auto textureParameter = std::dynamic_pointer_cast<MV::TextureParameter>(a_parameter)) {
			auto write = parameterWriter<MV::TextureParameter, std::shared_ptr<MV::TextureHandle>>(a_parameter);
			std::weak_ptr<MV::TextureParameter> weakTexture = textureParameter;
			makeButton(a_fieldHost, *services, *theme, "value", size, textureLabel(textureParameter->value.get()), nullptr)->onAccept.connect("pick", [this, write, weakTexture, name](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
				openTexturePicker([this, write, weakTexture, name](const std::shared_ptr<MV::TextureHandle>& a_handle) {
					auto locked = weakTexture.lock();
					if (!locked) {
						return;
					}
					auto previous = locked->value.get();
					write(a_handle);
					history->record(name, [write, a_handle]() { write(a_handle); }, [write, previous]() { write(previous); });
				}, a_clickable->mouse().position());
			});
		}
	}

	void InspectorPanel::addStageRow(const std::shared_ptr<MV::Material>& a_material, bool a_vertex) {
		auto row = makeRow(a_vertex ? "vertex" : "fragment");
		auto fieldHost = row->make("field");
		fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
		std::weak_ptr<MV::Material> weakMaterial = a_material;
		auto apply = [this, weakMaterial, a_vertex](const std::string& a_file) {
			auto locked = weakMaterial.lock();
			if (!locked) {
				return;
			}
			(a_vertex ? locked->vertex : locked->fragment) = a_file;
			try {
				contentNode->renderer().materialStagesChanged(locked);
			} catch (std::exception& e) {
				MV::error("Material [", locked->id(), "] did not build with ", a_file, ": ", e.what());
			}
		};
		const std::string current = a_vertex ? a_material->vertex.get() : a_material->fragment.get();
		makeDropdown(fieldHost, *services, *theme, "value", MV::size(fieldWidth(), theme->fieldHeight), current, [this, apply, current, a_vertex](const MV::Point<int>& a_screenPoint) {
			if (!onOpenMenu) {
				return;
			}
			std::vector<MenuItem> items;
			for (auto&& file : contentNode->renderer().shaderStageFiles(a_vertex ? ".vert" : ".frag")) {
				MenuItem item;
				item.label = file;
				item.action = [this, apply, current, file]() {
					if (file == current) {
						return;
					}
					apply(file);
					history->record("stage", [apply, file]() { apply(file); }, [apply, current]() { apply(current); });
					rebuild();
				};
				items.push_back(std::move(item));
			}
			onOpenMenu(a_screenPoint, items);
		});
	}

	void InspectorPanel::addMaterialSection(const std::shared_ptr<MV::Material>& a_material, const std::shared_ptr<MV::Scene::Drawable>& a_drawable) {
		auto& renderer = contentNode->renderer();
		const bool saved = !renderer.materialFile(a_material->id()).empty();
		if (!addSectionHeader("Material: " + a_material->id() + (saved ? std::string() : " (not saved)"), "material#" + a_material->id())) {
			return;
		}
		std::weak_ptr<MV::Material> weakMaterial = a_material;
		std::weak_ptr<MV::Scene::Drawable> weakDrawable = a_drawable;
		addStageRow(a_material, true);
		addStageRow(a_material, false);
		{
			auto row = makeRow("blend");
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto write = [weakMaterial](int a_index) {
				if (auto locked = weakMaterial.lock()) {
					locked->blend(static_cast<MV::BlendMode>(a_index));
				}
			};
			makeCycleButton(fieldHost, *services, *theme, "value", MV::size(fieldWidth(), theme->fieldHeight), BLEND_LABELS, static_cast<int>(a_material->blendMode.get()),
				[this, write, previous = std::make_shared<int>(static_cast<int>(a_material->blendMode.get()))](int a_index) {
					write(a_index);
					history->record("blend", [write, a_index]() { write(a_index); }, [write, before = *previous]() { write(before); });
					*previous = a_index;
				});
		}
		{
			auto row = makeRow("shared time");
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto write = [weakMaterial](bool a_on) {
				if (auto locked = weakMaterial.lock()) {
					locked->sharedTime = a_on;
				}
			};
			makeToggle(fieldHost, *services, *theme, "value", a_material->sharedTime.get(), [this, write](bool a_on) {
				write(a_on);
				history->record("shared time", [write, a_on]() { write(a_on); }, [write, a_on]() { write(!a_on); });
			});
		}

		auto setParameters = [weakMaterial](const MV::MaterialParameters& a_parameters) {
			if (auto locked = weakMaterial.lock()) {
				locked->parameters = a_parameters;
			}
		};
		std::vector<MV::MaterialUniform> rows = MV::materialUniforms(*a_material);
		for (auto&& parameter : *a_material->parameters) {
			if (parameter && std::none_of(rows.begin(), rows.end(), [&](const MV::MaterialUniform& a_uniform) { return a_uniform.name == parameter->name(); })) {
				rows.push_back({ parameter->name(), "" });
			}
		}
		const float removeWidth = theme->fieldHeight;
		for (auto&& uniform : rows) {
			auto row = makeRow(uniform.name);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto parameter = MV::findParameter(*a_material->parameters, uniform.name);
			if (parameter) {
				addParameterField(fieldHost, fieldWidth() - removeWidth - theme->px(2.0f), parameter);
				auto remove = makeIconButton(fieldHost, *services, *theme, "remove", MV::size(removeWidth, removeWidth), Icon::Close, [this, weakMaterial, setParameters, parameter]() {
					auto locked = weakMaterial.lock();
					if (!locked) {
						return;
					}
					auto before = *locked->parameters;
					auto after = before;
					std::erase(after, parameter);
					setParameters(after);
					history->record("remove parameter", [setParameters, after]() { setParameters(after); }, [setParameters, before]() { setParameters(before); });
					rebuild();
				}, uniform.glslType.empty() ? "Remove (its shader no longer takes it)" : "Remove");
				remove->owner()->position(MV::point(fieldWidth() - removeWidth, 0.0f));
			} else if (auto made = MV::makeParameter(uniform.glslType, uniform.name)) {
				makeButton(fieldHost, *services, *theme, "add", MV::size(fieldWidth(), theme->fieldHeight), "Add " + uniform.glslType, [this, weakMaterial, setParameters, made]() {
					auto locked = weakMaterial.lock();
					if (!locked) {
						return;
					}
					auto before = *locked->parameters;
					auto after = before;
					after.push_back(made);
					setParameters(after);
					history->record("add parameter", [setParameters, after]() { setParameters(after); }, [setParameters, before]() { setParameters(before); });
					rebuild();
				});
			} else {
				makeLabel(fieldHost, *services, *theme, "value", MV::size(fieldWidth(), theme->fieldHeight), uniform.glslType + " (no parameter kind)");
			}
		}

		auto row = makeRow("file");
		auto fieldHost = row->make("field");
		fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
		makeButton(fieldHost, *services, *theme, "save", MV::size(fieldWidth(), theme->fieldHeight), saved ? "Save " + a_material->id() + ".material" : "Save as " + a_material->id() + ".material", [this, weakMaterial, weakDrawable]() {
			auto locked = weakMaterial.lock();
			if (!locked) {
				return;
			}
			if (locked->implicit()) {
				if (auto drawable = weakDrawable.lock()) {
					locked->blend(drawable->blend());
				}
			}
			if (!contentNode->renderer().saveMaterial(locked)) {
				MV::error("Material [", locked->id(), "] was not saved");
			}
			rebuild();
		});
	}

	void InspectorPanel::addOverridesGroup(const std::shared_ptr<MV::Scene::Drawable>& a_drawable) {
		auto material = a_drawable->material();
		if (!material) {
			return;
		}
		auto uniforms = MV::materialUniforms(*material);
		for (auto&& parameter : a_drawable->materialOverrides()) {
			if (parameter && std::none_of(uniforms.begin(), uniforms.end(), [&](const MV::MaterialUniform& a_uniform) { return a_uniform.name == parameter->name(); })) {
				uniforms.push_back({ parameter->name(), "" });
			}
		}
		if (uniforms.empty()) {
			return;
		}
		const size_t overridden = a_drawable->materialOverrides().size();
		if (!addGroupHeader("overrides", "overrides#" + a_drawable->id(), overridden == 0 ? std::string("none") : std::to_string(overridden) + (overridden == 1 ? " override" : " overrides"))) {
			return;
		}
		rowIndent += theme->px(12.0f);
		std::weak_ptr<MV::Scene::Drawable> weakDrawable = a_drawable;
		const float toggleWidth = theme->fieldHeight + theme->px(4.0f);
		for (auto&& uniform : uniforms) {
			auto row = makeRow(uniform.name);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth() + theme->pad, 0.0f));
			auto existing = MV::findParameter(a_drawable->materialOverrides(), uniform.name);
			auto materialValue = MV::findParameter(*material->parameters, uniform.name);
			auto made = existing ? existing : (materialValue ? materialValue->clone() : MV::makeParameter(uniform.glslType, uniform.name));
			auto apply = [weakDrawable, made, name = uniform.name](bool a_on) {
				if (auto locked = weakDrawable.lock()) {
					if (a_on) {
						locked->materialOverride(made);
					} else {
						locked->clearMaterialOverride(name);
					}
				}
			};
			if (made) {
				makeToggle(fieldHost, *services, *theme, "override", existing != nullptr, [this, apply](bool a_on) {
					apply(a_on);
					history->record("override", [apply, a_on]() { apply(a_on); }, [apply, a_on]() { apply(!a_on); });
					rebuild();
				});
			}
			if (existing) {
				auto valueHost = fieldHost->make("overrideValue");
				valueHost->position(MV::point(toggleWidth, 0.0f));
				addParameterField(valueHost, fieldWidth() - toggleWidth, existing);
			}
		}
		rowIndent -= theme->px(12.0f);
	}

}
