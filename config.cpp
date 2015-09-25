#include "config.h"
#include <regex>
#include <fstream>
#include <sstream>

// Used to track what's being done with tokens
struct parse_state {
	bool needs_key = true;
	std::vector<std::string> namespaces;
	std::string last_token;
};

/**
 * handle_token - Feed tokens into keyvalue map
 * @token:	Token string
 * @state:	Parser state
 *
 * Track namespaces, which tokens are keys and which are values and write them
 * to the keyvalue map prefixed by outer namespaces
 */
void config::handle_token(std::string token, parse_state *state)
{
	// Replace outer double quotes
	if (token[0] == '"') {
		token.replace(token.begin(), token.begin() + 1, "");
		token.replace(token.end() - 1, token.end(), "");
	}
	if (token == "{") {
		state->namespaces.push_back(state->last_token);
		state->needs_key = true;
	} else if (token == "}") {
		state->namespaces.pop_back();
		state->needs_key = true;
	} else if (state->needs_key = !state->needs_key) {
		std::string prefix;
		for(auto &i : state->namespaces)
			prefix += i + ".";

		kv_map[prefix + state->last_token] = token;
	}
	state->last_token = token;
}

/**
 * config::config - Read config file
 * @filename:	File path
 *
 * Use regexes to ignore comments and tokenize the file contents
 */
config::config(const std::string &filename)
{
	std::ifstream file(filename);
	std::ostringstream stream;
	stream << file.rdbuf();
	auto contents = stream.str();

	parse_state state;

	// Remove C++ style comments, both single and multi line
	std::regex comments("(?:/\\*(?:[^*]|(?:\\*+[^*/]))*\\*+/)|(?://.*)");
	contents = std::regex_replace(contents, comments, "");

	// Loop through tokens
	std::regex split("\\s+(?=(?:[^\\\"]*[\\\"][^\\\"]*[\\\"])*[^\\\"]*$)");
	std::for_each(
			std::sregex_token_iterator(
					contents.begin(),
					contents.end(),
					split,
					-1),
			std::sregex_token_iterator(),
			[this, &state](std::ssub_match token)
			{
				handle_token(token.str(), &state);
			});
}