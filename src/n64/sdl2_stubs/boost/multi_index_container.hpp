/* N64 stub: minimal boost::multi_index_container */
#pragma once
#include <vector>
#include <functional>
#include <unordered_map>
#include <utility>

namespace boost { namespace multi_index {

// Tag types
struct chronological {};
struct by_unit {};
struct by_hex {};

// Index specifiers (stubs)
template<typename Tag = void> struct tag {};
template<typename... Args> struct random_access {};
template<typename... Args> struct hashed_non_unique {};
template<typename... Args> struct ordered_unique {};
template<typename... Args> struct ordered_non_unique {};
template<typename Tag, typename Extractor> struct tagged {};

template<typename Class, typename Type, Type (Class::*Func)() const>
struct const_mem_fun {
    using result_type = Type;
    Type operator()(const Class& obj) const { return (obj.*Func)(); }
};

template<typename Class, typename Type, Type Class::*Member>
struct member {
    using result_type = Type;
    Type operator()(const Class& obj) const { return obj.*Member; }
};

template<typename... Indices> struct indexed_by {};

// Minimal multi_index_container backed by a std::vector
template<typename Value, typename IndexSpecifier = indexed_by<>>
class multi_index_container {
public:
    using value_type = Value;
    using iterator = typename std::vector<Value>::iterator;
    using const_iterator = typename std::vector<Value>::const_iterator;
    using reverse_iterator = typename std::vector<Value>::reverse_iterator;
    using const_reverse_iterator = typename std::vector<Value>::const_reverse_iterator;
    using size_type = std::size_t;

    // Index access (all indices return the same underlying vector view)
    template<typename Tag>
    struct index {
        using type = multi_index_container;
    };

    multi_index_container() = default;

    iterator begin() { return data_.begin(); }
    iterator end() { return data_.end(); }
    const_iterator begin() const { return data_.begin(); }
    const_iterator end() const { return data_.end(); }
    const_iterator cbegin() const { return data_.cbegin(); }
    const_iterator cend() const { return data_.cend(); }
    reverse_iterator rbegin() { return data_.rbegin(); }
    reverse_iterator rend() { return data_.rend(); }
    const_reverse_iterator rbegin() const { return data_.rbegin(); }
    const_reverse_iterator rend() const { return data_.rend(); }
    const_reverse_iterator crbegin() const { return data_.crbegin(); }
    const_reverse_iterator crend() const { return data_.crend(); }

    size_type size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    std::pair<iterator,bool> insert(iterator pos, const Value& v) {
        auto it = data_.insert(pos, v);
        return {it, true};
    }
    iterator erase(iterator pos) { return data_.erase(pos); }
    iterator erase(iterator first, iterator last) { return data_.erase(first, last); }
    void clear() { data_.clear(); }

    void push_back(const Value& v) { data_.push_back(v); }
    void push_front(const Value& v) { data_.insert(data_.begin(), v); }

    Value& front() { return data_.front(); }
    const Value& front() const { return data_.front(); }
    Value& back() { return data_.back(); }
    const Value& back() const { return data_.back(); }

    // get<Tag>() — returns reference to self (all "indices" share the same data)
    template<typename Tag>
    multi_index_container& get() { return *this; }
    template<typename Tag>
    const multi_index_container& get() const { return *this; }

    // project<Tag>() — identity projection (single vector-backed index)
    template<typename Tag>
    iterator project(iterator it) { return it; }
    template<typename Tag>
    const_iterator project(const_iterator it) const { return it; }

    // For hashed index: find / equal_range stubs
    template<typename Key>
    iterator find(const Key&) { return end(); }
    template<typename Key>
    const_iterator find(const Key&) const { return end(); }

    template<typename Key>
    std::pair<iterator, iterator> equal_range(const Key&) {
        return {end(), end()};
    }
    template<typename Key>
    std::pair<const_iterator, const_iterator> equal_range(const Key&) const {
        return {end(), end()};
    }

    // Count matching a key (stub: always returns 0 since we don't index)
    template<typename Key>
    size_type count(const Key&) const { return 0; }

    // Modify (for multi_index modify semantics)
    template<typename Modifier>
    bool modify(iterator it, Modifier mod) {
        if (it == end()) return false;
        mod(*it);
        return true;
    }

    template<typename Modifier>
    bool modify(const_iterator cit, Modifier mod) {
        if (cit == data_.cend()) return false;
        auto dist = std::distance(data_.cbegin(), cit);
        auto it = data_.begin() + dist;
        mod(*it);
        return true;
    }

    // Replace
    bool replace(iterator it, const Value& v) {
        if (it == end()) return false;
        *it = v;
        return true;
    }

    // Relocate (move element to a new position)
    void relocate(iterator to, iterator from) {
        if (from == to) return;
        Value v = std::move(*from);
        data_.erase(from);
        data_.insert(to, std::move(v));
    }

private:
    std::vector<Value> data_;
};

// Free function get<> for multi_index_container
template<typename Tag, typename Value, typename IS>
auto& get(multi_index_container<Value, IS>& c) { return c.template get<Tag>(); }
template<typename Tag, typename Value, typename IS>
const auto& get(const multi_index_container<Value, IS>& c) { return c.template get<Tag>(); }

}} // namespace boost::multi_index
