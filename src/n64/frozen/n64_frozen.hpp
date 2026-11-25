/*
 * n64_frozen.hpp -- the baked WML image linked into the ROM (see
 * n64_frozen_format.hpp for the layout and tools/n64-bake for the compiler).
 */
#pragma once

#include "n64_frozen_format.hpp"

#include <string_view>

namespace n64_frozen
{
/** Start of the image, or nullptr if the ROM was built without one. */
const unsigned char* image();

struct root_info
{
	const unsigned char* node = nullptr;     /**< the config */
	std::uint32_t hash = 0;                  /**< check hash from the bake tool */
	const unsigned char* terrain = nullptr;  /**< terrain rule list, or nullptr */
};

/** Looks up the root baked for @a key ("<file>|<defines>"). */
bool find_root(std::string_view key, root_info& info);

/**
 * The terrain rule list (see the format) that goes with the game config in
 * use: config_cache records it when it hands out a baked WML tree.
 */
void set_terrain_rules(const unsigned char* list);
const unsigned char* terrain_rules();

/** Terrain match @a id of the image's match table; match_count() of them. */
const unsigned char* terrain_match(std::uint32_t id);
std::uint32_t match_count();

/**
 * Testing (--n64-check-frozen): every baked config is read completely when it
 * is loaded and its hash compared with the one the bake tool computed from
 * the stock loader's result.
 */
extern bool check_roots;
} // namespace n64_frozen
