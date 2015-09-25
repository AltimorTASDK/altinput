#pragma once

#include "base_input.h"
#include <Windows.h>
#include <hidsdi.h>
#include <list>
#include <vector>
#include <memory>

class joystick : public base_input {
	float deadzone; // Deadzone for axis to register

	// TGM3 buttons
	unsigned short buttons_1p;
	unsigned short buttons_2p;

	// Directional buttons from the previous update
	unsigned short old_dir_buttons;

	// List containing which direction keys are pressed
	std::list<unsigned short> direction_keys;

	enum class axis {
		none,
		up_down,
		left_right
	};

	// Stored info for devices that are in use
	struct device_info {
		HANDLE nt_handle;
		HANDLE ri_handle;
		wchar_t name[128];

		int player;

		ULONG num_buttons;
		USAGE button_usage_page;
		USAGE lowest_button_id;
		std::unique_ptr<unsigned short[]> button_mask_map;
		
		USHORT num_value_caps;
		std::unique_ptr<struct _HIDP_VALUE_CAPS[]> value_caps;
		float deadzone;
		std::unique_ptr<axis[]> axis_map;

		~device_info()
		{
			CloseHandle(nt_handle);
		}
	};

	std::vector<std::unique_ptr<device_info>> devices;

	// Grab the info for a HID device
	void get_device_info(
		const config &cfg,
		const HANDLE nt_handle,
		const HANDLE ri_handle);

public:
	// Raw input usage
	std::vector<int> get_usage() override
	{
		return { { 4, 5 } };
	}

	// Load button and axis codes from config
	void init(const config &cfg) override;

	// Update buttons
	void update(const tagRAWINPUT *input) override;

	// Clear direction keys list too
	void clear_buttons() override
	{
		buttons_1p = 0;
		buttons_2p = 0;
		direction_keys.clear();
	}

	// buttons accessors
	unsigned short get_buttons_1p() const override
	{
		return buttons_1p;
	}

	unsigned short get_buttons_2p() const override
	{
		return buttons_2p;
	}
};