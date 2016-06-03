#ifndef _MV_CEREALUTILITY_H_
#define _MV_CEREALUTILITY_H_

#include "cereal/cereal.hpp"
//common include types
#include "cereal/types/set.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"
#include "cereal/types/polymorphic.hpp"

#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

#include <sstream>
#include <string>

namespace MV {

	inline std::string toBase64(const std::string& input) {
		return cereal::base64::encode(input);
	}
	inline std::string fromBase64(const std::string& input) {
		return cereal::base64::decode(input);
	}
	inline std::string toBase64(const char* input) {
		return cereal::base64::encode(input);
	}
	inline std::string fromBase64(const char* input) {
		return cereal::base64::decode(input);
	}

	template <typename T>
	std::string toBase64(const T& a_input) {
		return toBase64(toBinaryString(a_input));
	}

	template <typename T>
	T fromBase64(const std::string &a_input) {
		return fromBinaryString(fromBase64(a_input));
	}

	template <typename T>
	std::string toBinaryString(const T& a_input) {
		std::stringstream messageStream;
		cereal::PortableBinaryOutputArchive output(messageStream);
		output(a_input);
		return messageStream.str();
	}

	template <typename T>
	T fromBinaryString(const std::string &a_input) {
		std::stringstream messageStream(a_input);
		cereal::PortableBinaryInputArchive input(messageStream);
		T result;
		input(result);
		return result;
	}

	template <typename T>
	std::string toJson(const T& a_input) {
		std::stringstream messageStream;
		cereal::JsonOutputArchive output(messageStream);
		output(a_input);
		return messageStream.str();
	}

	template <typename T>
	T fromJson(const std::string &a_input) {
		std::stringstream messageStream(a_input);
		cereal::JsonInputArchive input(messageStream);
		T result;
		input(result);
		return result;
	}
}

#endif
