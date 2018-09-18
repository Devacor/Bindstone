#ifndef _MV_VISITOR_H_
#define _MV_VISITOR_H_

#include <boost/variant.hpp>
#include <type_traits>

namespace MV {
	namespace detail {

		template<typename... Lambdas>
		struct lambda_visitor;

		template<typename Lambda1, typename... Lambdas>
		struct lambda_visitor<Lambda1, Lambdas...>
			: public lambda_visitor<Lambdas...>,
			public Lambda1 {

			using Lambda1::operator ();
			using lambda_visitor<Lambdas...>::operator ();

			lambda_visitor(Lambda1 l1, Lambdas... lambdas)
				: Lambda1(l1)
				, lambda_visitor<Lambdas...>(lambdas...) {}
		};

		template<typename Lambda1>
		struct lambda_visitor<Lambda1>
			:
			public Lambda1 {

			using Lambda1::operator ();

			lambda_visitor(Lambda1 l1)
				: Lambda1(l1) {}
		};
	}

	template<class...Fs>
	auto compose(Fs&& ...fs) {
		using visitor_type = detail::lambda_visitor<std::decay_t<Fs>...>;
		return visitor_type(std::forward<Fs>(fs)...);
	};

	template<class...Vs, class...Fs>
	auto visit(boost::variant<Vs...> &a_item, Fs&& ...fs) {
		return boost::apply_visitor(compose(std::forward<Fs>(fs)...), a_item);
	};

	template<class...Vs, class...Fs>
	void visit(std::vector<boost::variant<Vs...>> &a_items, Fs&& ...fs) {
		auto composedMethods{ compose(std::forward<Fs>(fs)...) };
		for (auto&& item : a_items) {
			boost::apply_visitor(composedMethods, item);
		}
	};
}

#endif
