#pragma once

#include <vector>

class config;
struct tagRAWINPUT;

class base_input {
protected:
	// Game button bitmasks
	static constexpr auto mask_up = 32;
	static constexpr auto mask_down = 16;
	static constexpr auto mask_left = 8;
	static constexpr auto mask_right = 4;

	static constexpr auto mask_A = 2;
	static constexpr auto mask_B = 1;
	static constexpr auto mask_C = 32768;
	static constexpr auto mask_D = 16384;
	static constexpr auto mask_start = 128;

public:
	// Usage for raw input device
	virtual std::vector<int> get_usage() = 0;

	// Called every time a WM_INPUT message is received
	virtual void update(const tagRAWINPUT *input) = 0;

	// Return TGM3 buttons
	virtual unsigned short get_buttons_1p() const = 0;
	virtual unsigned short get_buttons_2p() const = 0;

	// Must be used on alt tab
	virtual void clear_buttons() = 0;

	// Initialize from config
	virtual void init(const config &cfg) = 0;
};
