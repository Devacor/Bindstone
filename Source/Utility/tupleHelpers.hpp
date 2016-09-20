#ifndef __MV_TUPLE_HELPERS_HPP__
#define __MV_TUPLE_HELPERS_HPP__

#include <tuple>

namespace MV {
	template<class T, class Tuple, std::size_t N>
	struct TupleAggregator {
		static void addToVector(const Tuple& t, std::vector<T> &a_aggregate) {
			TupleAggregator<T, Tuple, N - 1>::addToVector(t, a_aggregate);
			a_aggregate.emplace_back(std::ref(std::get<N - 1>(t)));
		}
	};

	template<class T, class Tuple>
	struct TupleAggregator<T, Tuple, 1> {
		static void addToVector(const Tuple& t, std::vector<T> &a_aggregate) {
			a_aggregate.emplace_back(std::ref(std::get<0>(t)));
		}
	};

	//Converts a tuple to a vector of pointers to the elements in the tuple.
	//Works with boost::any or chaiscript::Boxed_Value etc.
	template<class T, class... Args>
	std::vector<T> toVector(const std::tuple<Args...>& t) {
		std::vector<T> result;
		TupleAggregator<T, decltype(t), sizeof...(Args)>::addToVector(t, result);
		return result;
	}
}

#endif