/* boost::unordered_map for the N64 build: std::unordered_map with boost::hash
   semantics (hash_value() found by ADL, else std::hash). */
#pragma once
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <string_view>
#include <utility>

namespace boost {
namespace n64_detail {
template<class T, class = void> struct has_hash_value : std::false_type {};
template<class T>
struct has_hash_value<T, std::void_t<decltype(hash_value(std::declval<const T&>()))>> : std::true_type {};
} // namespace n64_detail

#ifndef N64_BOOST_HASH_DEFINED
#define N64_BOOST_HASH_DEFINED
template<class T, class = void> struct n64_is_range : std::false_type {};
template<class T>
struct n64_is_range<T, std::void_t<decltype(std::declval<const T&>().begin()),
                                   decltype(std::declval<const T&>().end())>> : std::true_type {};

template<class T>
struct hash {
    std::size_t operator()(const T& v) const {
        if constexpr (n64_detail::has_hash_value<T>::value) {
            return hash_value(v);
        } else if constexpr (n64_is_range<T>::value && !std::is_convertible_v<T, std::string_view>) {
            // boost::hash_range: seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2)
            std::size_t seed = 0;
            for (const auto& e : v) {
                seed ^= hash<std::decay_t<decltype(e)>>{}(e) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
            }
            return seed;
        } else {
            return std::hash<T>{}(v);
        }
    }
};
#endif

template<class K, class V, class H = boost::hash<K>, class E = std::equal_to<K>,
         class A = std::allocator<std::pair<const K, V>>>
using unordered_map = std::unordered_map<K, V, H, E, A>;
} // namespace boost
