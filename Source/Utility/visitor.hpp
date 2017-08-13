#ifndef _MV_VISITOR_H_
#define _MV_VISITOR_H_

#include <memory>
#include <type_traits>
#include <utility>

namespace MV {
	//minorly modified from: https://ciaranm.wordpress.com/2010/07/15/generic-lambda-visitors-or-writing-haskell-in-c0x-part-4/
	struct UnknownTypeForVariant;

	template <typename Want_, typename... Types_>
	struct SelectVariantType;

	template <typename Want_>
	struct SelectVariantType<Want_>
	{
		typedef UnknownTypeForVariant Type;
	};

	template <typename Want_, typename Try_, typename... Rest_>
	struct SelectVariantType<Want_, Try_, Rest_...>
	{
		typedef typename std::conditional<
			std::is_same<Want_, Try_>::value,
			Try_,
			typename SelectVariantType<Want_, Rest_...>::Type
		>::type Type;
	};

	template <typename Type_>
	struct ParameterTypes;

	template <typename C_, typename R_, typename P_>
	struct ParameterTypes<R_(C_::*)(P_)>
	{
		typedef P_ FirstParameterType;
		typedef R_ ReturnType;
	};

	template <typename C_, typename R_, typename P_>
	struct ParameterTypes<R_(C_::*)(P_) const>
	{
		typedef P_ FirstParameterType;
		typedef R_ ReturnType;
	};

	template <typename Lambda_>
	struct LambdaParameterTypes
	{
		typedef typename ParameterTypes<decltype(&Lambda_::operator())>::FirstParameterType FirstParameterType;
		typedef typename ParameterTypes<decltype(&Lambda_::operator())>::ReturnType ReturnType;
	};

	template <typename Type_>
	struct VariantVisitorVisit
	{
		virtual void visitThis(Type_ &) = 0;
	};

	template <typename... Types_>
	struct VariantVisitor :
		VariantVisitorVisit<Types_>...
	{
	};

	template <typename Visitor_, typename Underlying_, typename Result_, typename... Types_>
	struct VariantVisitorWrapperVisit;

	template <typename Visitor_, typename Underlying_, typename Result_>
	struct VariantVisitorWrapperVisit<Visitor_, Underlying_, Result_> :
		Visitor_
	{
		Underlying_ & underlying;
	std::function<Result_()> execute;

	VariantVisitorWrapperVisit(Underlying_ & u) :
		underlying(u)
	{
	}
	};

	template <typename Visitor_, typename Underlying_, typename Result_, typename Type_, typename... Rest_>
	struct VariantVisitorWrapperVisit<Visitor_, Underlying_, Result_, Type_, Rest_...> :
		VariantVisitorWrapperVisit<Visitor_, Underlying_, Result_, Rest_...>
	{
		VariantVisitorWrapperVisit(Underlying_ & u) :
		VariantVisitorWrapperVisit<Visitor_, Underlying_, Result_, Rest_...>(u)
	{
	}

	Result_ visitThis_returning(Type_ & t)
	{
		return this->underlying.visitThis(t);
	}

	virtual void visitThis(Type_ & t)
	{
		this->execute = std::bind(&VariantVisitorWrapperVisit::visitThis_returning, this, std::ref(t));
	}
	};

	template <typename Underlying_, typename Result_, typename... Types_>
	struct VariantVisitorWrapper :
		VariantVisitorWrapperVisit<VariantVisitor<Types_...>, Underlying_, Result_, Types_...>
	{
		VariantVisitorWrapper(Underlying_ & u) :
			VariantVisitorWrapperVisit<VariantVisitor<Types_...>, Underlying_, Result_, Types_...>(u)
		{
		}
	};

	template <typename... Types_>
	struct VariantValueBase
	{
		virtual ~VariantValueBase() = 0;

		virtual void accept(VariantVisitor<Types_...> &) = 0;
		virtual void accept(VariantVisitor<const Types_...> &) const = 0;
	};

	template <typename... Types_>
	VariantValueBase<Types_...>::~VariantValueBase() = default;

	template <typename Type_, typename... Types_>
	struct VariantValue :
		VariantValueBase<Types_...>
	{
		Type_ value;

		VariantValue(const Type_ & type) :
			value(type)
		{
		}

		virtual void accept(VariantVisitor<Types_...> & visitThisor)
		{
			static_cast<VariantVisitorVisit<Type_> &>(visitThisor).visitThis(value);
		}

		virtual void accept(VariantVisitor<const Types_...> & visitThisor) const
		{
			static_cast<VariantVisitorVisit<const Type_> &>(visitThisor).visitThis(value);
		}
	};

	template <typename... Types_>
	class Variant
	{
	private:
		std::unique_ptr<VariantValueBase<Types_...> > _value;

	public:
		template <typename Type_>
		Variant(const Type_ & value) :
			_value(new VariantValue<typename SelectVariantType<Type_, Types_...>::Type, Types_...>{ value })
		{
		}

		Variant(const Variant & other) = delete;

		Variant(Variant && other) :
			_value(std::move(other._value))
		{
		}

		template <typename Type_>
		Variant & operator= (const Type_ & value)
		{
			_value.reset(new VariantValue<typename SelectVariantType<Type_, Types_...>::Type, Types_...>{ value });
			return *this;
		}

		Variant & operator= (const Variant & other) = delete;

		Variant & operator= (Variant && other)
		{
			_value = std::move(other._value);
			return *this;
		}

		VariantValueBase<Types_...> & value()
		{
			return *_value;
		}

		const VariantValueBase<Types_...> & value() const
		{
			return *_value;
		}
	};

	template <typename Visitor_, typename Result_, typename Variant_>
	struct VariantVisitorWrapperTypeFinder;

	template <typename Visitor_, typename Result_, typename... Types_>
	struct VariantVisitorWrapperTypeFinder<Visitor_, Result_, const Variant<Types_...> &>
	{
		typedef VariantVisitorWrapper<Visitor_, Result_, const Types_...> Type;
	};

	template <typename Visitor_, typename Result_, typename... Types_>
	struct VariantVisitorWrapperTypeFinder<Visitor_, Result_, Variant<Types_...> &>
	{
		typedef VariantVisitorWrapper<Visitor_, Result_, Types_...> Type;
	};

	template <typename Result_, typename Variant_, typename Visitor_>
	Result_
		accept_returning(Variant_ && one_of, Visitor_ && visitThisor)
	{
		typename VariantVisitorWrapperTypeFinder<Visitor_, Result_, Variant_>::Type visitThisor_wrapper(visitThisor);
		one_of.value().accept(visitThisor_wrapper);
		return visitThisor_wrapper.execute();
	}

	template <typename Variant_, typename Visitor_>
	void accept(Variant_ && one_of, Visitor_ && visitThisor)
	{
		accept_returning<void>(one_of, visitThisor);
	}

	template <typename Result_, typename... Funcs_>
	struct LambdaVisitor;

	template <typename Result_>
	struct LambdaVisitor<Result_>
	{
		void visitThis(struct NotReallyAType);
	};

	template <typename Result_, typename Func_, typename... Rest_>
	struct LambdaVisitor<Result_, Func_, Rest_...> :
		LambdaVisitor<Result_, Rest_...>
	{
		Func_ & func;

	LambdaVisitor(Func_ & f, Rest_ & ... rest) :
		LambdaVisitor<Result_, Rest_...>(rest...),
		func(f)
	{
	}

	Result_ visitThis(typename LambdaParameterTypes<Func_>::FirstParameterType & v)
	{
		return func(v);
	}

	using LambdaVisitor<Result_, Rest_...>::visitThis;
	};

	template <typename... Funcs_>
	struct AllReturnSame;

	template <typename Func_>
	struct AllReturnSame<Func_>
	{
		enum { value = true };
	};

	template <typename A_, typename B_, typename... Funcs_>
	struct AllReturnSame<A_, B_, Funcs_...>
	{
		enum {
			value = std::is_same<typename LambdaParameterTypes<A_>::ReturnType, typename LambdaParameterTypes<B_>::ReturnType>::value &&
			AllReturnSame<B_, Funcs_...>::value
		};
	};

	template <typename...>
	struct SeenSoFar
	{
	};

	template <typename...>
	struct ExtendSeenSoFar;

	template <typename New_, typename... Current_>
	struct ExtendSeenSoFar<New_, SeenSoFar<Current_...> >
	{
		typedef SeenSoFar<Current_..., New_> Type;
	};

	template <typename...>
	struct AlreadySeen;

	template <typename Query_>
	struct AlreadySeen<SeenSoFar<>, Query_>
	{
		enum { value = false };
	};

	template <typename Query_, typename A_, typename... Rest_>
	struct AlreadySeen<SeenSoFar<A_, Rest_...>, Query_>
	{
		enum { value = std::is_same<Query_, A_>::value || AlreadySeen<SeenSoFar<Rest_...>, Query_>::value };
	};

	template <typename...>
	struct VariantDeduplicatorBuilder;

	template <typename... Values_>
	struct VariantDeduplicatorBuilder<SeenSoFar<Values_...> >
	{
		typedef Variant<Values_...> Type;
	};

	template <typename SeenSoFar_, typename Next_, typename... Funcs_>
	struct VariantDeduplicatorBuilder<SeenSoFar_, Next_, Funcs_...>
	{
		typedef typename std::conditional<
			AlreadySeen<SeenSoFar_, Next_>::value,
			typename VariantDeduplicatorBuilder<SeenSoFar_, Funcs_...>::Type,
			typename VariantDeduplicatorBuilder<typename ExtendSeenSoFar<Next_, SeenSoFar_>::Type, Funcs_...>::Type
		>::type Type;
	};

	template <typename... Funcs_>
	struct VariantDeduplicator
	{
		typedef typename VariantDeduplicatorBuilder<SeenSoFar<>, Funcs_...>::Type Type;
	};

	template <typename... Funcs_>
	struct WhenReturnType;

	template <typename FirstFunc_, typename... Funcs_>
	struct WhenReturnType<FirstFunc_, Funcs_...>
	{
		typedef typename std::conditional<
			AllReturnSame<FirstFunc_, Funcs_...>::value,
			typename LambdaParameterTypes<FirstFunc_>::ReturnType,
			typename VariantDeduplicator<
			typename LambdaParameterTypes<FirstFunc_>::ReturnType,
			typename LambdaParameterTypes<Funcs_>::ReturnType ...>::Type
		>::type Type;
	};

	template <typename Val_, typename... Funcs_>
	typename WhenReturnType<Funcs_...>::Type
		visit(Val_ && val, Funcs_ && ... funcs)
	{
		LambdaVisitor<typename WhenReturnType<Funcs_...>::Type, Funcs_...> visitThisor(funcs...);
		return accept_returning<typename WhenReturnType<Funcs_...>::Type>(val, visitThisor);
	}

	template <typename Val_, typename... Funcs_>
	void
		visit_each(const std::vector<Val_> & collection, Funcs_ && ... funcs)
	{
		for (auto&& val : collection) {
			LambdaVisitor<typename WhenReturnType<Funcs_...>::Type, Funcs_...> visitThisor(funcs...);
			accept_returning<typename WhenReturnType<Funcs_...>::Type>(val, visitThisor);
		}
	}
}

#endif
