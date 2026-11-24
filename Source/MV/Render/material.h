#ifndef _MV_RENDER_MATERIAL_H_
#define _MV_RENDER_MATERIAL_H_

#include "MV/Render/render.h"
#include "MV/Render/textures.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {

	struct MaterialContext {
		Draw2D& renderer;
		float time;
		const TransformMatrix& worldTransform;
		float alpha;
		int32_t cameraId;
	};

	// A uniform a material gives its shader: the uniform's name and a value of one kind. A drawable's overrides are a
	// list of the same kinds, naming only what that drawable changes.
	class MaterialParameter : public jai::property_owner<MaterialParameter> {
		friend jai::access;
	public:
		virtual ~MaterialParameter() = default;

		const std::string& name() const { return ourName.get(); }
		MaterialParameter& name(const std::string& a_name) { ourName = a_name; return *this; }

		// A texture takes the binding slot a_nextTextureSlot holds and moves it on.
		virtual void apply(Shader& a_shader, GLuint& a_nextTextureSlot) const {}
		virtual std::shared_ptr<MaterialParameter> clone() const;

	protected:
		JAI_NAMED_PROPERTY((std::string), "name", ourName);

		template<class Archive>
		void save(Archive& archive, std::uint32_t const) const {
			property_mgr.save(archive);
		}

		template<class Archive>
		void load(Archive& archive, std::uint32_t const) {
			property_mgr.load(archive);
		}

		template<typename Archive>
		static void load_and_construct(Archive& archive, jai::serialization::construct<MaterialParameter>& construct) {
			construct();
			construct->load(archive, 0);
		}
	};

	// One level of the parameter ladder: a value of one type and how it reaches the shader.
	#define MV_MATERIAL_PARAMETER(ParameterType, ValueType, DefaultValue) \
	class ParameterType : public jai::property_owner<ParameterType, MaterialParameter> { \
		friend jai::access; \
	public: \
		ParameterType() = default; \
		ParameterType(const std::string& a_name, const ValueType& a_value) { name(a_name); value = a_value; } \
		void apply(Shader& a_shader, GLuint& a_nextTextureSlot) const override; \
		std::shared_ptr<MaterialParameter> clone() const override { return std::make_shared<ParameterType>(name(), value.get()); } \
		JAI_PROPERTY((ValueType), value, DefaultValue); \
	protected: \
		template<class Archive> \
		void save(Archive& archive, std::uint32_t const) const { \
			property_mgr.save(archive); \
			MaterialParameter::save(archive, 0); \
		} \
		template<class Archive> \
		void load(Archive& archive, std::uint32_t const) { \
			property_mgr.load(archive); \
			MaterialParameter::load(archive, 0); \
		} \
		template<typename Archive> \
		static void load_and_construct(Archive& archive, jai::serialization::construct<ParameterType>& construct) { \
			construct(); \
			construct->load(archive, 0); \
		} \
	};

	MV_MATERIAL_PARAMETER(FloatParameter, float, 0.0f)
	MV_MATERIAL_PARAMETER(Vec2Parameter, Point<>, Point<>())
	MV_MATERIAL_PARAMETER(Vec3Parameter, Point<>, Point<>())
	MV_MATERIAL_PARAMETER(ColorParameter, Color, Color())
	MV_MATERIAL_PARAMETER(TextureParameter, std::shared_ptr<TextureHandle>, nullptr)

	#undef MV_MATERIAL_PARAMETER

	using MaterialParameters = std::vector<std::shared_ptr<MaterialParameter>>;

	// A uniform a shader stage declares that the engine does not set itself, which a parameter can name.
	struct MaterialUniform {
		std::string name;
		std::string glslType;
	};
	class Material;
	// What the material's vertex and fragment files declare, in the order they declare it.
	std::vector<MaterialUniform> materialUniforms(const Material& a_material);
	// A parameter of the kind a GLSL type takes, at its default; null for a type no parameter holds.
	std::shared_ptr<MaterialParameter> makeParameter(const std::string& a_glslType, const std::string& a_name);

	// A .material file's text, written and read as a scene file is: the object itself, compact.
	std::string materialToJson(const std::shared_ptr<Material>& a_material, Services& a_services);
	std::shared_ptr<Material> materialFromJson(const std::string& a_json, Services& a_services);
	MaterialParameters cloneParameters(const MaterialParameters& a_parameters);
	std::shared_ptr<MaterialParameter> findParameter(const MaterialParameters& a_parameters, const std::string& a_name);

	// What a drawable draws with: a shader program, a blend mode and the uniforms its program takes beyond the engine's
	// own (transformation, alpha, time, uvMin/uvMax, the drawable's textures, the lights). A .material file in the
	// shader folder is one, under its file's name; a .vert/.frag pair with no file is one too, implicitly, which keeps
	// the drawable's blend and the per-drawable clock.
	class Material : public jai::property_owner<Material> {
		friend jai::access;
		friend Draw2D;
	public:
		using ApplyFunction = std::function<void(Shader&, const MaterialContext&)>;

		Material() = default;
		Material(const std::string& a_id, const std::string& a_shaderProgramId) :
			ourId(a_id),
			ourShaderProgramId(a_shaderProgramId) {
		}

		JAI_PROPERTY((std::string), vertex, "default.vert");
		JAI_PROPERTY((std::string), fragment, "default.frag");
		JAI_NAMED_PROPERTY((BlendMode), "blendMode", blendMode, BLEND_DEFAULT);
		// Every drawable on the material reads one clock, so drawables with nothing of their own can share a draw; off,
		// each starts at a random point so copies of one thing do not move in step.
		JAI_PROPERTY((bool), sharedTime, false);
		JAI_PROPERTY((MaterialParameters), parameters, {});

		Material& setApply(ApplyFunction a_applyFunction) {
			applyFunction = std::move(a_applyFunction);
			return *this;
		}

		Material& blend(BlendMode a_mode) {
			blendMode = a_mode;
			return *this;
		}

		// The blend a drawable on this material draws with: the material's own, or for an implicit material the drawable's.
		BlendMode blendFor(BlendMode a_drawableBlend) const {
			return isImplicit ? a_drawableBlend : blendMode.get();
		}

		// Made for a .vert/.frag pair that has no .material file: nothing to save.
		bool implicit() const { return isImplicit; }

		// Whether drawables using this material may share one draw. apply sets nothing from a drawable but its
		// parameters, which every drawable on it shares; a C++ apply function may set more, so it draws on its own until
		// it says otherwise.
		Material& batchable(bool a_batchable) {
			declaredBatchable = a_batchable;
			return *this;
		}

		bool batchable() const {
			return declaredBatchable.value_or(!applyFunction);
		}

		// The parameters (textures binding from the slot a_nextTextureSlot holds on), then the apply function, which may
		// override them.
		void apply(Shader& a_shader, const MaterialContext& a_context, GLuint& a_nextTextureSlot) const;

		const std::string& id() const { return ourId; }
		const std::string& shaderProgramId() const { return ourShaderProgramId; }

	protected:
		template<class Archive>
		void save(Archive& archive, std::uint32_t const) const {
			property_mgr.save(archive);
		}

		template<class Archive>
		void load(Archive& archive, std::uint32_t const) {
			property_mgr.load(archive);
		}

		template<typename Archive>
		static void load_and_construct(Archive& archive, jai::serialization::construct<Material>& construct) {
			construct();
			construct->load(archive, 0);
		}

	private:
		std::string ourId;
		std::string ourShaderProgramId;
		ApplyFunction applyFunction;
		std::optional<bool> declaredBatchable;
		bool isImplicit = false;
	};

}

#endif
