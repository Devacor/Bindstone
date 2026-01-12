#ifndef _MV_SERIALIZE_H_
#define _MV_SERIALIZE_H_

#include <jaiscript/jaiscript.hpp>
#include <jaiscript/properties/property_serialization.hpp>

#include "MV/Utility/services.hpp"

#include <sstream>
#include <string>

// Keep cereal includes for backwards compatibility
#include "cereal/cereal.hpp"
#include "cereal/types/set.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/variant.hpp"
#include "cereal/types/optional.hpp"
#include "cereal/types/base_class.hpp"
#include "cereal/types/polymorphic.hpp"
#include "cereal/archives/adapters.hpp"
#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/details/traits.hpp"

namespace MV {

	// ============================================================================
	// Base64 encoding (uses JaiScript's implementation)
	// ============================================================================

	inline std::string toBase64(const std::string& input) {
		return jai::base64_encode(input);
	}
	inline std::string fromBase64(const std::string& input) {
		return jai::base64_decode(input);
	}
	inline std::string toBase64(const char* input) {
		return jai::base64_encode(std::string(input));
	}
	inline std::string fromBase64(const char* input) {
		return jai::base64_decode(std::string(input));
	}

	// ============================================================================
	// JaiScript-based serialization (default)
	// ============================================================================
	// Uses jai::to_json, jai::from_json etc. from jaiscript/serialization/convenience.hpp
	// Engine is obtained from MV::Services. Overloads without Services use the singleton.

	// --- With explicit Services parameter ---

	template <typename T>
	std::string toBinaryString(const T& a_input, MV::Services& a_services) {
		auto* engine = a_services.get<jai::engine>();
		return jai::to_binary_string(*engine, a_input);
	}

	template <typename T>
	T fromBinaryString(const std::string& a_input, MV::Services& a_services) {
		auto* engine = a_services.get<jai::engine>();
		return jai::from_binary_string<T>(*engine, a_input, a_services);
	}

	template <typename T>
	std::string toJson(const T& a_input, MV::Services& a_services) {
		auto* engine = a_services.get<jai::engine>();
		return jai::to_json(*engine, a_input);
	}

	template <typename T>
	T fromJson(const std::string& a_input, MV::Services& a_services) {
		auto* engine = a_services.get<jai::engine>();
		return jai::from_json<T>(*engine, a_input, a_services);
	}

	// --- Cast helpers ---

	template <typename C, typename T>
	std::string toBinaryStringCast(const std::shared_ptr<T>& a_input, MV::Services& a_services) {
		return toBinaryString(std::static_pointer_cast<C>(a_input), a_services);
	}

	template <typename C, typename T>
	std::string toJsonCast(const std::shared_ptr<T>& a_input, MV::Services& a_services) {
		return toJson(std::static_pointer_cast<C>(a_input), a_services);
	}

	// --- Inline JSON helpers ---

	template <typename T>
	std::string toJsonInline(const T& a_input, MV::Services& a_services) {
		auto result = toJson(a_input, a_services);
		//{"value0": == 10
		result = result.substr(10, result.size() - 11); //strip the first 10 and last 1 characters.
		return result;
	}

	template <typename T>
	T fromJsonInline(const std::string& a_input, MV::Services& a_services) {
		return fromJson<T>("{\"value0\":" + a_input + "}", a_services);
	}

	// --- Base64 serialization helpers ---

	template <typename T>
	T fromBase64(const std::string& a_input, MV::Services& a_services) {
		auto* engine = a_services.get<jai::engine>();
		return jai::from_base64<T>(*engine, a_input, a_services);
	}

	template <typename T>
	std::string toBase64(const T& a_input, MV::Services& a_services) {
		auto* engine = a_services.get<jai::engine>();
		return jai::to_base64(*engine, a_input, a_services);
	}

	template <typename C, typename T>
	std::string toBase64Cast(const std::shared_ptr<T>& a_input, MV::Services& a_services) {
		return toBase64(std::static_pointer_cast<C>(a_input), a_services);
	}

	// ============================================================================
	// Cereal-based deserialization (for loading legacy files)
	// ============================================================================

	template <typename T>
	T fromJsonCereal(const std::string& a_input) {
		std::stringstream messageStream(a_input);
		cereal::JSONInputArchive input(messageStream);
		T result;
		input(result);
		return result;
	}

	template <typename T>
	T fromJsonCereal(const std::string& a_input, MV::Services& a_services) {
		std::stringstream messageStream(a_input);
		cereal::UserDataAdapter<MV::Services, cereal::JSONInputArchive> input(a_services, messageStream);
		T result;
		input(result);
		return result;
	}

	template <typename T>
	T fromBinaryStringCereal(const std::string& a_input) {
		std::istringstream messageStream(a_input, std::ios_base::in | std::ios_base::binary);
		cereal::PortableBinaryInputArchive input(messageStream);
		T result;
		input(result);
		return result;
	}

	template <typename T>
	T fromBinaryStringCereal(const std::string& a_input, MV::Services& a_services) {
		std::stringstream messageStream(a_input, std::ios_base::in | std::ios_base::binary);
		cereal::UserDataAdapter<MV::Services, cereal::PortableBinaryInputArchive> input(a_services, messageStream);
		T result;
		input(result);
		return result;
	}

}

#endif
