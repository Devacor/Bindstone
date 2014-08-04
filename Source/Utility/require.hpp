#ifndef __MV_REQUIRE_H__
#define __MV_REQUIRE_H__

#undef require

#include <string>

namespace MV {
	class MVExceptionBase {
	public:
		MVExceptionBase(){}

		void setExceptionMessage(std::string a_exceptionPrefix, std::string a_exceptionMessage){
			exceptionMessage = "[" + a_exceptionPrefix + "] = (" + a_exceptionMessage + ")";
		}
		void setExceptionMessage(std::string a_exceptionPrefix){
			exceptionMessage = "[" + a_exceptionPrefix + "]";
		}
		const std::string getExceptionMessage(){
			return exceptionMessage;
		}
	protected:
		std::string exceptionMessage;
	};

	class DefaultException : public MVExceptionBase {
	public:
		DefaultException(){ setExceptionMessage("Default Exception"); }
		DefaultException(std::string a_exceptionMessage){ setExceptionMessage("Default Exception", a_exceptionMessage); }
	};
	class RangeException : public MVExceptionBase {
	public:
		RangeException(){ setExceptionMessage("Range Exception"); }
		RangeException(std::string a_exceptionMessage){ setExceptionMessage("Range Exception", a_exceptionMessage); }
	};
	class ResourceException : public MVExceptionBase {
	public:
		ResourceException(){ setExceptionMessage("Resource Exception"); }
		ResourceException(std::string a_exceptionMessage){ setExceptionMessage("Resource Exception", a_exceptionMessage); }
	};
	class PointerException : public MVExceptionBase {
	public:
		PointerException(){ setExceptionMessage("Pointer Exception"); }
		PointerException(std::string a_exceptionMessage){ setExceptionMessage("Pointer Exception", a_exceptionMessage); }
	};

	template <class exception_type>
	inline exception_type outputAssertionFailed(exception_type exception) {
		std::cerr << "ASSERTION FAILED! " << exception.getExceptionMessage() << std::endl;
		return exception;
	}

	//assert functions (named require to avoid potential name clashes with the common c macro "assert")
	template <class exception_type, class condition_type>
	inline void require(condition_type condition){
		if(!condition){
			throw outputAssertionFailed<exception_type>(exception_type());
		}
	}
	template <class exception_type, class condition_type>
	inline void require(condition_type condition, const exception_type &exception){
		if(!condition){
			throw outputAssertionFailed<exception_type>(exception);
		}
	}
	template <class condition_type>
	inline void require(condition_type condition){
		if(!condition){
			throw outputAssertionFailed<DefaultException>(DefaultException());
		}
	}
}

#endif
