#define WIN32_LEAN_AND_MEAN
#include "../config.h"
#include <Windows.h>
#include <sstream>
#include <iostream>

/**
 * apply_patches - Apply memory patches
 * @process:	Handle to TGM3 process
 *
 * Patch resolution, aspect ratio and sramdata path with VirtualProtectEx and
 * WriteProcessMemory
 */
static void apply_patches(const HANDLE process)
{
	const auto patch_extern = [process](
			const uintptr_t addr,
			const void *buf,
			const size_t size)
	{
		DWORD old_protect;
		VirtualProtectEx(
				process,
				(void*)(addr),
				size,
				PAGE_EXECUTE_READWRITE,
				&old_protect);

		WriteProcessMemory(process, (void*)(addr), buf, size, nullptr);
		VirtualProtectEx(
				process,
				(void*)(addr),
				size,
				old_protect,
				&old_protect);
	};


	const config cfg("tgm3.cfg");
	const auto resolution_x = cfg.value_int(640, "patches.resolution_x");
	const auto resolution_y = cfg.value_int(480, "patches.resolution_y");

	const auto fullscreen = (char)(cfg.value_bool(true, "patches.fullscreen"));
	patch_extern(0x44DCC9, &fullscreen, sizeof(fullscreen));
	patch_extern(0x40D160, &resolution_y, sizeof(resolution_y));
	patch_extern(0x40D165, &resolution_x, sizeof(resolution_x));

	patch_extern(0x40D19A, &resolution_y, sizeof(resolution_y));
	patch_extern(0x40D19F, &resolution_x, sizeof(resolution_x));

	patch_extern(0x41F154, &resolution_x, sizeof(resolution_x));
	patch_extern(0x41F163, &resolution_x, sizeof(resolution_x));
	patch_extern(0x41F176, &resolution_y, sizeof(resolution_y));
	patch_extern(0x41F181, &resolution_y, sizeof(resolution_y));

	patch_extern(0x44DCA6, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44DCAB, &resolution_x, sizeof(resolution_x));
	patch_extern(0x44DCB0, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44DCB5, &resolution_x, sizeof(resolution_x));

	patch_extern(0x44DD2D, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44DD32, &resolution_x, sizeof(resolution_x));
	patch_extern(0x44DD4D, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44DD52, &resolution_x, sizeof(resolution_x));

	const auto aspect_ratio = (float)(resolution_x) / (float)(resolution_y);
	patch_extern(0x44DD6B, &aspect_ratio, sizeof(aspect_ratio));

	patch_extern(0x44E126, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44E12B, &resolution_x, sizeof(resolution_x));

	patch_extern(0x44E198, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44E19D, &resolution_x, sizeof(resolution_x));

	patch_extern(0x44E349, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44E34E, &resolution_x, sizeof(resolution_x));

	patch_extern(0x44E429, &resolution_y, sizeof(resolution_y));
	patch_extern(0x44E42E, &resolution_x, sizeof(resolution_x));

	patch_extern(0x450E5B, &resolution_y, sizeof(resolution_y));
	patch_extern(0x450E60, &resolution_x, sizeof(resolution_x));

	patch_extern(0x450E90, &resolution_y, sizeof(resolution_y));
	patch_extern(0x450E95, &resolution_x, sizeof(resolution_x));

	patch_extern(0x450ED7, &resolution_y, sizeof(resolution_y));
	patch_extern(0x450EDC, &resolution_x, sizeof(resolution_x));

	// Replace the call to main with an infinite loop so the WinMain hook
	// doesn't fail if the game's WinMain gets executed first for some
	// reason
	const auto *infinite_loop = "\xEB\xFE"; // JMP in place
	patch_extern(0x42ED40, infinite_loop, 2);

	// This was broken
	/*// rotozoom background scale fix based on diagonal/width ratio change
	const auto old_diag_ratio = sqrtf(1.F + (4.F / 3.F) * (4.F / 3.F));
	const auto new_diag_ratio = sqrtf(1.F + aspect_ratio * aspect_ratio);
	// default scale is 1.04
	auto new_zoom = 1.04F * (new_diag_ratio / old_diag_ratio);
	patch_extern(0x4686E4, &new_zoom, sizeof(new_zoom));*/

	// texture filtering patch
	if (cfg.value_bool(true, "patches.gl_nearest")) {
		const auto GL_NEAREST = 0x2600;
		patch_extern(0x43DC95, &GL_NEAREST, sizeof(GL_NEAREST));
	}

	// patch the value passed to sub_450E50
	if (cfg.value_bool(false, "patches.windowed")) {
		const auto zero = '\0';
		patch_extern(0x44DCC9, &zero, sizeof(zero));
	}

	// patch the base sramdata directory to null
	const auto null_terminator = '\0';
	patch_extern(0x46A0B0, &null_terminator, sizeof(null_terminator));

	// patch in a reference to the new directory for sramdata
	const auto sram_path = cfg.value_str("./", "patches.sram_path");
	const auto buf_len = sram_path.length() + 1;
	auto *buf = VirtualAllocEx(
		process,
		nullptr,
		buf_len,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE);

	WriteProcessMemory(process, buf, sram_path.c_str(), buf_len, nullptr);
	patch_extern(0x44B3E1, &buf, sizeof(buf));
}

/**
 * inject_dll - Inject a DLL
 * @process:	Process handle
 * @dll_path:	Path of DLL file
 *
 * Use VirtualAllocEx and WriteProcessMemory to insert the DLL path into the
 * external process, then CreateRemoteThread to make it call LoadLibrary.
 */
void inject_dll(const HANDLE process, const char *dll_path)
{
	const auto buf_len = strlen(dll_path) + 1;
	auto *buf = VirtualAllocEx(
			process,
			nullptr,
			buf_len,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE);

	WriteProcessMemory(process, buf, dll_path, buf_len, nullptr);
	CreateRemoteThread(
			process,
			nullptr,
			0,
			(LPTHREAD_START_ROUTINE)(LoadLibrary),
			buf,
			0,
			nullptr);
}

/**
 * main - Entry point
 * @argc:	Command line argument count
 * @argv:	Array of command line arguments
 *
 * Launch TGM3 suspended, patch the resolution, inject our hook DLL + the typex
 * loader hook DLL and start the main thread
 */
int main(const int argc, const char *argv[])
{
	PROCESS_INFORMATION proc_info;
	STARTUPINFO startup_info = { 0 };
	startup_info.cb = sizeof(startup_info);

	// Pass the cmdline arguments to the game
	char cmdline[256];
	strcpy_s(cmdline, "game");
	for (auto i = 1; i < argc; i++) {
		strcat_s(cmdline, " ");
		strcat_s(cmdline, argv[i]);
	}

	CreateProcess(
		nullptr,
		cmdline,
		nullptr,
		nullptr,
		false,
		CREATE_SUSPENDED,
		nullptr,
		nullptr,
		&startup_info,
		&proc_info);

	apply_patches(proc_info.hProcess);

	inject_dll(proc_info.hProcess, "tgm3_input.dll");
	inject_dll(proc_info.hProcess, "typex_io.dll");
	ResumeThread(proc_info.hThread);

	return 0;
}