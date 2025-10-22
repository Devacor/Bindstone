# Materials and Shaders Design Document

## Overview

This document outlines the design for separating the concepts of Shaders and Materials in the Bindstone renderer, moving from the current merged approach to a more traditional material system.

## Current System Analysis

### Current Shader Class
The current `Shader` class in `render.h` is actually a hybrid that combines:
- **Shader Program Management**: Compiling, linking, and managing OpenGL shader programs
- **Uniform Cache**: Storing and retrieving uniform locations
- **Uniform Setting**: Methods to set various types of uniforms (textures, matrices, floats, vectors)

### Current Material-like Behavior
Material properties are currently spread across several places:
- **Shader Program ID**: Stored in each `Drawable` as a string
- **Textures**: Stored in `Drawable::ourTextures` map
- **Blend Modes**: Stored in `Drawable::blendModePreset`
- **Custom Uniforms**: Set via `materialSettings` callback function
- **Built-in Uniforms**: Set in `materialSettingsImplementation` (time, alpha, transformation)

## Proposed Design

### 1. ShaderProgram Class (renamed from Shader)
```cpp
class ShaderProgram {
public:
    // Core shader functionality only
    ShaderProgram(const std::string& id, GLuint programId, 
                  const std::string& vertexPath, const std::string& fragmentPath);
    
    void use();
    GLint getUniformLocation(const std::string& name);
    bool hasUniform(const std::string& name);
    
    // Direct uniform setters (low-level)
    void setFloat(GLint location, float value);
    void setVec2(GLint location, const Point<>& value);
    void setVec3(GLint location, const Point3<>& value);
    void setMatrix4(GLint location, const TransformMatrix& value);
    void setTexture(GLint location, GLuint textureId, GLuint textureUnit);
    
private:
    std::string id;
    GLuint programId;
    std::string vertexPath, fragmentPath;
    std::unordered_map<std::string, GLint> uniformCache;
};
```

### 2. Material Class (new)
```cpp
class Material {
public:
    struct Parameter {
        enum Type { Float, Vec2, Vec3, Vec4, Matrix4, Texture };
        Type type;
        std::string name;
        std::variant<float, Point<>, Point3<>, Color, TransformMatrix, 
                     std::shared_ptr<TextureHandle>> defaultValue;
    };
    
    Material(const std::string& id, const std::string& shaderProgramId);
    
    // Define material parameters
    Material& addParameter(const std::string& name, Parameter::Type type, 
                          const auto& defaultValue);
    
    // Set material values (these become the defaults)
    Material& setFloat(const std::string& name, float value);
    Material& setVec2(const std::string& name, const Point<>& value);
    Material& setTexture(const std::string& name, std::shared_ptr<TextureHandle> texture);
    // ... other setters
    
    // Apply this material to a shader program
    void apply(ShaderProgram* shader) const;
    
    // Create an instance with overrides
    std::shared_ptr<MaterialInstance> createInstance();
    
    // Getters
    const std::string& getShaderProgramId() const { return shaderProgramId; }
    BlendMode getBlendMode() const { return blendMode; }
    
private:
    std::string id;
    std::string shaderProgramId;
    std::map<std::string, Parameter> parameters;
    std::map<std::string, std::variant<...>> values;
    BlendMode blendMode = BlendMode::Default;
    bool twoSided = false;
    // Other material-wide settings
};
```

### 3. MaterialInstance Class (new)
```cpp
class MaterialInstance {
public:
    MaterialInstance(std::shared_ptr<Material> baseMaterial);
    
    // Override specific parameters
    MaterialInstance& setFloat(const std::string& name, float value);
    MaterialInstance& setTexture(const std::string& name, std::shared_ptr<TextureHandle> texture);
    // ... other setters
    
    // Apply base material + overrides to shader
    void apply(ShaderProgram* shader) const;
    
    Material* getBaseMaterial() const { return baseMaterial.get(); }
    
private:
    std::shared_ptr<Material> baseMaterial;
    std::map<std::string, std::variant<...>> overrides;
};
```

### 4. Updated Drawable Class
```cpp
class Drawable : public Component {
    // Option 1: Direct material instance
    std::shared_ptr<MaterialInstance> materialInstance;
    
    // Option 2: Keep backward compatibility
    std::string shaderProgramId;  // Deprecated path
    std::function<void(ShaderProgram*)> userMaterialSettings;  // Deprecated path
    
    // New material methods
    std::shared_ptr<Drawable> material(std::shared_ptr<Material> material);
    std::shared_ptr<Drawable> material(std::shared_ptr<MaterialInstance> instance);
    
    // Override material parameters on this drawable
    std::shared_ptr<Drawable> setMaterialFloat(const std::string& name, float value);
    std::shared_ptr<Drawable> setMaterialTexture(const std::string& name, 
                                                std::shared_ptr<TextureHandle> texture);
};
```

## Key Differences

### Conceptual Separation
| Current System | Proposed System |
|----------------|-----------------|
| Shader = Program + Parameter Management | ShaderProgram = Just GPU program |
| No material concept | Material = Template with defaults |
| Per-object callbacks | MaterialInstance = Per-object overrides |
| Uniforms set imperatively | Parameters defined declaratively |

### Data Organization
| Aspect | Current | Proposed |
|--------|---------|----------|
| Shader code | In Shader class | In ShaderProgram class |
| Default values | None (set in callbacks) | In Material class |
| Per-object values | Scattered (textures, callbacks) | In MaterialInstance |
| Blend modes | In Drawable | In Material |
| Parameter types | Implicit | Explicit Parameter struct |

### Usage Example

#### Current System:
```cpp
drawable->shader("premultiply")
    ->texture(myTexture)
    ->materialSettings([](Shader* shader) {
        shader->set("tint", Color(1, 0, 0, 1));
        shader->set("intensity", 0.5f);
    });
```

#### Proposed System:
```cpp
// Create material once
auto glassMaterial = Material::create("glass", "transparentShader")
    ->addParameter("tint", Parameter::Vec4, Color(1, 1, 1, 1))
    ->addParameter("intensity", Parameter::Float, 1.0f)
    ->addParameter("mainTexture", Parameter::Texture, nullptr)
    ->setBlendMode(BlendMode::Alpha);

// Use on many objects with different values
drawable->material(glassMaterial)
    ->setMaterialTexture("mainTexture", myTexture)
    ->setMaterialVec4("tint", Color(1, 0, 0, 1))
    ->setMaterialFloat("intensity", 0.5f);
```

## Benefits of New System

1. **Clear Separation of Concerns**: Shaders handle GPU code, Materials handle parameters
2. **Reusability**: Materials can be defined once and reused with variations
3. **Type Safety**: Parameters have explicit types
4. **Tool-Friendly**: Materials can be serialized, edited in tools, hot-reloaded
5. **Performance**: Material instances can be batched by base material
6. **Backward Compatible**: Can support old system during transition

## Migration Strategy

1. **Phase 1**: Rename Shader to ShaderProgram internally (typedef for compatibility)
2. **Phase 2**: Implement Material and MaterialInstance classes
3. **Phase 3**: Add material support to Drawable alongside existing system
4. **Phase 4**: Migrate built-in rendering to use materials
5. **Phase 5**: Deprecate old callback-based system
6. **Phase 6**: Remove deprecated code

## Implementation Notes

- Materials should be managed by the renderer (like shaders currently are)
- Material IDs should be strings for easy reference (like shader IDs)
- Consider caching material parameter layouts for performance
- Support material inheritance/variants in the future
- Add material hot-reloading for development