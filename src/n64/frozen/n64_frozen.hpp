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

/**
 * The root node baked for @a key ("<file>|<defines>"), or nullptr.
 * @a hash receives the check hash the bake tool computed for it.
 */
const unsigned char* find_root(std::string_view key, std::uint32_t* hash = nullptr);

/**
 * Testing (--n64-check-frozen): every baked config is read completely when it
 * is loaded and its hash compared with the one the bake tool computed from
 * the stock loader's result.
 */
extern bool check_roots;
} // namespace n64_frozen
