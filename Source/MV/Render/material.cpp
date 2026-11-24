#include "material.h"
#include "MV/Utility/services.hpp"
#include <jaiscript/serialization/json_archive.hpp>
#include <regex>
#include <set>

namespace MV {

	std::shared_ptr<MaterialParameter> MaterialParameter::clone() const {
		auto result = std::make_shared<MaterialParameter>();
		result->name(name());
		return result;
	}

	void FloatParameter::apply(Shader& a_shader, GLuint&) const {
		a_shader.set(name(), value.get(), false);
	}

	void Vec2Parameter::apply(Shader& a_shader, GLuint&) const {
		a_shader.setVec2(name(), value.get(), false);
	}

	void Vec3Parameter::apply(Shader& a_shader, GLuint&) const {
		a_shader.setVec3(name(), value.get(), false);
	}

	void ColorParameter::apply(Shader& a_shader, GLuint&) const {
		const Color& color = value.get();
		const float channels[] { color.R, color.G, color.B, color.A };
		a_shader.setFloats(name(), channels, 4);
	}

	void TextureParameter::apply(Shader& a_shader, GLuint& a_nextTextureSlot) const {
		if (value.get()) {
			a_shader.set(name(), value.get(), a_nextTextureSlot++, false);
		}
	}

	MaterialParameters cloneParameters(const MaterialParameters& a_parameters) {
		MaterialParameters result;
		for (auto&& parameter : a_parameters) {
			if (parameter) {
				result.push_back(parameter->clone());
			}
		}
		return result;
	}

	std::shared_ptr<MaterialParameter> findParameter(const MaterialParameters& a_parameters, const std::string& a_name) {
		for (auto&& parameter : a_parameters) {
			if (parameter && parameter->name() == a_name) {
				return parameter;
			}
		}
		return nullptr;
	}

	std::vector<MaterialUniform> materialUniforms(const Material& a_material) {
		static const std::set<std::string> ENGINE_UNIFORMS {
			"transformation", "alpha", "time", "uvMin", "uvMax", "worldFromClip",
			"normalMapped", "lightCount", "lightPositions", "lightColors", "lightInnerRadii", "ambient"
		};
		static const std::regex UNIFORM_DECLARATION(R"(uniform\s+(\w+)\s+(\w+)\s*;)");
		static const std::regex DRAWABLE_TEXTURE(R"(texture\d+)");
		std::vector<MaterialUniform> result;
		for (auto&& stage : { a_material.vertex.get(), a_material.fragment.get() }) {
			auto source = fileContents(SHADER_FOLDER + "/" + stage);
			for (auto found = std::sregex_iterator(source.begin(), source.end(), UNIFORM_DECLARATION); found != std::sregex_iterator(); ++found) {
				std::string name = (*found)[2];
				bool known = std::any_of(result.begin(), result.end(), [&](const MaterialUniform& a_uniform) { return a_uniform.name == name; });
				if (!known && !ENGINE_UNIFORMS.contains(name) && !std::regex_match(name, DRAWABLE_TEXTURE)) {
					result.push_back({ name, (*found)[1] });
				}
			}
		}
		return result;
	}

	std::shared_ptr<MaterialParameter> makeParameter(const std::string& a_glslType, const std::string& a_name) {
		if (a_glslType == "float") {
			return std::make_shared<FloatParameter>(a_name, 0.0f);
		} else if (a_glslType == "vec2") {
			return std::make_shared<Vec2Parameter>(a_name, Point<>());
		} else if (a_glslType == "vec3") {
			return std::make_shared<Vec3Parameter>(a_name, Point<>());
		} else if (a_glslType == "vec4") {
			return std::make_shared<ColorParameter>(a_name, Color());
		} else if (a_glslType == "sampler2D") {
			return std::make_shared<TextureParameter>(a_name, nullptr);
		}
		return nullptr;
	}

	std::string materialToJson(const std::shared_ptr<Material>& a_material, Services& a_services) {
		jai::serialization::json_archive_writer archive(0, a_services.get<jai::engine>());
		archive(a_material);
		return archive.str();
	}

	std::shared_ptr<Material> materialFromJson(const std::string& a_json, Services& a_services) {
		jai::serialization::json_archive_reader archive(a_json, a_services.get<jai::engine>());
		archive.set_user_context<Services>(&a_services);
		std::shared_ptr<Material> result;
		archive(result);
		return result;
	}

	void Material::apply(Shader& a_shader, const MaterialContext& a_context, GLuint& a_nextTextureSlot) const {
		for (auto&& parameter : *parameters) {
			if (parameter) {
				parameter->apply(a_shader, a_nextTextureSlot);
			}
		}
		if (applyFunction) {
			applyFunction(a_shader, a_context);
		}
	}

}
