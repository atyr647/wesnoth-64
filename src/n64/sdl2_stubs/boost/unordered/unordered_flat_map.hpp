/* boost::unordered_flat_map for the N64 build (same interface subset as
   std::unordered_map; see unordered_map.hpp). */
#pragma once
#include <boost/unordered/unordered_map.hpp>
namespace boost {
template<class K, class V, class H = boost::hash<K>, class E = std::equal_to<K>,
         class A = std::allocator<std::pair<const K, V>>>
using unordered_flat_map = std::unordered_map<K, V, H, E, A>;
}
