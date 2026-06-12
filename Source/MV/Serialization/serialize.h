#ifndef _MV_SERIALIZE_H_
#define _MV_SERIALIZE_H_

#include <jaiscript/core/engine.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/properties/property_serialization.hpp>

#include "MV/Utility/services.hpp"

#include <sstream>
#include <string>

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

}

#endif
