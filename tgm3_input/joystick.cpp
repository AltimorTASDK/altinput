#define WIN32_LEAN_AND_MEAN
#include "joystick.h"
#include "config.h"
#include <memory>
#include <limits>
#include <codecvt>
#include <sstream>
#include <Windows.h>
#include <subauth.h>
#include <hidsdi.h>

/**
 * init - Initialize an input device from a config
 * @cfg:	Config object
 *
 * Load button/axis codes and joystick settings from config
 */
void joystick::init(const config &cfg)
{
	buttons_1p = 0;
	buttons_2p = 0;

	// Get required buffer size
	unsigned int num_devices;
	GetRawInputDeviceList(nullptr, &num_devices, sizeof(RAWINPUTDEVICELIST));

	auto device_list = std::make_unique<RAWINPUTDEVICELIST[]>(num_devices);
	GetRawInputDeviceList(device_list.get(), &num_devices, sizeof(RAWINPUTDEVICELIST));

	for (auto i = 0u; i < num_devices; i++) {
		if (device_list[i].dwType != RIM_TYPEHID)
			continue;

		// Get required buffer size
		auto device_name_len = 0u;
		GetRawInputDeviceInfo(
			device_list[i].hDevice,
			RIDI_DEVICENAME,
			nullptr,
			&device_name_len);

		auto device_name = std::make_unique<char[]>(device_name_len);
		GetRawInputDeviceInfo(
			device_list[i].hDevice,
			RIDI_DEVICENAME,
			device_name.get(),
			&device_name_len);

		const auto nt_handle = CreateFile(
			device_name.get(),
			0,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr);

		get_device_info(cfg, nt_handle, device_list[i].hDevice);
	}
}

/**
 * get_device_info - Grab the info for control devices
 * @cfg:	Config object
 * @nt_handle:	Handle to HID device
 * @ri_handle:	Handle to rawinput device
 *
 * Check for config info on a device based on the HID product string. Append
 * a number in the case of duplicates.
 */
void joystick::get_device_info(
	const config &cfg,
	const HANDLE nt_handle,
	const HANDLE ri_handle
) {
	auto new_device = std::make_unique<device_info>();

	wchar_t name[128];
	if (!HidD_GetProductString(nt_handle, name, sizeof(name)))
		return;

	// Append a number if this is a duplicate
	auto duplicate_num = 0;
	for (const auto &check_device : devices) {
		if (wcscmp(name, check_device->name) == 0)
			duplicate_num++;
	}

	std::stringstream prefix;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	prefix << "joystick." << converter.to_bytes(name);
	if (duplicate_num > 0)
		prefix << " " + (duplicate_num + 1);

	new_device->player = cfg.value_int(0, prefix.str() + ".player");

	// Make sure this is actually going to be used for a player
	if (new_device->player != 1 && new_device->player != 2)
		return;

	_HIDP_PREPARSED_DATA *dev_info;
	if (!HidD_GetPreparsedData(nt_handle, &dev_info))
		return;

	// Get device capabilities
	HIDP_CAPS caps;
	if (!NT_SUCCESS(HidP_GetCaps(dev_info, &caps))) {
		HidD_FreePreparsedData(dev_info);
		return;
	}

	// Get button capabilities
	auto num_button_caps = caps.NumberInputButtonCaps;
	auto button_caps = std::make_unique<HIDP_BUTTON_CAPS[]>(
		num_button_caps);

	if (!NT_SUCCESS(HidP_GetButtonCaps(
		HidP_Input,
		button_caps.get(),
		&num_button_caps,
		dev_info))
	) {
		HidD_FreePreparsedData(dev_info);
		return;
	}

	// Get value capabilities
	new_device->num_value_caps = caps.NumberInputValueCaps;
	new_device->value_caps = std::make_unique<HIDP_VALUE_CAPS[]>(
		new_device->num_value_caps);

	if (!NT_SUCCESS(HidP_GetValueCaps(
		HidP_Input,
		new_device->value_caps.get(),
		&new_device->num_value_caps,
		dev_info))
	) {
		HidD_FreePreparsedData(dev_info);
		return;
	}

	new_device->nt_handle = nt_handle;
	new_device->ri_handle = ri_handle;;
	wcscpy_s(new_device->name, name);
	new_device->num_buttons = (ULONG)(
		button_caps[0].Range.UsageMax -
		button_caps[0].Range.UsageMin + 1);
	new_device->button_usage_page = button_caps[0].UsagePage;
	new_device->lowest_button_id = button_caps[0].Range.UsageMin;

	new_device->deadzone  = cfg.value_float(.5F, prefix.str() + ".deadzone");

	// Axes
	const auto up_down    = cfg.value_int(-1, prefix.str() + ".up_down");
	const auto left_right = cfg.value_int(-1, prefix.str() + ".left_right");

	new_device->axis_map = std::make_unique<axis[]>(new_device->num_value_caps);
	for (auto i = 0; i < new_device->num_value_caps; i++)
		new_device->axis_map[i] = axis::none;

	const auto write_axis_checked = [&](const int idx, const axis axis)
	{
		if (idx >= 0 && idx < new_device->num_value_caps)
			new_device->axis_map[idx] = axis;
	};

	write_axis_checked(up_down, axis::up_down);
	write_axis_checked(left_right, axis::left_right);

	// Buttons
	const auto up         = cfg.value_int(-1, prefix.str() + ".up");
	const auto down       = cfg.value_int(-1, prefix.str() + ".down");
	const auto left       = cfg.value_int(-1, prefix.str() + ".left");
	const auto right      = cfg.value_int(-1, prefix.str() + ".right");

	const auto A          = cfg.value_int(1, prefix.str() + ".A");
	const auto B          = cfg.value_int(2, prefix.str() + ".B");
	const auto C          = cfg.value_int(3, prefix.str() + ".C");
	const auto D          = cfg.value_int(0, prefix.str() + ".D");

	const auto start      = cfg.value_int(7, prefix.str() + ".start");

	new_device->button_mask_map = std::make_unique<unsigned short[]>(
		new_device->num_buttons);

	for (auto i = 0; i < new_device->num_buttons; i++)
		new_device->button_mask_map[i] = 0;

	const auto write_button_checked = [&](const int idx, const int mask)
	{
		if (idx >= 0 && idx < new_device->num_buttons)
			new_device->button_mask_map[idx] = mask;
	};

	write_button_checked(up, mask_up);
	write_button_checked(down, mask_down);
	write_button_checked(left, mask_left);
	write_button_checked(right, mask_right);

	write_button_checked(A, mask_A);
	write_button_checked(B, mask_B);
	write_button_checked(C, mask_C);
	write_button_checked(D, mask_D);

	write_button_checked(start, mask_start);

	devices.push_back(std::move(new_device));

	HidD_FreePreparsedData(dev_info);
}

/**
 * update - Update the bits in buttons
 * @input:	Raw input data pointer
 *
 * Check device button status and update buttons accordingly, if it's an axis
 * change compare it against deadzone
 */
void joystick::update(const tagRAWINPUT *input)
{
	if (input->header.dwType != RIM_TYPEHID)
		return;

	// See if this a device that's registered
	const device_info *device = nullptr;
	for (const auto &check_device : devices) {
		if (check_device->ri_handle != input->header.hDevice)
			continue;

		device = check_device.get();
		break;
	}

	if (device == nullptr)
		return;

	auto *buttons = device->player == 1 ? &buttons_1p : &buttons_2p;

	// Completely reset buttons
	*buttons = 0;

	_HIDP_PREPARSED_DATA *dev_info;
	if (!HidD_GetPreparsedData(device->nt_handle, &dev_info))
		return;

	auto num_buttons = device->num_buttons;
	auto usages = std::make_unique<USAGE[]>(num_buttons);
	if (!NT_SUCCESS(HidP_GetUsages(
		HidP_Input,
		device->button_usage_page,
		0,
		usages.get(),
		&num_buttons,
		dev_info,
		(char*)(input->data.hid.bRawData),
		input->data.hid.dwSizeHid))
	) {
		HidD_FreePreparsedData(dev_info);
		return;
	}

	for (auto i = 0u; i < num_buttons; i++) {
		const auto id = usages[i] - device->lowest_button_id;
		*buttons |= device->button_mask_map[id];
	}

	// Get axis positions
	for (auto i = 0; i < device->num_value_caps; i++) {
		const auto &val_cap = device->value_caps[i];

		ULONG value;
		if (!HidP_GetUsageValue(
			HidP_Input,
			val_cap.UsagePage,
			0,
			val_cap.Range.UsageMin,
			&value,
			dev_info,
			(char*)(input->data.hid.bRawData),
			input->data.hid.dwSizeHid)
		)
			continue;

		if (val_cap.Range.UsageMin == 0x39) { // Hat switch
			const unsigned short hat_map[] = {
				0, mask_up, 0, mask_right, 0,
				mask_down, 0, mask_left, 0,
			};
			if (value <= 8)
				*buttons |= hat_map[value];
		} else {
			// Cast to signed and rescale to [-1.0, 1.0]
			auto position = (float)((signed)(value));
			position -= val_cap.LogicalMin;
			position /= (val_cap.LogicalMax - val_cap.LogicalMin);
			position = (position - .5F) * 2.F;

			if (fabs(position) < device->deadzone)
				continue;

			const auto axis = device->axis_map[i];
			if (axis == axis::up_down)
				*buttons |= position > 0 ? mask_down : mask_up;
			else if (axis == axis::left_right)
				*buttons |= position > 0 ? mask_right : mask_left;
		}
	}

	const auto dir_buttons = *buttons & (mask_up | mask_down | mask_left | mask_right);
	*buttons &= ~dir_buttons;

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
		*buttons |= direction_keys.front();

	old_dir_buttons = dir_buttons;

	HidD_FreePreparsedData(dev_info);
}