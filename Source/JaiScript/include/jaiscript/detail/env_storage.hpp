#pragma once

#ifndef __JAISCRIPT_DETAIL_ENV_STORAGE_HPP__
#define __JAISCRIPT_DETAIL_ENV_STORAGE_HPP__

#include <jaiscript/core/value.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace jai::detail {

    // An environment's id -> value table: the first entries live in the environment itself (a scope or a closure
    // defines a handful of names), a hash table takes over once they outgrow it and stays for the env's life
    template <class V, size_t N = 4>
    class env_id_map {
        struct slot { uint64_t key; V value; };
        slot inline_[N];
        uint32_t count_ = 0;
        std::unique_ptr<std::unordered_map<uint64_t, V>> big_;

        void spill() {
            big_ = std::make_unique<std::unordered_map<uint64_t, V>>();
            big_->reserve(N * 4);
            for (uint32_t i = 0; i < count_; ++i) { big_->emplace(inline_[i].key, inline_[i].value); }
            count_ = 0;
        }

    public:
        env_id_map() = default;
        env_id_map(const env_id_map&) = delete;
        env_id_map& operator=(const env_id_map&) = delete;

        V* find(uint64_t key) noexcept {
            if (big_) {
                auto it = big_->find(key);
                return it == big_->end() ? nullptr : &it->second;
            }
            for (uint32_t i = 0; i < count_; ++i) {
                if (inline_[i].key == key) { return &inline_[i].value; }
            }
            return nullptr;
        }
        const V* find(uint64_t key) const noexcept { return const_cast<env_id_map*>(this)->find(key); }
        bool contains(uint64_t key) const noexcept { return find(key) != nullptr; }

        V& operator[](uint64_t key) {
            if (V* found = find(key)) { return *found; }
            if (!big_) {
                if (count_ < N) {
                    inline_[count_] = slot{key, V{}};
                    return inline_[count_++].value;
                }
                spill();
            }
            return (*big_)[key];
        }

        bool erase(uint64_t key) {
            if (big_) { return big_->erase(key) != 0; }
            for (uint32_t i = 0; i < count_; ++i) {
                if (inline_[i].key == key) {
                    inline_[i] = inline_[--count_];
                    return true;
                }
            }
            return false;
        }

        void clear() noexcept {
            count_ = 0;
            if (big_) { big_->clear(); }
        }
        bool empty() const noexcept { return big_ ? big_->empty() : count_ == 0; }
        size_t size() const noexcept { return big_ ? big_->size() : count_; }

        template <class F> void for_each(F&& f) const {
            if (big_) {
                for (const auto& [key, value] : *big_) { f(key, value); }
                return;
            }
            for (uint32_t i = 0; i < count_; ++i) { f(inline_[i].key, inline_[i].value); }
        }
        template <class P> void erase_if(P&& drop) {
            if (big_) {
                for (auto it = big_->begin(); it != big_->end();) { it = drop(it->first, it->second) ? big_->erase(it) : std::next(it); }
                return;
            }
            for (uint32_t i = 0; i < count_;) {
                if (drop(inline_[i].key, inline_[i].value)) { inline_[i] = inline_[--count_]; } else { ++i; }
            }
        }
    };

    // An environment's own values in declaration order, each at a fixed address for the env's life (lookups and the vm's
    // caches hold pointers into it): the first few inside the environment, the rest in fixed chunks, none moved on growth
    class env_value_store {
        static constexpr size_t k_inline = 4;
        static constexpr size_t k_chunk = 16;
        struct chunk { alignas(script_value) unsigned char bytes[k_chunk * sizeof(script_value)]; };
        alignas(script_value) unsigned char inline_[k_inline * sizeof(script_value)];
        std::vector<std::unique_ptr<chunk>> chunks_;
        size_t size_ = 0;

        script_value* cell(size_t i) noexcept {
            if (i < k_inline) { return reinterpret_cast<script_value*>(inline_) + i; }
            const size_t rest = i - k_inline;
            return reinterpret_cast<script_value*>(chunks_[rest / k_chunk]->bytes) + rest % k_chunk;
        }
        void* next_cell() {
            if (size_ >= k_inline) {
                const size_t rest = size_ - k_inline;
                if (rest / k_chunk == chunks_.size()) { chunks_.push_back(std::make_unique<chunk>()); }
            }
            return cell(size_);
        }

    public:
        env_value_store() = default;
        env_value_store(const env_value_store&) = delete;
        env_value_store& operator=(const env_value_store&) = delete;
        ~env_value_store() { while (size_ != 0) { pop_back(); } }   // reverse declaration order (C++)

        script_value& push_back(const script_value& value) {
            void* at = next_cell();
            auto* placed = new (at) script_value(value);
            ++size_;
            return *placed;
        }
        script_value& push_back(script_value&& value) {
            void* at = next_cell();
            auto* placed = new (at) script_value(std::move(value));
            ++size_;
            return *placed;
        }
        void pop_back() noexcept {
            script_value* last = cell(size_ - 1);
            --size_;   // the slot leaves the store before its value dies (a destructor reentering the env sees it gone)
            last->~script_value();
        }
        void clear() noexcept { while (size_ != 0) { pop_back(); } }
        script_value& back() noexcept { return *cell(size_ - 1); }
        bool empty() const noexcept { return size_ == 0; }
        size_t size() const noexcept { return size_; }
        script_value& operator[](size_t i) noexcept { return *cell(i); }
        const script_value& operator[](size_t i) const noexcept { return *const_cast<env_value_store*>(this)->cell(i); }

        template <class Store, class Value>
        class basic_iterator {
            Store* store_;
            size_t i_;
        public:
            basic_iterator(Store* store, size_t i) : store_(store), i_(i) {}
            Value& operator*() const noexcept { return (*store_)[i_]; }
            basic_iterator& operator++() noexcept { ++i_; return *this; }
            bool operator!=(const basic_iterator& other) const noexcept { return i_ != other.i_; }
            bool operator==(const basic_iterator& other) const noexcept { return i_ == other.i_; }
        };
        using iterator = basic_iterator<env_value_store, script_value>;
        using const_iterator = basic_iterator<const env_value_store, const script_value>;
        iterator begin() noexcept { return {this, 0}; }
        iterator end() noexcept { return {this, size_}; }
        const_iterator begin() const noexcept { return {this, 0}; }
        const_iterator end() const noexcept { return {this, size_}; }
    };

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_ENV_STORAGE_HPP__
