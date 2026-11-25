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
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// The terrain rules are read out of the stock terrain_builder after it has
// parsed them; its rule structures are private.
#define private public
#define protected public
#include "animated.hpp"
#include "terrain/builder.hpp"
#undef private
#undef protected

#include "config.hpp"
#include "config_cache.hpp"
#include "filesystem.hpp"
#include "game_config_view.hpp"
#include "picture.hpp"
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
std::string out(24, '\0'); // header filled in at the end
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
	const std::uint32_t off = out.size();
	put32(out, s.size());
	out += s;
	out.append((4 - out.size() % 4) % 4, '\0');
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
	const std::uint32_t off = out.size();
	out += n;
	nodes.emplace(std::move(n), off);
	return off;
}
} // namespace

namespace
{
// ---- terrain rules -------------------------------------------------------
std::unordered_map<std::string, std::uint32_t> blobs;      // rules, rule lists
std::vector<std::uint32_t> match_offsets;                  // id -> offset
std::unordered_map<std::string, std::uint32_t> match_ids;
std::uint64_t total_rules = 0;

std::uint32_t add_blob(const std::string& b)
{
	const auto it = blobs.find(b);
	if(it != blobs.end()) {
		return it->second;
	}
	const std::uint32_t off = out.size();
	out += b;
	blobs.emplace(b, off);
	return off;
}

std::uint32_t add_match(const t_translation::ter_match& m)
{
	std::string b;
	put32(b, m.has_wildcard);
	put32(b, m.is_empty);
	for(const t_translation::ter_list* l : {&m.terrain, &m.mask, &m.masked_terrain}) {
		put32(b, l->size());
		for(const t_translation::terrain_code& c : *l) {
			put32(b, c.base);
			put32(b, c.overlay);
		}
	}
	const auto it = match_ids.find(b);
	if(it != match_ids.end()) {
		return it->second;
	}
	match_offsets.push_back(add_blob(b));
	match_ids.emplace(std::move(b), match_offsets.size() - 1);
	return match_offsets.size() - 1;
}

template<typename List>
void put_strings(std::string& b, const List& l)
{
	put32(b, l.size());
	for(const std::string& s : l) {
		put32(b, add_string(s));
	}
}

std::uint32_t add_rule(const terrain_builder::building_rule& rule)
{
	std::string b;
	put32(b, rule.precedence);
	put32(b, rule.constraints.size());
	for(const auto& c : rule.constraints) {
		put32(b, add_match(c.terrain_types_match));
	}
	put32(b, rule.probability);
	put32(b, rule.location_constraints.x);
	put32(b, rule.location_constraints.y);
	put32(b, rule.modulo_constraints.x);
	put32(b, rule.modulo_constraints.y);
	put32(b, rule.get_hash());
	for(const auto& c : rule.constraints) {
		put32(b, c.loc.x);
		put32(b, c.loc.y);
		put32(b, c.no_draw);
		put_strings(b, c.set_flag);
		put_strings(b, c.no_flag);
		put_strings(b, c.has_flag);
		put32(b, c.images.size());
		for(const auto& ri : c.images) {
			put32(b, ri.layer);
			put32(b, ri.basex);
			put32(b, ri.basey);
			put32(b, ri.global_image);
			put32(b, ri.center_x);
			put32(b, ri.center_y);
			put32(b, ri.is_water);
			put32(b, ri.variants.size());
			for(const auto& v : ri.variants) {
				put_strings(b, v.tods);
				put_strings(b, v.has_flag);
				put32(b, v.random_start.count());
				put32(b, v.images.size());
				for(const auto& anim : v.images) {
					put32(b, anim.frames_.size());
					for(const auto& frame : anim.frames_) {
						put32(b, frame.duration_.count());
						put32(b, add_string(frame.value_.get_filename()));
						put32(b, add_string(frame.value_.get_modifications()));
					}
				}
			}
		}
	}
	return add_blob(b);
}

/** The global terrain rules of @a cfg as the stock builder parses them. */
std::uint32_t add_terrain_rules(const config& cfg)
{
	const game_config_view view = game_config_view::wrap(cfg);
	filesystem::binary_paths_manager paths;
	paths.set_paths(view);
	image::flush_cache();   // image existence depends on the binary paths

	terrain_builder::rules_cfg_ = nullptr;
	terrain_builder::building_rules_.clear();
	terrain_builder builder(config(), nullptr, "", false);
	builder.parse_global_config(view);

	std::string list;
	put32(list, terrain_builder::building_rules_.size());
	for(const auto& rule : terrain_builder::building_rules_) {   // in precedence order
		put32(list, add_rule(rule));
		++total_rules;
	}
	terrain_builder::building_rules_.clear();
	return add_blob(list);
}
} // namespace

namespace
{
struct root { std::uint32_t key, node, hash, terrain; };
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
		const std::size_t before = out.size();
		root r;
		r.node = add_node(cfg);
		r.key = add_string(key);
		r.hash = n64_frozen::check_hash(cfg);
		const std::size_t mid = out.size();
		r.terrain = cfg.has_child("terrain_graphics") ? add_terrain_rules(cfg) : 0;
		roots.push_back(r);
		std::printf("%-58s +%6zu KiB  terrain +%5zu KiB  hash %08x\n", key.c_str(), (mid - before) >> 10,
			(out.size() - mid) >> 10, r.hash);
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

	const std::uint32_t table = out.size();
	for(const root& r : roots) {
		put32(out, r.key);
		put32(out, r.node);
		put32(out, r.hash);
		put32(out, r.terrain);
	}
	const std::uint32_t matches = out.size();
	for(const std::uint32_t off : match_offsets) {
		put32(out, off);
	}
	std::string header;
	put32(header, n64_frozen::magic);
	put32(header, n64_frozen::version);
	put32(header, roots.size());
	put32(header, table);
	put32(header, match_offsets.size());
	put32(header, matches);
	out.replace(0, 24, header);

	std::ofstream(argv[2], std::ios::binary).write(out.data(), out.size());
	std::printf("%zu roots: %llu nodes (%zu unique), %llu attributes, %zu unique strings; image %zu KiB\n",
		roots.size(), static_cast<unsigned long long>(total_nodes), nodes.size(),
		static_cast<unsigned long long>(total_attrs), strings.size(), out.size() >> 10);
	std::printf("terrain: %llu rules (%zu distinct blobs), %zu distinct terrain matches\n",
		static_cast<unsigned long long>(total_rules), blobs.size(), match_offsets.size());
	return 0;
}
