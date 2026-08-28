#ifndef _MV_SERIALIZE_H_
#define _MV_SERIALIZE_H_

#include <jaiscript/core/engine.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/properties/property_serialization.hpp>

#include "MV/Utility/services.hpp"

#include <sstream>
#include <string>
#include <type_traits>

namespace MV {

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

	template <typename C, typename T>
	std::string toBinaryStringCast(const std::shared_ptr<T>& a_input, MV::Services& a_services) {
		return toBinaryString(std::static_pointer_cast<C>(a_input), a_services);
	}

	template <typename C, typename T>
	std::string toJsonCast(const std::shared_ptr<T>& a_input, MV::Services& a_services) {
		return toJson(std::static_pointer_cast<C>(a_input), a_services);
	}

	template <typename T>
	std::string toJsonInline(const T& a_input, MV::Services& a_services) {
		auto result = toJson(a_input, a_services);
		auto key = result.find("\"value0\"");
		auto colon = key == std::string::npos ? std::string::npos : result.find(':', key);
		auto end = result.rfind('}');
		if (colon == std::string::npos || end == std::string::npos || end <= colon) { return result; }
		auto begin = result.find_first_not_of(" \t\r\n", colon + 1);
		auto last = result.find_last_not_of(" \t\r\n", end - 1);
		return result.substr(begin, last - begin + 1);
	}

	template <typename T> struct jsonRootWrapped : std::false_type {};
	template <typename T> struct jsonRootWrapped<std::shared_ptr<T>> : std::true_type {};
	template <typename T> struct jsonRootWrapped<std::unique_ptr<T>> : std::true_type {};

	// Objects serialize inline at the document root; only owning pointers ride under a "value0" wrapper.
	template <typename T>
	T fromJsonInline(const std::string& a_input, MV::Services& a_services) {
		if constexpr (jsonRootWrapped<T>::value) {
			return fromJson<T>("{\"value0\":" + a_input + "}", a_services);
		} else {
			return fromJson<T>(a_input, a_services);
		}
	}

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
