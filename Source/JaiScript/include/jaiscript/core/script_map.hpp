#pragma once

#ifndef __JAISCRIPT_CORE_SCRIPT_MAP_HPP__
#define __JAISCRIPT_CORE_SCRIPT_MAP_HPP__

// script_map: the map behind TYPEID_MAP. Lookup is hashed (keys are equal exactly when script_value::operator<=> calls them
// equivalent: type rank, then payload; floats by bits, heavy values by identity); key order exists only where it is observed -
// iteration and everything built on it (serialization, keys()/values(), structural equality) - through a key-sorted view of the
// entries built on the first iteration after a change. Entries never move: a reference or pointer to a key or value stays valid
// until that entry is erased. An iteration iterator carries its key, so after the map changes under it (the body of a range-for
// inserting or erasing) it continues with the next greater key, as a std::map iterator walks; a find() iterator carries none
// (concurrent readers of a borrowed map only find). Included at the end of value.hpp, where script_value is complete.

#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <iterator>
#include <utility>

namespace jai {

    // the payload mix of script_value::map_key_hash (seeded by the type rank)
    inline size_t map_key_mix(script_value_type t, uint64_t x) noexcept {
        x ^= static_cast<uint64_t>(t) * 0x9E3779B97F4A7C15ull;
        x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
        return static_cast<size_t>(x);
    }

    struct script_value_hash {
        using is_transparent = void;
        size_t operator()(const script_value& v) const noexcept {
            const size_t k = v.raw_storage_index();   // the two common key kinds inline, the rows map_key_hash gives them
            if (k == script_value::TYPEID_INT) return map_key_mix(script_value_type::jai_int_type, static_cast<uint64_t>(v.unchecked_as_int()));
            if (k == script_value::TYPEID_STRING) return script_value::map_string_key_hash(v.unchecked_as_string());
            return v.map_key_hash();
        }
        size_t operator()(const map_string_key_probe& p) const noexcept { return script_value::map_string_key_hash(p.text); }
    };

    struct script_value_key_eq {
        using is_transparent = void;
        bool operator()(const script_value& a, const script_value& b) const {
            const size_t k = a.raw_storage_index();   // same-kind ints and strings compare their payloads, as operator<=> does
            if (k == b.raw_storage_index()) {
                if (k == script_value::TYPEID_INT) return a.unchecked_as_int() == b.unchecked_as_int();
                if (k == script_value::TYPEID_STRING) return a.unchecked_as_string() == b.unchecked_as_string();
            }
            return (a <=> b) == 0;
        }
        bool operator()(const script_value& a, const map_string_key_probe& p) const {
            return a.type() == script_value_type::jai_string_type && a.unchecked_as_string() == p.text;
        }
        bool operator()(const map_string_key_probe& p, const script_value& b) const { return (*this)(b, p); }
    };

    class script_map {
        using table_t = std::unordered_map<script_value, script_value, script_value_hash, script_value_key_eq>;
    public:
        using key_type = script_value;
        using mapped_type = script_value;
        using value_type = std::pair<const script_value, script_value>;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using key_compare = script_value_map_less;
        using node_type = table_t::node_type;

    private:
        table_t table_;
        uint64_t gen_ = 1;                          // moves on every insert, erase or clear
        mutable std::vector<value_type*> order_;    // the entries by key, valid while order_gen_ == gen_
        mutable uint64_t order_gen_ = 0;

        void changed() noexcept { ++gen_; }
        void build_order() const {
            if (order_gen_ == gen_) return;
            order_.clear();
            order_.reserve(table_.size());
            for (auto& e : const_cast<table_t&>(table_)) { order_.push_back(&e); }
            std::sort(order_.begin(), order_.end(), [](const value_type* a, const value_type* b) { return script_value_map_less{}(a->first, b->first); });
            order_gen_ = gen_;
        }
        static constexpr size_t npos = static_cast<size_t>(-1);
        // the position after `key` in key order (the first entry whose key is greater)
        size_t index_after(const script_value& key) const {
            build_order();
            auto it = std::upper_bound(order_.begin(), order_.end(), key,
                [](const script_value& k, const value_type* e) { return script_value_map_less{}(k, e->first); });
            return static_cast<size_t>(it - order_.begin());
        }

    public:
        template <bool Const>
        class basic_iterator {
            friend class script_map;
            template <bool> friend class basic_iterator;
            using map_ptr = std::conditional_t<Const, const script_map*, script_map*>;
            map_ptr m_ = nullptr;
            mutable value_type* node_ = nullptr;   // null = end
            mutable size_t index_ = npos;          // the node's place in the order view it was read from (npos: a find() result)
            mutable uint64_t gen_ = 0;             // the map generation that view belongs to
            mutable std::optional<script_value> key_;   // an iteration iterator's key (the resync point after a change)
            // an iteration iterator after the map changed: its own entry if the key is still there, else the next greater key
            void revalidate() const {
                if (!key_ || !m_ || gen_ == m_->gen_) return;
                auto f = m_->table_.find(*key_);
                if (f != m_->table_.end()) {
                    node_ = const_cast<script_map::value_type*>(&*f); index_ = npos; gen_ = m_->gen_;
                } else {
                    const_cast<basic_iterator*>(this)->seat(m_->index_after(*key_));
                }
            }

            basic_iterator(map_ptr m, value_type* n, size_t i, uint64_t g, bool keep_key) : m_(m), node_(n), index_(i), gen_(g) {
                if (keep_key && n) key_.emplace(n->first);
            }
            void seat(size_t i) const {
                if (i < m_->order_.size()) {
                    node_ = m_->order_[i]; index_ = i; gen_ = m_->gen_;
                    key_.emplace(node_->first);
                } else {
                    node_ = nullptr; index_ = npos; key_.reset();
                }
            }
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = script_map::value_type;
            using difference_type = ptrdiff_t;
            using pointer = std::conditional_t<Const, const script_map::value_type*, script_map::value_type*>;
            using reference = std::conditional_t<Const, const script_map::value_type&, script_map::value_type&>;

            basic_iterator() = default;
            template <bool C = Const, typename = std::enable_if_t<C>>
            basic_iterator(const basic_iterator<false>& o) : m_(o.m_), node_(o.node_), index_(o.index_), gen_(o.gen_), key_(o.key_) {}

            reference operator*() const { revalidate(); return *node_; }
            pointer operator->() const { revalidate(); return node_; }
            basic_iterator& operator++() {
                if (key_ && gen_ != m_->gen_) {   // the entry may be gone: continue after its key
                    seat(m_->index_after(*key_));
                } else if (m_->order_gen_ == m_->gen_ && gen_ == m_->gen_ && index_ != npos) {
                    seat(index_ + 1);
                } else {
                    seat(m_->index_after(key_ ? *key_ : node_->first));
                }
                return *this;
            }
            basic_iterator operator++(int) { basic_iterator t = *this; ++*this; return t; }
            template <bool C2>
            bool operator==(const basic_iterator<C2>& o) const { revalidate(); o.revalidate(); return node_ == o.node_; }
            template <bool C2>
            bool operator!=(const basic_iterator<C2>& o) const { return !(*this == o); }
        };
        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;

        script_map() = default;
        script_map(const script_map& o) : table_(o.table_) {}
        script_map(script_map&& o) noexcept : table_(std::move(o.table_)) { o.changed(); }
        script_map& operator=(const script_map& o) {
            if (this != &o) { table_ = o.table_; changed(); }
            return *this;
        }
        script_map& operator=(script_map&& o) noexcept {
            if (this != &o) { table_ = std::move(o.table_); changed(); o.changed(); }
            return *this;
        }
        inline ~script_map();
        // Every entry onto `work` (back = next to die) in the order the table destroys them: entry by entry, value then key
        void hand_off_entries(std::vector<script_value>& work) noexcept {
            const size_t base = work.size();
            while (!table_.empty()) {
                auto nh = table_.extract(table_.begin());
                work.push_back(std::move(nh.mapped()));
                work.push_back(std::move(nh.key()));
            }
            changed();
            order_.clear();
            std::reverse(work.begin() + static_cast<ptrdiff_t>(base), work.end());
        }

        void prepare_order() const { build_order(); }   // the order view built now (ahead of concurrent readers)
        size_type size() const noexcept { return table_.size(); }
        bool empty() const noexcept { return table_.empty(); }
        void clear() noexcept { table_.clear(); changed(); }

        iterator begin() { build_order(); iterator it(this, nullptr, npos, gen_, false); it.seat(0); return it; }
        const_iterator begin() const { build_order(); const_iterator it(this, nullptr, npos, gen_, false); it.seat(0); return it; }
        const_iterator cbegin() const { return begin(); }
        iterator end() noexcept { return iterator(); }
        const_iterator end() const noexcept { return const_iterator(); }
        const_iterator cend() const noexcept { return const_iterator(); }

        iterator find(const script_value& k) {
            auto it = table_.find(k);
            return it == table_.end() ? end() : iterator(this, &*it, npos, gen_, false);
        }
        const_iterator find(const script_value& k) const {
            auto it = table_.find(k);
            return it == table_.end() ? end() : const_iterator(this, const_cast<value_type*>(&*it), npos, gen_, false);
        }
        iterator find(const map_string_key_probe& p) {
            auto it = table_.find(p);
            return it == table_.end() ? end() : iterator(this, &*it, npos, gen_, false);
        }
        const_iterator find(const map_string_key_probe& p) const {
            auto it = table_.find(p);
            return it == table_.end() ? end() : const_iterator(this, const_cast<value_type*>(&*it), npos, gen_, false);
        }
        size_type count(const script_value& k) const { return table_.count(k); }
        bool contains(const script_value& k) const { return table_.find(k) != table_.end(); }

        script_value& operator[](const script_value& k) {
            auto [it, inserted] = table_.try_emplace(k);
            if (inserted) changed();
            return it->second;
        }
        script_value& operator[](script_value&& k) {
            auto [it, inserted] = table_.try_emplace(std::move(k));
            if (inserted) changed();
            return it->second;
        }
        script_value& at(const script_value& k) {
            auto it = table_.find(k);
            if (it == table_.end()) throw std::out_of_range("script_map::at");
            return it->second;
        }
        const script_value& at(const script_value& k) const {
            auto it = table_.find(k);
            if (it == table_.end()) throw std::out_of_range("script_map::at");
            return it->second;
        }

        template <typename K, typename V>
        std::pair<iterator, bool> emplace(K&& k, V&& v) {
            auto [it, inserted] = table_.emplace(std::forward<K>(k), std::forward<V>(v));
            if (inserted) changed();
            return {iterator(this, &*it, npos, gen_, false), inserted};
        }
        template <typename K, typename V>
        std::pair<iterator, bool> insert_or_assign(K&& k, V&& v) {
            auto [it, inserted] = table_.insert_or_assign(std::forward<K>(k), std::forward<V>(v));
            if (inserted) changed();
            return {iterator(this, &*it, npos, gen_, false), inserted};
        }
        std::pair<iterator, bool> insert(const value_type& v) {
            auto [it, inserted] = table_.insert(v);
            if (inserted) changed();
            return {iterator(this, &*it, npos, gen_, false), inserted};
        }
        std::pair<iterator, bool> insert(value_type&& v) {
            auto [it, inserted] = table_.insert(std::move(v));
            if (inserted) changed();
            return {iterator(this, &*it, npos, gen_, false), inserted};
        }

        size_type erase(const script_value& k) {
            const size_type n = table_.erase(k);
            if (n) changed();
            return n;
        }
        // the entry after `pos` in key order
        iterator erase(const_iterator pos) {
            pos.revalidate();
            iterator next(this, pos.node_, pos.index_, pos.gen_, false);
            next.key_ = pos.key_;
            ++next;   // seated (and keyed) before the erase: its node stays valid
            table_.erase(pos.node_->first);
            changed();
            return next;
        }
        iterator erase(iterator pos) { return erase(const_iterator(pos)); }

        node_type extract(const_iterator pos) {
            pos.revalidate();
            node_type nh = table_.extract(pos.node_->first);
            changed();
            return nh;
        }
        iterator insert(const_iterator, node_type&& nh) {
            auto r = table_.insert(std::move(nh));
            changed();
            return r.position == table_.end() ? end() : iterator(this, &*r.position, npos, gen_, false);
        }
    };

    namespace detail {
        // work: values to destroy, back first; a nested last-handle array<var> or map hands its elements on instead of
        // destroying them inside its own destructor, so nesting costs no native depth and the order is unchanged
        inline void release_work(std::vector<script_value>& work) noexcept {
            while (!work.empty()) {
                script_value v = std::move(work.back());
                work.pop_back();
                if (!v.release_flattens()) { continue; }
                if (v.raw_storage_index() == script_value::TYPEID_ARRAY) {
                    auto& vals = v.unchecked_get_array_storage()->values();
                    for (size_t i = vals.size(); i-- > 0;) { work.push_back(std::move(vals[i])); }
                    vals.clear();
                } else {
                    v.unchecked_get_map_storage()->hand_off_entries(work);
                }
            }
        }
        inline void release_values_flattened(std::vector<script_value>& values) noexcept {
            std::vector<script_value> work;
            work.reserve(values.size());
            for (size_t i = values.size(); i-- > 0;) { work.push_back(std::move(values[i])); }
            values.clear();
            release_work(work);
        }
    }

    inline script_map::~script_map() {
        for (const auto& e : table_) {
            if (e.second.release_flattens() || e.first.release_flattens()) {
                std::vector<script_value> work;
                hand_off_entries(work);
                detail::release_work(work);
                return;
            }
        }
    }

} // namespace jai

#endif
