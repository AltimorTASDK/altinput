#define WIN32_LEAN_AND_MEAN
#include "config.h"
#include <random>
#include <ctime>
#include <vector>
#include <algorithm>

#include <Windows.h>
#include <detours.h>

// Use a bag to keep things consistent
static class garbage_bag {
	std::vector<int> bag;

public:
	int next(const int field_width);
} garbage_generator;

/**
 * next - Get the next garbage hole position
 * @field_width:	Width of play field
 *
 * Return the back of the bag and remove that entry. Generate a new bag if
 * necessary.
 */
int garbage_bag::next(const int field_width)
{
	if (bag.empty()) {
		for (auto i = 1; i <= field_width - 2; i++)
			bag.push_back(i);

		std::random_shuffle(bag.begin(), bag.end());
	}

	const auto result = bag.back();
	bag.pop_back();
	return result;
}

static struct {
	int quota;
	int block_size;
	int counter;
} dig;

/**
 * dig_are_frame - Dig practice logic
 * @play_data:	Holds field buf pointer and info
 *
 * Update the garbage quota and add the desired number of rows with a single
 * random hole in each to the bottom of the field if the quota has been
 * fulfilled.
 */
static void dig_are_frame(char *play_data)
{
	// Apply on last frame of ARE
	const auto are = *(short*)(play_data + 0xC);
	if (are > 1)
		return;

	dig.counter++;
	if (dig.counter < dig.quota)
		return;

	dig.counter = 0;

	const auto field_width = *(play_data + 0x1140);
	const auto field_height = *(play_data + 0x1141);

	struct field_block {
		int properties;
		int unknown;
	};

	auto *field_buf = *(field_block**)(play_data + 0x113C);

	// Copy the old rows up above the new garbage
	for (auto y = field_height - 6; y > dig.block_size; y--) {
		for (auto x = 1; x < field_width - 1; x++) {
			const auto dest_idx = x + field_width * y;
			const auto src_row = y - dig.block_size;
			const auto src_idx = x + field_width * src_row;
			field_buf[dest_idx] = field_buf[src_idx];
		}
	}

	// Add garbage rows with random holes
	for (auto y = 1; y <= dig.block_size; y++) {
		const auto hole_x = garbage_generator.next(field_width);
		for (auto x = 1; x < field_width - 1; x++) {
			const auto idx = x + field_width * y;
			//0x40000 = Unknown
			//0x00100 = Garbage
			//0x00006 = Block type (color overriden by garbage)
			field_buf[idx].properties = x != hole_x ? 0x40106 : 0;
			field_buf[idx].unknown = 0;
		}
	}
}

static BYTE *orig_are_frame;
/**
 * hook_are_frame - Asm wrapper for dig practice hook
 * @eax:	Play data
 *
 * Pass the play data to dig_spawn_piece then jump to the original function
 */
__declspec(naked) static void hook_are_frame()
{
	_asm {
		push eax // Preserve eax

		push eax
		call dig_are_frame
		add esp, 4

		pop eax

		jmp [orig_are_frame]
	}
}

static int speed_lock;

using get_timing_t = short(*)(const int table, const int level);
static get_timing_t orig_get_timing;
/**
 * hook_get_timing - Get a timing for a particular level
 * @table:	Which timing to get
 * @level:	Which level to get timing for
 *
 * Apply speed lock
 */
static short hook_get_timing(const int table, const int level)
{
	return orig_get_timing(table, speed_lock);
}

using get_gravity_t = int(*)(const int mode, const int level);
static get_gravity_t orig_get_gravity;
/**
 * hook_get_gravity - Get gravity for a particular level
 * @mode:	Current gamemode
 * @level:	Which level to get gravity for
 *
 * Apply speed lock
 */
static int hook_get_gravity(const int mode, const int level)
{
	return orig_get_gravity(mode, speed_lock);
}

/**
 * init_practice - Set up practice hooks
 * @cfg:	tgm3.cfg
 *
 * Apply hooks for dig mode
 */
void init_practice(const config &cfg)
{
	speed_lock = cfg.value_int(-1, "practice.speed_lock");
	if (speed_lock >= 0) {
		orig_get_timing = (get_timing_t)(DetourFunction(
				(BYTE*)(0x425610), (BYTE*)(hook_get_timing)));
		orig_get_gravity = (get_gravity_t)(DetourFunction(
				(BYTE*)(0x40CBA0), (BYTE*)(hook_get_gravity)));
	}

	if (cfg.value_bool(false, "practice.invisible")) {
		// patch the invisible field flag test to a JMP instead of JZ
		DWORD old_protect;
		VirtualProtect((void*)(0x41D429), 2, PAGE_READWRITE, &old_protect);
		*(char*)(0x41D429) = 0x90; // JZ is 2 bytes, so NOP one
		*(char*)(0x41D42A) = 0xE9;
		VirtualProtect((void*)(0x41D429), 2, old_protect, &old_protect);
	}

	if (cfg.value_bool(false, "practice.fuck_this_game")) {
		// force [] blocks
		DWORD old_protect;
		VirtualProtect((void*)(0x402B50), 2, PAGE_READWRITE, &old_protect);
		*(char*)(0x402B50) = 0x90;
		*(char*)(0x402B51) = 0x90;
		VirtualProtect((void*)(0x402B50), 2, old_protect, &old_protect);
	}

	dig.quota = cfg.value_int(0, "practice.dig.quota");
	if (dig.quota > 0) {
		dig.block_size = cfg.value_int(1, "practice.dig.block_size");
		orig_are_frame = DetourFunction(
			(BYTE*)(0x4107F0), (BYTE*)(hook_are_frame));
	}
}