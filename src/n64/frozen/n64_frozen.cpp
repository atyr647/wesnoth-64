/*
 * n64_frozen.cpp -- access to the baked WML image (n64_frozen.hpp).
 */
#include "n64_frozen.hpp"

#include "filesystem.hpp"

#include <cstring>

extern "C" {
// src/n64/frozen/n64_frozen_image.S; absent (null) without an image.
extern const unsigned char n64_frozen_image_start[] __attribute__((weak));
extern const unsigned char n64_frozen_image_end[] __attribute__((weak));
}

namespace n64_frozen
{
bool check_roots = false;

const unsigned char* image()
{
	static const unsigned char* const start = [] () -> const unsigned char* {
		const unsigned char* p = n64_frozen_image_start;
		if(!p || n64_frozen_image_end - p < 24 || rd32(p) != magic || rd32(p + 4) != version) {
			return nullptr;
		}
		return p;
	}();
	return start;
}

bool find_root(std::string_view key, root_info& info)
{
	const unsigned char* const img = image();
	if(!img) {
		return false;
	}
	const std::uint32_t count = rd32(img + 8);
	const unsigned char* entry = img + rd32(img + 12);
	for(std::uint32_t i = 0; i < count; ++i, entry += root_entry_size) {
		const unsigned char* name = img + rd32(entry);
		if(rd32(name) == key.size() && std::memcmp(name + 4, key.data(), key.size()) == 0) {
			info.node = img + rd32(entry + 4);
			info.hash = rd32(entry + 8);
			info.terrain = rd32(entry + 12) ? img + rd32(entry + 12) : nullptr;
			return true;
		}
	}
	return false;
}

const unsigned char* find_file(const std::string& path)
{
	if(!image()) {
		return nullptr;
	}
	// key: "file:" and the path below the game directory
	std::string key = "file:";
	std::size_t i = path.compare(0, game_config::path.size(), game_config::path) == 0 ? game_config::path.size() : 0;
	for(; i < path.size(); ++i) {
		if(path[i] != '/' || (key.size() > 5 && key.back() != '/')) {
			key += path[i];
		}
	}
	key += '|';
	root_info info;
	return find_root(key, info) ? info.node : nullptr;
}

namespace
{
const unsigned char* current_terrain = nullptr;
}

void set_terrain_rules(const unsigned char* list) { current_terrain = list; }
const unsigned char* terrain_rules() { return current_terrain; }

std::uint32_t match_count()
{
	return image() ? rd32(image() + 16) : 0;
}

const unsigned char* terrain_match(std::uint32_t id)
{
	const unsigned char* const img = image();
	return img + rd32(img + rd32(img + 20) + id * 4);
}
} // namespace n64_frozen
