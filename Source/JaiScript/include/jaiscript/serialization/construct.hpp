#pragma once
#ifndef JAI_SERIALIZATION_CONSTRUCT_HPP
#define JAI_SERIALIZATION_CONSTRUCT_HPP

#include <memory>
#include <jaiscript/serialization/archive.hpp>

namespace jai {

// Friend class for serialization access (like cereal::access)
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
};

namespace serialization {

	// construct<T> - for load_and_construct pattern (like cereal::construct)
	template<typename T>
	class construct {
		std::shared_ptr<T>& ptr_ref_;
		bool constructed_ = false;
	public:
		explicit construct(std::shared_ptr<T>& ptr) : ptr_ref_(ptr) {}

		template<typename... Args>
		void operator()(Args&&... args) {
			ptr_ref_ = access::make_shared<T>(std::forward<Args>(args)...);
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
			ptr_ref_ = access::make_unique<T>(std::forward<Args>(args)...);
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
