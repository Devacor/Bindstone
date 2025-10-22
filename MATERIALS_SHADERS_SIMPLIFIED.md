# Simplified Material System Design

## Core Idea: Material as a Reusable Callback

Instead of complex parameter systems, a Material is just:
1. A shader program ID
2. A reusable configuration function
3. Optional default textures/blend mode

## Simplified Material Class

```cpp
class Material {
public:
    using SetupFunction = std::function<void(Shader*, Drawable*)>;
    
    Material(const std::string& id, const std::string& shaderProgramId)
        : id(id), shaderProgramId(shaderProgramId) {}
    
    // Chain-able configuration
    Material& setup(SetupFunction setupFn) {
        setupFunction = setupFn;
        return *this;
    }
    
    Material& texture(size_t slot, std::shared_ptr<TextureHandle> tex) {
        defaultTextures[slot] = tex;
        return *this;
    }
    
    Material& blend(BlendMode mode) {
        blendMode = mode;
        return *this;
    }
    
    // Apply to a drawable (called during render)
    void apply(Shader* shader, Drawable* drawable) const {
        // Apply default textures first
        for (auto& [slot, tex] : defaultTextures) {
            if (tex) shader->set("texture" + std::to_string(slot), tex, slot);
        }
        
        // Run the setup function
        if (setupFunction) {
            setupFunction(shader, drawable);
        }
        
        // User can still override
        if (drawable->userMaterialSettings) {
            drawable->userMaterialSettings(shader);
        }
    }
    
    const std::string& getShaderProgramId() const { return shaderProgramId; }
    BlendMode getBlendMode() const { return blendMode; }
    
private:
    std::string id;
    std::string shaderProgramId;
    SetupFunction setupFunction;
    std::map<size_t, std::shared_ptr<TextureHandle>> defaultTextures;
    BlendMode blendMode = BlendMode::Default;
};
```

## Usage Examples

### Simple Static Material
```cpp
// Define once
auto woodMaterial = std::make_shared<Material>("wood", "textured");
woodMaterial->texture(0, woodTexture)
            ->blend(BlendMode::Default)
            ->setup([](Shader* s, Drawable* d) {
                s->set("roughness", 0.8f);
                s->set("tint", Color(0.9f, 0.85f, 0.8f, 1.0f));
            });

// Use many times
drawable1->material(woodMaterial);
drawable2->material(woodMaterial);
drawable3->material(woodMaterial)->materialSettings([](Shader* s) {
    s->set("tint", Color(0.5f, 0.4f, 0.3f, 1.0f)); // Override tint for this one
});
```

### Dynamic Material with Runtime Logic
```cpp
auto waterMaterial = std::make_shared<Material>("water", "water");
waterMaterial->setup([](Shader* s, Drawable* d) {
    float time = d->owner()->renderer().currentTime();
    s->set("waveOffset", sin(time) * 0.1f);
    s->set("waveFrequency", 2.0f);
    
    // Access game state directly - no abstraction!
    auto scene = d->owner()->scene();
    if (scene->isStormy()) {
        s->set("waveAmplitude", 2.0f);
    } else {
        s->set("waveAmplitude", 0.5f);
    }
});
```

### Backward Compatible Drawable
```cpp
class Drawable {
    // Option 1: Use material
    std::shared_ptr<Material> material;
    
    // Option 2: Use old system (still works!)
    std::string shaderProgramId;
    std::function<void(Shader*)> userMaterialSettings;
    
    // In draw():
    if (material) {
        shaderProgram = renderer.getShader(material->getShaderProgramId());
        shaderProgram->use();
        material->apply(shaderProgram, this);
    } else {
        // Old path still works
        shaderProgram = renderer.getShader(shaderProgramId);
        shaderProgram->use();
        materialSettingsImplementation(shaderProgram);
        if (userMaterialSettings) userMaterialSettings(shaderProgram);
    }
};
```

## Benefits Over Complex Material System

1. **Still Simple**: Material is just a wrapper around the existing callback pattern
2. **Still Flexible**: Full access to shader, drawable, and game state in callbacks
3. **No Parameter System**: No need to declare types, names, defaults
4. **Reusable**: Define material once, use many times
5. **Composable**: Materials provide defaults, drawables can override
6. **Backward Compatible**: Old system still works
7. **Minimal Memory**: Just stores a function and some textures
8. **No Abstraction**: Direct shader access, no parameter mapping

## Benefits Over Current System

1. **Reusability**: Define common materials once
2. **Organization**: Materials can be managed/loaded as assets
3. **Defaults**: Built-in texture and blend mode defaults
4. **Naming**: Materials have IDs for easy reference
5. **Cleaner Drawable**: Less configuration code per drawable

## What We're NOT Adding

- ❌ Parameter type system
- ❌ Material instances with overrides  
- ❌ Variant/inheritance system
- ❌ Serialization complexity
- ❌ Abstract parameter getters/setters
- ❌ Material property blocks
- ❌ Shader parameter reflection

## Implementation Path

1. Add Material class (20-30 lines of code)
2. Add `material` pointer to Drawable
3. Update draw logic to check for material first
4. Keep everything else the same
5. Gradually create materials for common cases

This gives us 80% of the benefits with 20% of the complexity!