#ifndef _MV_OPTIONALCALLS_H_
#define _MV_OPTIONALCALLS_H_
#include <type_traits>

#define EXPAND( x ) x

namespace detail
{
	template <typename T, typename NameGetter>
	struct has_member_impl
	{
		typedef char matched_return_type;
		typedef long unmatched_return_type;

		template <typename C>
		static matched_return_type f(typename NameGetter::template get<C>*);

		template <typename C>
		static unmatched_return_type f(...);

	public:
		static const bool value = (sizeof(f<T>(0)) == sizeof(matched_return_type));
	};
}

template <typename T, typename NameGetter>
struct has_member :
	std::integral_constant<bool, detail::has_member_impl<T, NameGetter>::value>
{ };

#define CREATE_CUSTOM_HAS_MEMBER_WITH_RETURN(name, member, returnType, parameters) \
struct check_has_##name { \
	template <typename T, \
	returnType (T::*)parameters = &T::member \
		> \
	struct get{}; \
}; \
template <typename T> \
struct has_##name : \
	has_member<T, check_has_##member>{ \
};

#define CREATE_CUSTOM_POSSIBLE_CALL_WITH_RETURN(name, member, returnType, parameters) \
CREATE_CUSTOM_HAS_MEMBER_WITH_RETURN(name, member, returnType, parameters) \
template<typename T, typename Enable = void>\
struct possible_call_object_##name{\
	possible_call_object_##name(T&, const returnType& a_result) : \
		result(a_result){\
	}\
	template<typename... Args>\
	returnType operator()(Args&&... a_parameters){\
		return result; \
	}\
	const returnType& result; \
}; \
\
template<typename T>\
struct possible_call_object_##name<T, typename std::enable_if<has_##name<T>::value, void>::type> {\
	possible_call_object_##name(T& a_action, const returnType&) : \
		action(a_action){\
	}\
	template<typename... Args>\
	returnType operator()(Args&&... a_arguments){\
		return action.member(std::forward<Args>(a_arguments)...); \
	}\
	T& action; \
}; \
template<typename T>\
possible_call_object_##name<T> possible_call_##name(T& a_action, const returnType& a_defaultResult) {\
	return{ a_action, a_defaultResult }; \
}

#define CREATE_CUSTOM_POSSIBLE_CALL_NO_RETURN(name, member, parameters) \
CREATE_CUSTOM_HAS_MEMBER_WITH_RETURN(name, member, void, parameters) \
template<typename T, typename Enable = void>\
struct possible_call_object_##name{\
	possible_call_object_##name(T&){}\
	template<typename... Args>\
	void operator()(Args&&... a_arguments){\
	}\
}; \
\
template<typename T>\
struct possible_call_object_##name<T, typename std::enable_if<has_##name<T>::value, void>::type> {\
	possible_call_object_##name(T& a_action) : \
		action(a_action){\
	}\
	template<typename... Args>\
	void operator()(Args&&... a_arguments){\
		action.member(std::forward<Args>(a_arguments)...); \
	}\
	T& action; \
}; \
template<typename T>\
possible_call_object_##name<T> possible_call_##name(T& a_action) {\
	return{ a_action }; \
}

#define GET_CREATE_CUSTOM_POSSIBLE_CALL(_1, _2, _3, _4, NAME, ...) NAME
#define CREATE_CUSTOM_POSSIBLE_CALL(...) EXPAND( GET_CREATE_CUSTOM_POSSIBLE_CALL(__VA_ARGS__, CREATE_CUSTOM_POSSIBLE_CALL_WITH_RETURN, CREATE_CUSTOM_POSSIBLE_CALL_NO_RETURN)(__VA_ARGS__) )

#define CREATE_POSSIBLE_CALL_NO_RETURN(member, parameters) CREATE_CUSTOM_POSSIBLE_CALL_NO_RETURN(member, member, parameters)
#define CREATE_POSSIBLE_CALL_WITH_RETURN(member, returnType, parameters) CREATE_CUSTOM_POSSIBLE_CALL_WITH_RETURN(member, member, returnType, parameters)

#define GET_CREATE_POSSIBLE_CALL(_1, _2, _3, NAME, ...) NAME
#define CREATE_POSSIBLE_CALL(...) EXPAND( GET_CREATE_POSSIBLE_CALL(__VA_ARGS__, CREATE_POSSIBLE_CALL_WITH_RETURN, CREATE_POSSIBLE_CALL_NO_RETURN)(__VA_ARGS__) )

#define CREATE_CUSTOM_HAS_MEMBER_NO_RETURN(name, method, parameters) CREATE_CUSTOM_HAS_MEMBER_WITH_RETURN(name, method, void, parameters)
#define CREATE_HAS_MEMBER_NO_RETURN(name, parameters) CREATE_CUSTOM_HAS_MEMBER_WITH_RETURN(name, name, void, parameters)

#define GET_CREATE_HAS_MEMBER(_1, _2, _3, _4, NAME, ...) NAME
#define CREATE_HAS_MEMBER(...) EXPAND( GET_CREATE_HAS_MEMBER(__VA_ARGS__, CREATE_CUSTOM_HAS_MEMBER_WITH_RETURN, CREATE_CUSTOM_HAS_MEMBER_NO_RETURN, CREATE_HAS_MEMBER_NO_RETURN)(__VA_ARGS__) )

#endif