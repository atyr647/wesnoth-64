/*
 * n64_frozen.cpp -- access to the baked WML image (n64_frozen.hpp).
 */
#include "n64_frozen.hpp"

#include <cstring>

extern "C" {
// src/n64/frozen/n64_frozen_image.S; absent (null) without an image.
extern const unsigned char n64_frozen_image_start[] __attribute__((weak));
extern const unsigned char n64_frozen_image_end[] __attribute__((weak));
}

namespace n64_frozen
{
const unsigned char* image()
{
	static const unsigned char* const start = [] () -> const unsigned char* {
		const unsigned char* p = n64_frozen_image_start;
		if(!p || n64_frozen_image_end - p < 16 || rd32(p) != magic || rd32(p + 4) != version) {
			return nullptr;
		}
		return p;
	}();
	return start;
}

const unsigned char* find_root(std::string_view key, std::uint32_t* hash)
{
	const unsigned char* const img = image();
	if(!img) {
		return nullptr;
	}
	const std::uint32_t count = rd32(img + 8);
	const unsigned char* entry = img + rd32(img + 12);
	for(std::uint32_t i = 0; i < count; ++i, entry += 12) {
		const unsigned char* name = img + rd32(entry);
		if(rd32(name) == key.size() && std::memcmp(name + 4, key.data(), key.size()) == 0) {
			if(hash) {
				*hash = rd32(entry + 8);
			}
			return img + rd32(entry + 4);
		}
	}
	return nullptr;
}
} // namespace n64_frozen
