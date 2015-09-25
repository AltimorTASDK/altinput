#define WIN32_LEAN_AND_MEAN
#include "keyboard.h"
#include "../config.h"
#include <Windows.h>

/**
 * init - Initialize an input device from a config
 * @cfg:	Config object
 *
 * Load all the keycodes from the config
 */
void keyboard::init(const config &cfg)
{
	const auto up    = cfg.value_int('W', "keyboard.up");
	const auto down  = cfg.value_int('S', "keyboard.down");
	const auto left  = cfg.value_int('A', "keyboard.left");
	const auto right = cfg.value_int('D', "keyboard.right");

	const auto A     = cfg.value_int('H', "keyboard.A");
	const auto B     = cfg.value_int('J', "keyboard.B");
	const auto C     = cfg.value_int('K', "keyboard.C");
	const auto D     = cfg.value_int('L', "keyboard.D");

	const auto start = cfg.value_int(VK_RETURN, "keyboard.start");

	for (auto &association : key_mask_map)
		association = 0;

	key_mask_map[up]    = mask_up;
	key_mask_map[down]  = mask_down;
	key_mask_map[left]  = mask_left;
	key_mask_map[right] = mask_right;

	key_mask_map[A]     = mask_A;
	key_mask_map[B]     = mask_B;
	key_mask_map[C]     = mask_C;
	key_mask_map[D]     = mask_D;

	key_mask_map[start] = mask_start;

	buttons = 0;
}

/**
 * update - Update the bits in buttons
 * @input:	Raw input data pointer
 *
 * Make the most recently pressed directional key take priority
 */
void keyboard::update(const tagRAWINPUT *input)
{
	if (input->header.dwType != RIM_TYPEKEYBOARD)
		return;

	const auto vkey = input->data.keyboard.VKey % 0xFF;
	const auto down = (input->data.keyboard.Flags & RI_KEY_BREAK) == 0;
	const auto mask = key_mask_map[vkey];

	buttons = down ? (buttons | mask) : (buttons & ~mask);

	const auto dir_buttons = buttons & (mask_up | mask_down | mask_left | mask_right);
	buttons &= ~dir_buttons;

	const auto changed = dir_buttons ^ old_dir_buttons;
	const auto pressed = changed & dir_buttons;
	const auto released = changed & old_dir_buttons;

	for (auto i = 0; i < std::numeric_limits<decltype(dir_buttons)>::digits; i++) {
		const auto mask = 1 << i;
		if (pressed & mask)
			direction_keys.push_front(mask);
		else if (released & mask)
			direction_keys.remove(mask);
	}

	if (direction_keys.size() != 0)
		buttons |= direction_keys.front();

	old_dir_buttons = dir_buttons;

	return;
}