#pragma once
#ifndef JAI_SERIALIZATION_CONSTRUCT_HPP
#define JAI_SERIALIZATION_CONSTRUCT_HPP

#include <memory>
#include <jaiscript/serialization/archive_impl.hpp>

namespace jai {

// Forward declarations for construct types
namespace serialization {
	template<typename T> class construct;
	template<typename T> class construct_unique;
}

// Friend class for serialization access
class access {
public:
	template<typename T, typename... Args>
	static std::shared_ptr<T> make_shared(Args&&... args) {
		return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
	}
	template<typename T, typename... Args>
	static std::unique_ptr<T> make_unique(Args&&... args) {
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}

	// Forward to T::load_and_construct - allows access to private methods via friendship
	// Templated on Archive to work with CRTP-based archives
	template<typename Archive, typename T>
	static auto load_and_construct(Archive& ar, serialization::construct<T>& c)
		-> decltype(T::load_and_construct(ar, c)) {
		return T::load_and_construct(ar, c);
	}

	template<typename Archive, typename T>
	static auto load_and_construct(Archive& ar, serialization::construct_unique<T>& c)
		-> decltype(T::load_and_construct(ar, c)) {
		return T::load_and_construct(ar, c);
	}
};

namespace serialization {

	// construct<T> - for load_and_construct pattern (types without default constructors)
	template<typename T>
	class construct {
		std::shared_ptr<T>& ptr_ref_;
		bool constructed_ = false;
	public:
		explicit construct(std::shared_ptr<T>& ptr) : ptr_ref_(ptr) {}

		template<typename... Args>
		void operator()(Args&&... args) {
			ptr_ref_ = ::jai::access::make_shared<T>(std::forward<Args>(args)...);
			constructed_ = true;
		}

		T* operator->() { return ptr_ref_.get(); }
		T& operator*() { return *ptr_ref_; }
		bool is_constructed() const { return constructed_; }
		std::shared_ptr<T>& get_ptr() { return ptr_ref_; }
	};

	template<typename T>
	class construct_unique {
		std::unique_ptr<T>& ptr_ref_;
		bool constructed_ = false;
	public:
		explicit construct_unique(std::unique_ptr<T>& ptr) : ptr_ref_(ptr) {}

		template<typename... Args>
		void operator()(Args&&... args) {
			ptr_ref_ = ::jai::access::make_unique<T>(std::forward<Args>(args)...);
			constructed_ = true;
		}

		T* operator->() { return ptr_ref_.get(); }
		T& operator*() { return *ptr_ref_; }
		bool is_constructed() const { return constructed_; }
		std::unique_ptr<T>& get_ptr() { return ptr_ref_; }
	};

} // namespace serialization
} // namespace jai

#endif // JAI_SERIALIZATION_CONSTRUCT_HPP
