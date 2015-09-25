// i dont tas okay
#define WIN32_LEAN_AND_MEAN
#include "../config.h"
#include "demo.h"
#include "practice.h"
#include "keyboard.h"
#include "joystick.h"
#include <memory>
#include <Windows.h>
#include <detours.h>
#include <intrin.h>

keyboard keyboard_device;
joystick joystick_device;
base_input *devices[] = {
	&keyboard_device,
	&joystick_device
};

const config cfg("tgm3.cfg");

using window_proc_t = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
static window_proc_t orig_window_proc;
/**
 * hook_window_proc - Window proc hook for raw input
 * @wnd:	Window handle
 * @msg:	Window message type
 * @wparam:	Additional message info
 * @lparam:	Additional message info
 *
 * Initialize and register raw input device on the first WM_PAINT. If msg is
 * WM_INPUT, grab the raw input data and pass it to the input device handlers.
 */
static LRESULT hook_window_proc(
	HWND wnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam
) {
	static auto once = false;
	if (msg == WM_PAINT && !once) {
		for (auto &device : devices) {
			for (auto &usage : device->get_usage()) {
				RAWINPUTDEVICE rid;
				rid.usUsagePage = 1;
				rid.usUsage = usage;
				rid.dwFlags = 0;
				rid.hwndTarget = wnd;
				RegisterRawInputDevices(&rid, 1, sizeof(rid));
			}
		}
		once = true;
	} else if (msg == WM_KILLFOCUS) {
		// Make sure buttons don't get stuck
		for (auto &device : devices)
			device->clear_buttons();
	}
	
	if (msg != WM_INPUT)
		return orig_window_proc(wnd, msg, wparam, lparam);

	// Get required buffer size
	unsigned int buf_size;
	GetRawInputData(
		(HRAWINPUT)(lparam),
		RID_INPUT,
		nullptr,
		&buf_size,
		sizeof(RAWINPUTHEADER));

	// Never access input_buf directly or you'll violate strict aliasing
	auto input_buf = std::make_unique<char[]>(buf_size);
	auto *input = (RAWINPUT*)(input_buf.get());

	GetRawInputData(
		(HRAWINPUT)(lparam),
		RID_INPUT, 
		input,
		&buf_size,
		sizeof(RAWINPUTHEADER));

	for (auto &device : devices)
		device->update(input);

	return 0;
}

using get_jvs_data_t = char*(*)(int);
static get_jvs_data_t orig_get_jvs_data;
/**
 * hook_get_jvs_data - TGM3 input hook
 * @unknown:	Always 1
 *
 * Pass the data acquired from raw input to TGM3.
 */
static char *hook_get_jvs_data(const int unknown)
{
	auto *data = orig_get_jvs_data(unknown);
	auto *buttons_1p = (unsigned short*)(data + 0x184);
	auto *buttons_2p = (unsigned short*)(data + 0x186);

	*buttons_1p = 0;
	*buttons_2p = 0;
	for (const auto &device : devices) {
		*buttons_1p |= device->get_buttons_1p();
		*buttons_2p |= device->get_buttons_2p();
	}

	return data;
}

/**
 * hook_set_play_mode - Hook for function that changes play mode
 *
 * Set free play mode
 */
static void hook_set_play_mode()
{
	*(int*)(0x6418EC) = 3;
}

/**
 * hook_set_sprite_scale - Sprite scale initialization hook
 * @sprite:	Pointer to sprite data
 * @scale:	Scale to set
 *
 * Rescale sprites for different resolutions because Arika used GL_POINTS
 */
void hook_set_sprite_scale(char *sprite, const float scale)
{
	const auto res_y = *(int*)(0x40D160);
	*(float*)(sprite + 0x22C) = scale * .5F * ((float)(res_y) / 480.F);
}

BOOL CALLBACK monitor_enum(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM param)
{
	((std::vector<RECT>*)(param))->push_back(*rect);
	return TRUE;
}

using position_window_t = void(*)(int);
position_window_t orig_position_window;
/**
 * hook_position_window - Position the window to a different monitor
 * @fullscreen: Whether or not to go into borderless mode
 *
 * Use EnumDisplayMonitors to get a rect for each monitor and sort by X offset,
 * then move the window to the monitor index specified in the config
 **/
void hook_position_window(const int fullscreen)
{
	orig_position_window(cfg.value_bool(true, "patches.fullscreen"));

	std::vector<RECT> rects;
	EnumDisplayMonitors(nullptr, nullptr, monitor_enum, (LPARAM)(&rects));

	std::sort(rects.begin(), rects.end(), [](const RECT &r1, const RECT &r2)
	{
		return r1.left < r2.left;
	});

	const auto idx = cfg.value_int(0, "patches.monitor");
	if (idx >= rects.size())
		return;

	const auto window = *(HWND*)(0x6415D4);
	MoveWindow(window, rects[idx].left, rects[idx].top, 640, 480, FALSE);
}


/**
 * hook_WinMain - Custom WinMain
 * @inst:	Current instance of application
 * @prev_inst:	Always null
 * @cmdline:	Console command line
 * @show_cmd:	How the window is to be shown
 *
 * Set up hooks and demo playback
 */
__declspec(dllexport) // Intel C++ was optimizing this function away for no reason
int CALLBACK hook_WinMain(
	const HINSTANCE inst,
	const HINSTANCE prev_inst,
	const char *cmdline,
	int show_cmd
) {
	for (auto &device : devices)
		device->init(cfg);

	DetourFunction((BYTE*)(0x452CE0), (BYTE*)(hook_set_play_mode));
	DetourFunction((BYTE*)(0x434E00), (BYTE*)(hook_set_sprite_scale));
	orig_position_window = (position_window_t)(DetourFunction(
		(BYTE*)(0x450E50), (BYTE*)(hook_position_window)));
	orig_window_proc = (window_proc_t)(DetourFunction(
		(BYTE*)(0x451400), (BYTE*)(hook_window_proc)));
	orig_get_jvs_data = (get_jvs_data_t)(DetourFunction(
		(BYTE*)(0x45D490), (BYTE*)(hook_get_jvs_data)));

	// Demo playback
	if (cmdline != nullptr && *cmdline != '\0')
		setup_playback(cmdline);
	else
		setup_recording();

	init_practice(cfg);

	// Call the original startup function
	((void(*)())(0x42ED30))();

	return 0;
}

/**
 * DllMain - DLL entry point
 * @inst:	Handle to this module
 * @reason:	Reason for entry point call
 *
 * Create the initialization thread
 */
BOOL WINAPI DllMain(
	const HINSTANCE inst,
	const DWORD reason,
	const void *reserved
) {
	if (reason != DLL_PROCESS_ATTACH)
		return false;

	// Patch in JMP rel32 to custom WinMain
	constexpr uintptr_t WinMain = 0x42ED40;

	DWORD old_protect;
	VirtualProtect((void*)(WinMain), 5, PAGE_READWRITE, &old_protect);
	*(char*)(WinMain) = '\xE9'; // opcode
	*(uintptr_t*)(WinMain + 1) = (uintptr_t)(hook_WinMain) - WinMain - 5;
	VirtualProtect((void*)(WinMain), 5, old_protect, &old_protect);

	return true;
}