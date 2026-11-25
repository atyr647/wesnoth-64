/*
 * n64_frozen_format.hpp -- layout of the baked ("frozen") WML image.
 *
 * tools/n64-bake compiles every game configuration the console can load (one
 * per set of preprocessor defines: the title screen, each campaign at each
 * difficulty) into one read-only image, with the stock engine's own
 * preprocessor and parser. Identical subtrees and strings are stored once and
 * shared between all configurations. The image is linked into the ROM's paged
 * read-only data; the console's config class reads nodes in place and only
 * copies a node into RAM when it is first used (config::thaw_impl_).
 *
 * All fields are big-endian 32-bit words; every offset is from the start of
 * the image and a multiple of 4.
 *
 *   header   magic, version, number of roots, offset of the root table,
 *            number of terrain matches, offset of the match table
 *   root     offset of the key string, offset of the node, check hash,
 *            offset of the terrain rule list (0: none)
 *   string   length, bytes, zero padding to 4
 *   node     number of attributes, number of children,
 *            attributes[] { key string offset, value }   sorted by key
 *            children[]   { tag string offset, node offset }  in WML order
 *   value    low 2 bits: 0 string (offset), 1 integer (value >> 2, signed),
 *            2 translatable string in t_string serialized form (offset | 2)
 *
 * Terrain rules (what terrain_builder::parse_global_config builds from a
 * root's [terrain_graphics]: rotated, with the frames whose image exists, in
 * precedence order):
 *
 *   rule list  number of rules, offsets of the rules
 *   match      has_wildcard, is_empty, then three lists (terrain, mask,
 *              masked terrain) of { count, (base, overlay) pairs }; the match
 *              table holds the offset of each match, indexed by match id
 *   rule       precedence, number of constraints, match id of each
 *              constraint, probability, location x y, modulo x y, hash, then
 *              per constraint: x, y, no_draw, set_flag, no_flag, has_flag
 *              (each: count, string offsets), number of images, and per
 *              image: layer, basex, basey, global, center x y, is_water,
 *              number of variants; per variant: tods, has_flag (string
 *              lists), random_start in ms, number of animations; per
 *              animation: number of frames; per frame: duration in ms, file
 *              name string, modifications string
 *
 * A root's key is "<path below the game data directory>|<defines>", the
 * defines being the names of the non-builtin preprocessor defines, sorted and
 * joined with commas.
 */
#pragma once

#include <cstdint>

namespace n64_frozen
{
constexpr std::uint32_t magic = 0x57465A31; // "WFZ1"
constexpr std::uint32_t version = 2;
constexpr std::uint32_t root_entry_size = 16;

enum : std::uint32_t { value_string = 0, value_int = 1, value_tstring = 2, value_mask = 3 };

inline std::uint32_t rd32(const unsigned char* p)
{
	return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | std::uint32_t(p[3]);
}

/** FNV-1a, used for the check hash of a root (see n64_frozen::check_hash). */
inline std::uint32_t fnv(std::uint32_t h, const void* data, std::uint32_t n)
{
	const unsigned char* p = static_cast<const unsigned char*>(data);
	for(std::uint32_t i = 0; i < n; ++i) {
		h = (h ^ p[i]) * 16777619u;
	}
	return h;
}
inline std::uint32_t fnv32(std::uint32_t h, std::uint32_t v)
{
	const unsigned char b[4] = {static_cast<unsigned char>(v >> 24), static_cast<unsigned char>(v >> 16),
		static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v)};
	return fnv(h, b, 4);
}

/**
 * Hash of a config through its public interface (attributes in key order with
 * their string values, children in WML order). The bake tool stores it for
 * every root; the console can recompute it to prove that what it reads from
 * the image is what the stock loader produced.
 */
template<typename Config>
std::uint32_t check_hash(const Config& cfg, std::uint32_t h = 2166136261u)
{
	std::uint32_t na = 0, nc = 0;
	for(const auto& [key, value] : cfg.attribute_range()) {
		const auto s = value.str();
		h = fnv32(h, key.size());
		h = fnv(h, key.data(), key.size());
		h = fnv32(h, s.size());
		h = fnv(h, s.data(), s.size());
		++na;
	}
	h = fnv32(h, na);
	for(const auto [tag, child] : cfg.all_children_view()) {
		h = fnv32(h, tag.size());
		h = fnv(h, tag.data(), tag.size());
		h = check_hash(child, h);
		++nc;
	}
	return fnv32(h, nc);
}
} // namespace n64_frozen
