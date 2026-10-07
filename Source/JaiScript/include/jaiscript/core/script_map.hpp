#pragma once

#ifndef __JAISCRIPT_CORE_SCRIPT_MAP_HPP__
#define __JAISCRIPT_CORE_SCRIPT_MAP_HPP__

// script_map: the map behind TYPEID_MAP. Lookup is hashed through an open-addressing index (keys are equal exactly when
// script_value::operator<=> calls them equivalent: type rank, then payload; floats by bits, heavy values by identity); key order exists only where it is observed -
// iteration and everything built on it (serialization, keys()/values(), structural equality) - through a key-sorted view of the
// entries built on the first iteration after a change. Entries never move: a reference or pointer to a key or value stays valid
// until that entry is erased. An iteration iterator carries its key, so after the map changes under it (the body of a range-for
// inserting or erasing) it continues with the next greater key, as a std::map iterator walks; a find() iterator carries none
// (concurrent readers of a borrowed map only find). Included at the end of value.hpp, where script_value is complete.

#include <bit>
#include <cstring>
#include <string_view>
#include <tuple>
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
    public:
        using key_type = script_value;
        using mapped_type = script_value;
        using value_type = std::pair<const script_value, script_value>;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using key_compare = script_value_map_less;

    private:
        // An entry lives in its own allocation until it is erased (references and pointers into it stay valid); the index is
        // open addressing over {hash, entry}: a probe compares the cached hash before it touches an entry
        // A string key of at most eight bytes keeps them beside the hash: a probe settles it without reaching the key's text
        static constexpr uint32_t no_short = UINT32_MAX;
        struct short_key { uint64_t text = 0; uint32_t len = no_short; };
        static short_key short_of(std::string_view t) noexcept {
            short_key k;
            if (t.size() <= 8) { std::memcpy(&k.text, t.data(), t.size()); k.len = static_cast<uint32_t>(t.size()); }
            return k;
        }
        static short_key short_of(const script_value& v) noexcept {
            return v.raw_storage_index() == script_value::TYPEID_STRING ? short_of(std::string_view(v.unchecked_as_string())) : short_key{};
        }
        static short_key short_of(const map_string_key_probe& p) noexcept { return short_of(p.text); }
        struct entry {
            size_t hash;
            short_key brief;
            value_type kv;
            template <typename K, typename... V>
            entry(size_t h, K&& k, V&&... v) : hash(h), kv(std::piecewise_construct, std::forward_as_tuple(std::forward<K>(k)), std::forward_as_tuple(std::forward<V>(v)...)) {
                brief = short_of(kv.first);
            }
        };
        // the probe's key test: two short string keys by their bytes, anything else by the key equality
        template <typename K>
        static bool same_key(const entry* e, const short_key& b, const K& key) {
            if (b.len != no_short && e->brief.len != no_short) { return b.len == e->brief.len && b.text == e->brief.text; }
            return script_value_key_eq{}(e->kv.first, key);
        }
        struct slot { size_t hash; entry* e; };
        static constexpr size_t min_capacity = 8;

        std::vector<slot> slots_;                   // empty, or a power of two
        size_t size_ = 0;
        unsigned shift_ = 64;                       // 64 - log2(capacity): the home slot is the top bits of hash * golden ratio
        uint64_t gen_ = 1;                          // moves on every insert, erase or clear
        mutable std::vector<value_type*> order_;    // the entries by key, valid while order_gen_ == gen_
        mutable uint64_t order_gen_ = 0;

        void changed() noexcept { ++gen_; }
        size_t home(size_t h) const noexcept { return static_cast<size_t>((static_cast<uint64_t>(h) * 0x9E3779B97F4A7C15ull) >> shift_); }
        size_t mask() const noexcept { return slots_.size() - 1; }

        template <typename K>
        entry* find_entry(const K& key) const {
            if (size_ == 0) return nullptr;
            const size_t h = script_value_hash{}(key);
            const short_key b = short_of(key);
            for (size_t i = home(h);; i = (i + 1) & mask()) {
                const slot& s = slots_[i];
                if (!s.e) return nullptr;
                if (s.hash == h && same_key(s.e, b, key)) return s.e;
            }
        }
        void place(entry* e) noexcept {
            size_t i = home(e->hash);
            while (slots_[i].e) { i = (i + 1) & mask(); }
            slots_[i] = slot{e->hash, e};
        }
        void grow_for(size_t count) {
            if (count * 8 <= slots_.size() * 7) return;
            size_t cap = slots_.empty() ? min_capacity : slots_.size() * 2;
            while (count * 8 > cap * 7) { cap *= 2; }
            std::vector<slot> old(cap, slot{0, nullptr});
            old.swap(slots_);
            shift_ = 64u - static_cast<unsigned>(std::countr_zero(cap));
            for (const slot& s : old) { if (s.e) place(s.e); }
        }
        // the entry for key, inserted (made by make(hash, key)) when absent
        template <typename K, typename Make>
        std::pair<entry*, bool> find_or_insert(K&& key, Make&& make) {
            const size_t h = script_value_hash{}(key);
            if (size_ != 0) {
                const short_key b = short_of(key);
                for (size_t i = home(h);; i = (i + 1) & mask()) {
                    const slot& s = slots_[i];
                    if (!s.e) break;
                    if (s.hash == h && same_key(s.e, b, key)) return {s.e, false};
                }
            }
            grow_for(size_ + 1);
            entry* e = make(h, std::forward<K>(key));
            place(e);
            ++size_;
            changed();
            return {e, true};
        }
        // the index without key's entry (backward shift: no tombstones); the entry, still allocated, or null
        entry* unlink(const script_value& key) {
            if (size_ == 0) return nullptr;
            const size_t h = script_value_hash{}(key);
            size_t i = home(h);
            for (;; i = (i + 1) & mask()) {
                const slot& s = slots_[i];
                if (!s.e) return nullptr;
                if (s.hash == h && script_value_key_eq{}(s.e->kv.first, key)) break;
            }
            entry* gone = slots_[i].e;
            for (size_t j = i;;) {
                j = (j + 1) & mask();
                if (!slots_[j].e) break;
                const size_t k = home(slots_[j].hash);
                if ((j > i && (k <= i || k > j)) || (j < i && k <= i && k > j)) { slots_[i] = slots_[j]; i = j; }
            }
            slots_[i] = slot{0, nullptr};
            --size_;
            changed();
            return gone;
        }
        // every entry, detached from the index: the map is empty and consistent before any value dies
        std::vector<entry*> detach_all() noexcept {
            std::vector<entry*> out;
            out.reserve(size_);
            for (slot& s : slots_) { if (s.e) { out.push_back(s.e); s = slot{0, nullptr}; } }
            size_ = 0;
            return out;
        }
        void copy_from(const script_map& o) {
            grow_for(o.size_);
            for (const slot& s : o.slots_) {
                if (!s.e) continue;
                place(new entry(s.hash, s.e->kv.first, s.e->kv.second));
                ++size_;
            }
        }
        void take_from(script_map& o) noexcept {
            slots_ = std::move(o.slots_); o.slots_.clear();
            size_ = o.size_; o.size_ = 0;
            shift_ = o.shift_; o.shift_ = 64;
        }

        void build_order() const {
            if (order_gen_ == gen_) return;
            order_.clear();
            order_.reserve(size_);
            for (const slot& s : slots_) { if (s.e) order_.push_back(&s.e->kv); }
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
                if (entry* f = m_->find_entry(*key_)) {
                    node_ = &f->kv; index_ = npos; gen_ = m_->gen_;
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
        script_map(const script_map& o) { copy_from(o); }
        script_map(script_map&& o) noexcept { take_from(o); o.changed(); }
        script_map& operator=(const script_map& o) {
            if (this != &o) {
                script_map fresh(o);
                std::vector<entry*> dying = detach_all();
                take_from(fresh);
                changed();
                for (entry* e : dying) { delete e; }
            }
            return *this;
        }
        script_map& operator=(script_map&& o) noexcept {
            if (this != &o) {
                std::vector<entry*> dying = detach_all();
                take_from(o);
                changed(); o.changed();
                for (entry* e : dying) { delete e; }
            }
            return *this;
        }
        inline ~script_map();
        // Every entry onto `work` (back = next to die) in the order clear() destroys them: entry by entry, value then key
        void hand_off_entries(std::vector<script_value>& work) noexcept {
            const size_t base = work.size();
            std::vector<entry*> dying = detach_all();
            for (entry* e : dying) {
                work.push_back(std::move(e->kv.second));
                work.push_back(std::move(const_cast<script_value&>(e->kv.first)));
                delete e;
            }
            changed();
            order_.clear();
            std::reverse(work.begin() + static_cast<ptrdiff_t>(base), work.end());
        }

        void prepare_order() const { build_order(); }   // the order view built now (ahead of concurrent readers)
        size_type size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }
        void clear() noexcept {
            std::vector<entry*> dying = detach_all();
            changed();
            for (entry* e : dying) { delete e; }
        }

        iterator begin() { build_order(); iterator it(this, nullptr, npos, gen_, false); it.seat(0); return it; }
        const_iterator begin() const { build_order(); const_iterator it(this, nullptr, npos, gen_, false); it.seat(0); return it; }
        const_iterator cbegin() const { return begin(); }
        iterator end() noexcept { return iterator(); }
        const_iterator end() const noexcept { return const_iterator(); }
        const_iterator cend() const noexcept { return const_iterator(); }

        iterator find(const script_value& k) {
            entry* e = find_entry(k);
            return e ? iterator(this, &e->kv, npos, gen_, false) : end();
        }
        const_iterator find(const script_value& k) const {
            entry* e = find_entry(k);
            return e ? const_iterator(this, &e->kv, npos, gen_, false) : end();
        }
        iterator find(const map_string_key_probe& p) {
            entry* e = find_entry(p);
            return e ? iterator(this, &e->kv, npos, gen_, false) : end();
        }
        const_iterator find(const map_string_key_probe& p) const {
            entry* e = find_entry(p);
            return e ? const_iterator(this, &e->kv, npos, gen_, false) : end();
        }
        size_type count(const script_value& k) const { return find_entry(k) ? 1 : 0; }
        bool contains(const script_value& k) const { return find_entry(k) != nullptr; }

        script_value& operator[](const script_value& k) {
            return find_or_insert(k, [](size_t h, const script_value& key) { return new entry(h, key); }).first->kv.second;
        }
        script_value& operator[](script_value&& k) {
            return find_or_insert(std::move(k), [](size_t h, script_value&& key) { return new entry(h, std::move(key)); }).first->kv.second;
        }
        script_value& at(const script_value& k) {
            entry* e = find_entry(k);
            if (!e) throw std::out_of_range("script_map::at");
            return e->kv.second;
        }
        const script_value& at(const script_value& k) const {
            entry* e = find_entry(k);
            if (!e) throw std::out_of_range("script_map::at");
            return e->kv.second;
        }

        template <typename K, typename V>
        std::pair<iterator, bool> emplace(K&& k, V&& v) {
            auto [e, inserted] = find_or_insert(script_value(std::forward<K>(k)), [&](size_t h, script_value&& key) { return new entry(h, std::move(key), std::forward<V>(v)); });
            return {iterator(this, &e->kv, npos, gen_, false), inserted};
        }
        template <typename K, typename V>
        std::pair<iterator, bool> insert_or_assign(K&& k, V&& v) {
            bool fresh = false;
            auto [e, inserted] = find_or_insert(script_value(std::forward<K>(k)), [&](size_t h, script_value&& key) { fresh = true; return new entry(h, std::move(key), std::forward<V>(v)); });
            if (!fresh) { e->kv.second = std::forward<V>(v); }
            return {iterator(this, &e->kv, npos, gen_, false), inserted};
        }
        std::pair<iterator, bool> insert(const value_type& v) { return emplace(v.first, v.second); }
        std::pair<iterator, bool> insert(value_type&& v) { return emplace(v.first, std::move(v.second)); }

        size_type erase(const script_value& k) {
            entry* gone = unlink(k);
            if (!gone) return 0;
            delete gone;
            return 1;
        }
        // the entry after `pos` in key order
        iterator erase(const_iterator pos) {
            pos.revalidate();
            iterator next(this, pos.node_, pos.index_, pos.gen_, false);
            next.key_ = pos.key_;
            ++next;   // seated (and keyed) before the erase: its node stays valid
            erase(pos.node_->first);
            return next;
        }
        iterator erase(iterator pos) { return erase(const_iterator(pos)); }
        // the key at pos, writable for a store of an equal value (same hash, equality and order): a parallel region's
        // admission detaches a shared key string in place
        script_value& equal_key_storage(const_iterator pos) { pos.revalidate(); return const_cast<script_value&>(pos.node_->first); }
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
        for (const slot& sl : slots_) {
            if (sl.e && (sl.e->kv.second.release_flattens() || sl.e->kv.first.release_flattens())) {
                std::vector<script_value> work;
                hand_off_entries(work);
                detail::release_work(work);
                return;
            }
        }
        clear();
    }

} // namespace jai

#endif
