/*
	Copyright (C) 2026
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace utils
{
/**
 * An immutable string stored once in a process-wide pool; copies share it.
 *
 * Used on memory-constrained builds (the Nintendo 64 port) for strings that
 * are copied many times over, such as image locators' file names: each
 * terrain graphics rule, rotation and animation frame otherwise holds its own
 * heap copy. Interned strings are never freed. Equal contents always give
 * the same pooled object, so equality is a pointer comparison.
 */
class interned_string
{
public:
	interned_string() : str_(&empty_string()) {}
	interned_string(const std::string& s) : str_(&intern(s)) {}
	interned_string(std::string&& s) : str_(&intern(std::move(s))) {}
	interned_string(const char* s) : str_(&intern(std::string(s))) {}

	interned_string& operator=(const std::string& s) { str_ = &intern(s); return *this; }
	interned_string& operator=(std::string&& s) { str_ = &intern(std::move(s)); return *this; }

	const std::string& str() const { return *str_; }
	operator const std::string&() const { return *str_; }

	bool empty() const { return str_->empty(); }
	std::size_t size() const { return str_->size(); }

	friend bool operator==(const interned_string& a, const interned_string& b) { return a.str_ == b.str_; }
	friend bool operator!=(const interned_string& a, const interned_string& b) { return a.str_ != b.str_; }
	friend bool operator<(const interned_string& a, const interned_string& b) { return *a.str_ < *b.str_; }

private:
	static const std::string& empty_string()
	{
		static const std::string& e = intern(std::string());
		return e;
	}

	template<typename S>
	static const std::string& intern(S&& s)
	{
		static auto* pool = new std::unordered_set<std::string>();   // never destroyed
		return *pool->emplace(std::forward<S>(s)).first;
	}

	const std::string* str_;
};

} // namespace utils
