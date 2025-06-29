# JaiScript Binary Serialization System

## Vision: Unified Property-Based Serialization

JaiScript's serialization system aims to unify Bindstone's existing property framework with multi-format serialization, providing a single source of truth for object metadata that drives JSON, binary, and network replication automatically.

## Core Design Philosophy

### Single Registration, Multiple Formats
```cpp
// Register once, serialize everywhere
make_class_builder<Player>(engine, "Player")
    .version(3)
    .property("name", &Player::name)
    .property("health", &Player::health)
    .property("experience", &Player::experience)
    .serialize_construct([](const script_value& data, int version) -> Player {
        // Custom construction logic with version handling
        return Player(data["name"].as_string(), data["health"].as_float());
    })
    .build();

// Automatically supports:
auto json = to_json(player);           // JSON format
auto binary = to_binary(player);       // Binary format
auto network_delta = sync(player);     // Network delta compression
```

### Integration with Existing Bindstone Properties

The system leverages Bindstone's mature property system as a foundation:

```cpp
class Player : public MV::PropertyOwner {
    MV_PROPERTY(std::string, name, "")
    MV_PROPERTY(float, health, 100.0f)
    MV_PROPERTY(int, experience, 0)
    
    // Auto-register with JaiScript
    void register_with_script(engine& e) {
        make_class_builder<Player>(e, "Player")
            .auto_import_properties(this->reflection())  // Import existing MV_PROPERTY definitions
            .version(current_version)
            .serialize_construct([](const script_value& data, int version) {
                // Custom construction if needed
            })
            .build();
    }
};
```

## Version-Based Schema Evolution

### Property Registration with Version History
```cpp
make_class_builder<GameEntity>(engine, "GameEntity")
    .version(4)  // Current version
    
    // Version 1 properties (original)
    .property("position", &GameEntity::position)
    .property("health", &GameEntity::health)
    
    // Version 2 additions
    .property("mana", &GameEntity::mana, version_added(2))
    .property("level", &GameEntity::level, version_added(2))
    
    // Version 3 changes
    .deleted_property<int>("score", version_removed(3))  // Keep for binary compatibility
    .property("experience", &GameEntity::experience, version_added(3))
    
    // Version 4 changes
    .property("inventory", &GameEntity::inventory, version_added(4))
    
    // Note: No need for explicit version_properties() - binary format is self-describing
    
    .build();
```

### Binary Compatibility Strategy

Binary serialization uses **self-describing format** for version independence:

```
Object Format:
[type_name: string]
[property_count: uint32]
[property_names: string[]]
[property_values: value[]]
```

Example serialization across versions:
```
Version 1: "GameEntity" | 2 | ["position", "health"] | [Vec3, float]
Version 2: "GameEntity" | 4 | ["position", "health", "mana", "level"] | [Vec3, float, float, int]
Version 3: "GameEntity" | 5 | ["position", "health", "mana", "level", "experience"] | [Vec3, float, float, int, int]
```

Key principles:
- **Self-describing format**: Property names embedded in binary data
- **Version independence**: Can deserialize any version order
- **Deleted property handling**: Missing properties skipped gracefully during load
- **Type safety**: Deleted properties retain type info for proper error handling

## Multi-Format Archive System

### Archive Interface
```cpp
class archive_writer {
public:
    virtual void write_int32(int32_t value) = 0;
    virtual void write_float64(double value) = 0;
    virtual void write_string(const std::string& value) = 0;
    virtual void write_binary(const void* data, size_t size) = 0;
    
    // Conditional serialization
    void set_enabled(const std::string& property_name, bool enabled);
    
    // Auto-serialize based on version
    void save_properties(const auto& object, int version);
    
    template<typename Base>
    void save_base(const auto& object);
};

class json_archive : public archive_writer {
    // JSON-specific implementation
};

class binary_archive : public archive_writer {
    // Binary-specific implementation with endianness handling
};

class network_archive : public archive_writer {
    // Delta compression + binary format
};
```

### Custom Serialization Support
```cpp
make_class_builder<ComplexEntity>(engine, "ComplexEntity")
    .property("basic_field", &ComplexEntity::basic_field)
    
    // Custom save logic for complex cases
    .serialize_save([](archive_writer& ar, const ComplexEntity& entity, int version) {
        // Conditional serialization based on runtime state
        ar.set_enabled("expensive_data", entity.should_serialize_expensive_data());
        
        // Custom compression
        if (entity.large_array.size() > 1000) {
            ar.write_compressed(entity.large_array);
        } else {
            ar.save_properties(entity, version);
        }
        
        // Handle base classes
        ar.save_base<GameObject>(entity);
    })
    
    // Custom construction with service injection
    .serialize_construct([serviceLocator](archive_reader& ar, int version) -> ComplexEntity {
        ComplexEntity entity;
        
        // Version-specific loading
        if (version >= 2) {
            ar.load_properties(entity, version);
        } else {
            // Manual migration from v1
            entity.new_field = migrate_from_old_data(ar.read_legacy_data());
        }
        
        // Inject services during construction
        entity.texture_manager = serviceLocator->get<TextureManager>();
        
        return entity;
    })
    .build();
```

## Network Integration

### Delta Compression Integration
Building on Bindstone's proven `DeltaVariable` system:

```cpp
// Existing Bindstone network code
DeltaVariable<float> health;
if (health.modified) {
    ar(health.value);
}

// JaiScript integration
make_class_builder<NetworkedEntity>(engine, "NetworkedEntity")
    .property("health", &NetworkedEntity::health, 
        network_flags::immediate | network_flags::reliable)
    .property("position", &NetworkedEntity::position,
        network_flags::unreliable | network_flags::compress_position())
    .property("animation_state", &NetworkedEntity::animation_state,
        network_flags::unreliable | network_flags::throttle(100ms))
    .build();

// Auto-generates network serialization compatible with existing system
```

### Automatic Network Synchronization
```cpp
// The property system drives network updates
auto updated_entities = network_pool.get_dirty_entities();
for (auto& entity : updated_entities) {
    network_archive ar(packet_buffer);
    entity.serialize_network_delta(ar);  // Only modified properties
    send_to_clients(packet_buffer);
}
```

## Performance Optimizations

### Compile-Time Optimization
```cpp
// Generate optimized serializers at registration time
.property("position", &Entity::position)
// Generates:
// - Direct memory offset calculations for binary
// - No virtual dispatch for common cases  
// - SIMD instructions for array serialization
// - Cached type information
```

### Runtime Optimization
```cpp
// Optimized self-describing serialization
void serialize_optimized(const Entity& e, binary_archive& ar) {
    // Cache property metadata for repeated serializations
    static const auto& prop_info = get_cached_property_info<Entity>();
    
    // Write property names once (can be optimized with string interning)
    ar.write_property_names(prop_info.names);
    
    // Fast property value serialization
    for (const auto& accessor : prop_info.accessors) {
        accessor.serialize(e, ar);  // Direct member access, no virtual dispatch
    }
}
```

## Wire Format Specification

### Binary Format
```
[Type Header]
  - Type name: string (length-prefixed)
  - Property count: uint32
  
[Property Names]
  - property_name_1: string
  - property_name_2: string
  - ... (property_count entries)
  
[Property Values]
  - property_value_1: typed_value 
  - property_value_2: typed_value
  - ... (property_count entries)
  
Each typed_value:
  - Type tag: uint8 (value_type enum)
  - Value data: variable size based on type
```

### JSON Format (for debugging/tools)
```json
{
  "__meta__": {
    "version": 3,
    "type": "GameEntity"
  },
  "position": {"x": 10.0, "y": 20.0, "z": 5.0},
  "health": 85.5,
  "experience": 1250
}
```

## Migration Strategy

### Phase 1: Auto-Import Existing Properties
- Scan existing `MV_PROPERTY` definitions
- Auto-generate JaiScript registrations
- Maintain existing serialization behavior

### Phase 2: Enhanced Serialization
- Add version support to existing classes
- Implement binary format alongside JSON
- Integrate with network layer

### Phase 3: Full Unification
- Replace manual serialization with property-driven
- Optimize binary format with fast paths
- Add tooling for schema evolution

## Tooling and Debugging

### Schema Validation
```cpp
// Compile-time checks
static_assert(Entity_v3::binary_size == 20, "Binary layout changed unexpectedly");

// Runtime validation
validate_schema<Entity>(version, property_list);
```

### Debug Support
```cpp
// Human-readable debug output
auto debug_info = get_serialization_info<Entity>();
for (const auto& prop : debug_info.properties) {
    std::cout << prop.name << " (" << prop.type << ") at offset " << prop.offset << std::endl;
}
```

## Comparison with Industry Standards

### vs. Unreal Engine
- ✅ Similar property-based approach
- ✅ Better version handling (explicit vs. implicit)
- ✅ Multi-format support built-in
- ❌ Less tooling integration (no visual editor yet)

### vs. Unity
- ✅ Much better version handling
- ✅ Binary format support
- ✅ C++ integration
- ❌ Less reflection metadata available

### vs. Custom AAA Engines
- ✅ Matches best practices (property-driven, versioned)
- ✅ Simpler API than most custom solutions
- ✅ Network integration designed from start
- ✅ Service injection built-in

## Conclusion

JaiScript's serialization system combines the best aspects of:
- **Bindstone's proven property system**
- **Cereal's multi-format approach** 
- **Game industry version handling best practices**
- **Modern C++ design patterns**

The result is a production-ready serialization solution that scales from simple JSON debugging to high-performance binary network protocols, all driven by a single property registration that integrates seamlessly with the scripting engine.

By building on Bindstone's existing property foundation, we can achieve a clean migration path while gaining significant new capabilities in versioning, binary serialization, and automatic network integration.