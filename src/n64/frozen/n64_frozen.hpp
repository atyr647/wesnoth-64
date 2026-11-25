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
} // namespace n64_frozen
