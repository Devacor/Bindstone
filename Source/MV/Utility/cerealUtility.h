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

#include "cereal/archives/adapters.hpp"

#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

#include "cereal/details/traits.hpp"

#include "MV/Utility/services.hpp"
#include "cerealInlineExtension.hpp"

#include <sstream>
#include <string>

namespace MV {

	inline std::string cerealEncodeWrapper(std::string const& unencoded_string) {
		return cereal::base64::encode(reinterpret_cast<const unsigned char *>(unencoded_string.c_str()), unencoded_string.size());
	}

	inline std::string toBase64(const std::string& input) {
		return cerealEncodeWrapper(input);
	}
	inline std::string fromBase64(const std::string& input) {
		return cerealEncodeWrapper(input);
	}
	inline std::string toBase64(const char* input) {
		return cerealEncodeWrapper(input);
	}
	inline std::string fromBase64(const char* input) {
		return cerealEncodeWrapper(input);
	}
    
	template <typename T>
	std::string toBinaryString(const T& a_input) {
		std::stringstream messageStream;
		{
			cereal::PortableBinaryOutputArchive output(messageStream);
			output(a_input);
		}
		return messageStream.str();
	}

	template <typename C, typename T>
	std::string toBinaryStringCast(const std::shared_ptr<T> &a_input) {
		return toBinaryString(std::static_pointer_cast<C>(a_input));
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
	T fromBinaryString(const std::string &a_input, MV::Services& a_services) {
		std::stringstream messageStream(a_input);
		cereal::UserDataAdapter<MV::Services, cereal::PortableBinaryInputArchive> input(a_services, messageStream);
		T result;
		input(result);
		return result;
	}
    
    template <typename T>
    T fromBase64(const std::string &a_input) {
        return fromBinaryString<T>(fromBase64(a_input));
    }
    
    template <typename T>
    std::string toBase64(const T& a_input) {
        return toBase64(toBinaryString(a_input));
    }
    
    template <typename C, typename T>
    std::string toBase64Cast(const std::shared_ptr<T> &a_input) {
        return toBase64(std::static_pointer_cast<C>(a_input));
    }

	template <typename T>
	std::string toJson(const T& a_input) {
		std::stringstream messageStream;
		{
			cereal::JSONOutputArchive output(messageStream);
			output(a_input);
		}
		return messageStream.str();
	}

	template <typename T>
	std::string toJsonInline(const T& a_input) {
		auto result = toJson(a_input);
		//{"value0": == 10
		result = result.substr(10, result.size() - 11); //strip the first 10 and last 1 characters.
		return result;
	}

	template <typename C, typename T>
	std::string toJsonCast(const std::shared_ptr<T> &a_input) {
		return toJson(std::static_pointer_cast<C>(a_input));
	}

	template <typename T>
	T fromJson(const std::string &a_input) {
		std::stringstream messageStream(a_input);
		cereal::JSONInputArchive input(messageStream);
		T result;
		input(result);
		return result;
	}

	template <typename T>
	T fromJsonInline(const std::string &a_input) {
		return fromJson<T>("{\"value0\":" + a_input + "}");
	}

	template <typename T>
	T fromJson(const std::string &a_input, MV::Services& a_services) {
		std::stringstream messageStream(a_input);
		cereal::UserDataAdapter<MV::Services, cereal::JSONInputArchive> input(a_services, messageStream);
		T result;
		input(result);
		return result;
	}

}

#endif
