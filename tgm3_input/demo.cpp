#define WIN32_LEAN_AND_MEAN
#include <fstream>
#include <string>
#include <ctime>

#include <Windows.h>
#include <detours.h.>
#include <intrin.h>

static std::ifstream input;

static std::ofstream output;
static std::ofstream out_info;

using get_jvs_data_t = char*(*)(int);
static get_jvs_data_t orig_get_jvs_data;

static const char format_version = 0;
static int header_size = 1; // begins with format ver

static int target_frame; // frame to skip to

/**
 * play_get_jvs_data - Playback input hook
 * @unknown:	Always 1
 *
 * Just read values from the file
 */
static char *play_get_jvs_data(const int unknown)
{
	auto *data = orig_get_jvs_data(unknown);

	auto *buttons1 = (unsigned short*)(data + 0x184);
	auto *buttons2 = (unsigned short*)(data + 0x186);

	input.read((char*)(buttons1), sizeof(*buttons1));
	input.read((char*)(buttons2), sizeof(*buttons2));

	if (input.eof())
		exit(0);

	return data;
}

/**
 * play_random - Playback RNG hook
 * @seed:	Seed pointer
 * @save_seed:	Boolean indicating whether to update the seed
 *
 * Just read values from the file
 */
static int play_random(int *seed, const int save_seed)
{
	int result;
	input.read((char*)(&result), sizeof(result));
	if (save_seed)
		*seed = result;

	if (input.eof())
		exit(0);

	return result;
}

/**
 * play_read_sram - Playback hook for SRAM data
 * @name:	Filename
 * @buf:	Output buf
 * @unused:	Most likely did something during development but not anymore
 * @size:	Size to write
 *
 * Return the success value stored in the file, and read the output from the
 * file if it succeeded when the demo was being recorded
 */
static int play_read_sram(
	const char *name,
	char *buf,
	const int unused,
	const size_t size)
{
	// This returns a boolean value, but Arika used a 32-bit return type.
	// Using a char will save space in the demo.
	char success;
	input.read(&success, sizeof(success));

	if (success)
		input.read(buf, size);

	return success;
}

// Can't be overwriting our player data by watching a demo
static void play_write_sram(
	const char *name,
	char *buf,
	const int unused,
	const size_t size)
{
}

using SwapBuffers_t = BOOL(WINAPI*)(HDC);
SwapBuffers_t orig_SwapBuffers;
/**
 * play_SwapBuffers - SwapBuffers hook
 * @hdc:	Device context
 *
 * Churn out frames as fast as possible until the target frame is reached.
 */
BOOL WINAPI play_SwapBuffers(HDC hdc)
{
	const auto frame_count = *(int*)(0x4AE114); // frames since startup
	return frame_count > target_frame ? orig_SwapBuffers(hdc) : TRUE;
}

/**
 * rec_get_jvs_data - Recording input hook
 * @unknown:	Always 1
 *
 * Call the original and write the result to the file
 */
static char *rec_get_jvs_data(const int unknown)
{
	auto *data = orig_get_jvs_data(unknown);

	auto *buttons1 = (unsigned short*)(data + 0x184);
	auto *buttons2 = (unsigned short*)(data + 0x186);

	output.write((char*)(buttons1), sizeof(*buttons1));
	output.write((char*)(buttons2), sizeof(*buttons2));

	return data;
}

using random_t = int(*)(int*, int);
static random_t orig_random;
/**
 * rec_random - Recording RNG hook
 * @seed:	Seed pointer
 * @save_seed:	Boolean indicating whether to update the seed
 *
 * Call the original and write the result to the file
 */
static int rec_random(int *seed, const int save_seed)
{
	auto result = orig_random(seed, save_seed);
	output.write((char*)(&result), sizeof(result));
	return result;
}

using read_sram_t = int(*)(const char*, char*, int, size_t);
static read_sram_t orig_read_sram;
/**
 * rec_read_sram - Recording hook for SRAM data
 * @name:	Filename
 * @buf:	Output buf
 * @unused:	Most likely did something during development but not anymore
 * @size:	Size to write
 *
 * Call the original function and write its success value to the demo. If it
 * succeeded, write the output buffer.
 */
static int rec_read_sram(
		const char *name,
		char *buf,
		const int unused,
		const size_t size)
{
	// This returns a boolean value, but Arika used a 32-bit return type.
	// Using a char will save space in the demo.
	const auto success = (char)(orig_read_sram(name, buf, unused, size));
	output.write(&success, sizeof(success));

	if (success)
		output.write(buf, size);

	return success;
}

using unknown_game_over_t = void(*)(void*);
unknown_game_over_t orig_calc_final_grade;
/**
 * rec_calc_final_grade - Gets called once on game over
 *
 * Write the play time in frames, mode, level, grade index and start frame to
 * the end of the info file.
 */
void rec_calc_final_grade(void *data)
{
	orig_calc_final_grade(data);

	const auto play_data = (char*)(0x4AE238);
	const auto frames_played = *(int*)(play_data + 0x104);
	const auto mode = *(short*)(play_data + 0xF2);
	const auto level = *(short*)(play_data + 0xC8);

	const auto grade_data = (char*)(0x4ACD88);
	const auto grade = *(char*)(grade_data + 0x6);

	const auto frame_count = *(int*)(0x4AE114); // frames since startup
	const auto start_frame = frame_count - frames_played;

	out_info.write((char*)(&frames_played), sizeof(frames_played));
	out_info.write((char*)(&mode), sizeof(mode));
	out_info.write((char*)(&level), sizeof(level));
	out_info.write(&grade, 1);
	out_info.write((char*)(&start_frame), sizeof(start_frame));
}

/**
 * setup_playback - Install hooks for demo playback
 * @name:	Demo file to play
 *
 * Hook the SRAM reading function and the RNG to read from the demo file and
 * hook the SRAM write functions to do nothing 
 */
void setup_playback(const char *cmdline)
{
	orig_get_jvs_data = (get_jvs_data_t)(DetourFunction(
		(BYTE*)(0x45D490), (BYTE*)(play_get_jvs_data)));
	DetourFunction((BYTE*)(0x431F20), (BYTE*)(play_random));
	DetourFunction((BYTE*)(0x44B690), (BYTE*)(play_read_sram));
	DetourFunction((BYTE*)(0x44B7E0), (BYTE*)(play_write_sram));
	orig_SwapBuffers = (SwapBuffers_t)(DetourFunction(
		(BYTE*)(SwapBuffers), (BYTE*)(play_SwapBuffers)));

	char name[MAX_PATH];
	int target_game = 0;
	sscanf_s(cmdline, "%s %i", name, MAX_PATH, &target_game);

	char demo_filename[MAX_PATH];
	sprintf_s(demo_filename, MAX_PATH, "demos/%s.dem", name);
	input.open(demo_filename, std::ios::binary);
	if (input.fail()) {
		MessageBox(
			nullptr,
			"Failed to open demo file for playback.",
			"Error",
			MB_OK);

		exit(EXIT_FAILURE);
	}

	char info_filename[MAX_PATH];
	sprintf_s(info_filename, MAX_PATH, "demos/%s.inf", name);
	std::ifstream in_info(info_filename, std::ios::binary);
	if (in_info.fail()) {
		// old demo, no info file
		target_frame = 0;
		return;
	}

	// For backwards compatibility
	char target_version;
	in_info.read(&target_version, 1);

	int game_num = 0;
	while (true) {
		int frames_played;
		in_info.read((char*)(&frames_played), sizeof(frames_played));

		if (in_info.eof()) {
			MessageBox(
				nullptr,
				"Failed to locate the target game in the demo .inf file.",
				cmdline,
				MB_OK);

			exit(EXIT_FAILURE);
		}

		short mode;
		in_info.read((char*)(&mode), sizeof(mode));
		short level;
		in_info.read((char*)(&level), sizeof(level));
		char grade;
		in_info.read(&grade, 1);
		int start_frame;
		in_info.read((char*)(&start_frame), sizeof(start_frame));

		if (game_num++ == target_game) {
			target_frame = start_frame;
			break;
		}
	}

}

/**
 * setup_recording - Install hooks for demo recording
 *
 * Hook the SRAM reading function and the RNG to save their result
 */
void setup_recording()
{
	CreateDirectory("demos", nullptr);

	tm datetime;
	auto time_ms = time(nullptr);
	localtime_s(&datetime, &time_ms);

	char filename[MAX_PATH];
	strftime(filename, MAX_PATH, "demos/%Y_%m_%d_%H_%M_%S.dem", &datetime);

	char info_filename[MAX_PATH];
	strftime(info_filename, MAX_PATH, "demos/%Y_%m_%d_%H_%M_%S.inf", &datetime);

	output.open(filename, std::ios::binary);
	out_info.open(info_filename, std::ios::binary);

	// Version
	out_info.write(&format_version, 1);

	orig_get_jvs_data = (get_jvs_data_t)(DetourFunction(
		(BYTE*)(0x45D490), (BYTE*)(rec_get_jvs_data)));
	orig_random = (random_t)(DetourFunction(
		(BYTE*)(0x431F20), (BYTE*)(rec_random)));
	orig_read_sram = (read_sram_t)(DetourFunction(
		(BYTE*)(0x44B690), (BYTE*)(rec_read_sram)));
	orig_calc_final_grade = (unknown_game_over_t)(DetourFunction(
		(BYTE*)(0x406B20), (BYTE*)(rec_calc_final_grade)));
}