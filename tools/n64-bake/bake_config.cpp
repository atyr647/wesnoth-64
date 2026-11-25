/*
 * bake_config -- compile game configurations into the frozen WML image the
 * N64 build reads in place from ROM (src/n64/frozen/n64_frozen_format.hpp).
 *
 * Built on the host against an unmodified upstream Wesnoth build: the
 * preprocessor, the parser and the config class are the stock engine's, so
 * each root is exactly the config the game would load for those defines.
 *
 *   bake_config <game dir> <out.bin>
 *
 * Bakes the list of cores and the default core's WML tree for the title
 * screen, multiplayer, test and editor defines, and for every campaign at
 * every difficulty (the campaign's define, its extra defines and the
 * difficulty's define, as game_config_manager::load_game_config_for_game sets
 * them).
 */
#include "config.hpp"
#include "config_cache.hpp"
#include "filesystem.hpp"
#include "game_config.hpp"
#include "log.hpp"
#include "serialization/string_utils.hpp"
#include "tstring.hpp"
#include "utils/variant.hpp"

#include "../../src/n64/frozen/n64_frozen_format.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
std::string image(16, '\0'); // header filled in at the end
std::unordered_map<std::string, std::uint32_t> strings, nodes;
std::uint64_t total_nodes = 0, total_attrs = 0;

void put32(std::string& s, std::uint32_t v)
{
	const char b[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
	s.append(b, 4);
}

std::uint32_t add_string(const std::string& s)
{
	const auto it = strings.find(s);
	if(it != strings.end()) {
		return it->second;
	}
	const std::uint32_t off = image.size();
	put32(image, s.size());
	image += s;
	image.append((4 - image.size() % 4) % 4, '\0');
	strings.emplace(s, off);
	return off;
}

struct value_encoder
{
	std::uint32_t operator()(const utils::monostate&) const { return add_string(""); }
	std::uint32_t operator()(int i) const
	{
		if(i >= -(1 << 29) && i < (1 << 29)) {
			return (static_cast<std::uint32_t>(i) << 2) | n64_frozen::value_int;
		}
		return add_string(std::to_string(i));
	}
	std::uint32_t operator()(const t_string& s) const { return add_string(s.to_serialized()) | n64_frozen::value_tstring; }
	std::uint32_t operator()(const std::string& s) const { return add_string(s); }
	// booleans, large integers, doubles: stored as the string the parser
	// assigned, and assigned as a string again on the console
	template<typename T>
	std::uint32_t operator()(const T&) const { return add_string(text); }
	std::string text;
};

std::uint32_t add_node(const config& cfg)
{
	std::string n;
	std::uint32_t na = 0, nc = 0;
	std::string body;
	for(const auto& [key, value] : cfg.attribute_range()) {
		put32(body, add_string(key));
		put32(body, value.apply_visitor(value_encoder{value.str()}));
		++na;
	}
	for(const auto [tag, child] : cfg.all_children_view()) {
		const std::uint32_t c = add_node(child);
		put32(body, add_string(tag));
		put32(body, c);
		++nc;
	}
	put32(n, na);
	put32(n, nc);
	n += body;
	++total_nodes;
	total_attrs += na;
	const auto it = nodes.find(n);
	if(it != nodes.end()) {
		return it->second;
	}
	const std::uint32_t off = image.size();
	image += n;
	nodes.emplace(std::move(n), off);
	return off;
}
} // namespace

namespace
{
struct root { std::uint32_t key, node, hash; };
std::vector<root> roots;
std::map<std::string, bool> done;

/** The key the console derives for a file: its path below the game directory. */
std::string relative(std::string path)
{
	if(path.compare(0, game_config::path.size(), game_config::path) == 0) {
		path.erase(0, game_config::path.size());
	}
	std::string res;
	for(const char c : path) {
		if(c != '/' || (!res.empty() && res.back() != '/')) {
			res += c;
		}
	}
	while(!res.empty() && res.back() == '/') {
		res.pop_back();
	}
	return res;
}

config bake(const std::string& file, std::vector<std::string> defines)
{
	std::sort(defines.begin(), defines.end());
	defines.erase(std::unique(defines.begin(), defines.end()), defines.end());
	const std::string key = relative(file) + "|" + utils::join(defines);

	auto& cache = game_config::config_cache::instance();
	config cfg;
	{
		game_config::config_cache_transaction transaction;
		cache.clear_defines();
		for(const std::string& d : defines) {
			cache.add_define(d);
		}
		cfg = cache.get_config(file);
		for(const std::string& d : defines) {
			cache.remove_define(d);
		}
	}
	if(done.emplace(key, true).second) {
		const std::size_t before = image.size();
		root r;
		r.node = add_node(cfg);
		r.key = add_string(key);
		r.hash = n64_frozen::check_hash(cfg);
		roots.push_back(r);
		std::printf("%-64s +%6zu KiB  hash %08x\n", key.c_str(), (image.size() - before) >> 10, r.hash);
		std::fflush(stdout);
	}
	return cfg;
}
} // namespace

int main(int argc, char** argv)
{
	if(argc != 3) {
		std::cerr << "usage: bake_config <game dir> <out.bin>\n";
		return 2;
	}
	game_config::path = filesystem::normalize_path(argv[1], true, true);
	// An empty user data directory: no add-ons, no preferences.
	filesystem::set_user_data_dir(std::string(argv[2]) + ".userdata");
	lg::set_log_domain_severity("all", lg::err());
	game_config::config_cache::instance().set_use_cache(false);

	// What game_config_manager loads: the list of cores, then the default
	// core's WML tree, for each set of defines the game can be in.
	const std::string cores = game_config::path + "/data/cores.cfg";
	const std::string tree = filesystem::get_wml_location("/").value();

	std::vector<std::vector<std::string>> sets = {{"TITLE_SCREEN"}, {"MULTIPLAYER"}, {"TEST"}, {"EDITOR"}};
	const config title = bake(tree, sets[0]);
	for(const config& campaign : title.child_range("campaign")) {
		std::vector<std::string> base = utils::split(campaign["extra_defines"].str());
		if(!campaign["define"].empty()) {
			base.push_back(campaign["define"].str());
		}
		bool any = false;
		for(const config& difficulty : campaign.child_range("difficulty")) {
			std::vector<std::string> set = base;
			if(!difficulty["define"].empty()) {
				set.push_back(difficulty["define"].str());
			}
			sets.push_back(set);
			any = true;
		}
		if(!any) {
			sets.push_back(base);
		}
	}
	for(const auto& set : sets) {
		bake(cores, set);
		bake(tree, set);
	}

	const std::uint32_t table = image.size();
	for(const root& r : roots) {
		put32(image, r.key);
		put32(image, r.node);
		put32(image, r.hash);
	}
	std::string header;
	put32(header, n64_frozen::magic);
	put32(header, n64_frozen::version);
	put32(header, roots.size());
	put32(header, table);
	image.replace(0, 16, header);

	std::ofstream(argv[2], std::ios::binary).write(image.data(), image.size());
	std::printf("%zu roots: %llu nodes (%zu unique), %llu attributes, %zu unique strings; image %zu KiB\n",
		roots.size(), static_cast<unsigned long long>(total_nodes), nodes.size(),
		static_cast<unsigned long long>(total_attrs), strings.size(), image.size() >> 10);
	return 0;
}
