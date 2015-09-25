#pragma once

#include <string>
#include <map>
#include <regex>

/*
 * Reads config files in the following format:
 * 
 * namespace
 * {
 *	key1 value
 *
 *	/* C style comments are recognized */
/*	// C++ single line comments as well
 *	namespace2
 * 	{
 * 		key2 "value with spaces"
 * 	}
 * }
 * 
 * You can then access a key like "namespace.namespace2.key2".
 */
class config {
	std::map<std::string, std::string> kv_map;

	// Handle a single token
	void config::handle_token(std::string token, struct parse_state *state);

public:
	// Split the file into keyvalues
	explicit config(const std::string &filename);

	/*
	 * These functions get a value given a key name
	 * TODO: Add error checking to prevent typo annoyance
	 */

	std::string value_str(const std::string &def, const std::string &key) const
	{
		try {
			return kv_map.at(key);
		} catch(...) {
			return def;
		}
	}

	int value_int(const int def, const std::string &key) const
	{
		try {
			return std::stoi(kv_map.at(key), nullptr, 0);
		} catch(...) {
			return def;
		}
	}

	float value_float(const float def, const std::string &key) const
	{
		try {
			return std::stof(kv_map.at(key));
		} catch(...) {
			return def;
		}
	}

	bool value_bool(const bool def, const std::string &key) const
	{
		try {
			return kv_map.at(key) == "True" || kv_map.at(key) == "true";
		} catch(...) {
			return def;
		}
	}
};
