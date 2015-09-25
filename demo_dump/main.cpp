#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <Windows.h>

const char *grades[] = {
	"9", "8", "7", "6", "5", "4", "3", "2", "1",
	"S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9",
	"S10", "S11", "S12", "S13",
	"m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "m9",
	"Master", "MasterK", "MasterV", "MasterO", "MasterM",
	"Grand Master"
};

struct demo_info {
	std::string filename;
	int game_num;

	int frames_played;
	short mode;
	short level;
	char grade;
	int start_frame;
};

/**
 * read_demo - Print data about games in a demo file
 *
 * Read game data and copy to the demo info list.
 */
template<typename T>
void read_demo(const char *filename, std::insert_iterator<T> inserter)
{
	std::string full_path("demos\\");
	full_path += filename;

	std::ifstream input(full_path);

	char target_version;
	input.read(&target_version, 1);

	auto game_num = 0;
	while (true) {
		demo_info info;
		info.filename = filename;
		info.game_num = game_num++;

		input.read(
				(char*)(&info.frames_played),
				sizeof(info.frames_played));

		if (input.eof())
			break;

		input.read((char*)(&info.mode), sizeof(info.mode));
		input.read((char*)(&info.level), sizeof(info.level));
		input.read(&info.grade, 1);
		input.read((char*)(&info.start_frame), sizeof(info.start_frame));

		*inserter = info;
	}
}

/**
 * main - Entry point
 *
 * Loop over the demos directory with the disgusting Windows API and pass every
 * file in it to read_demo.
 */
int main()
{
	std::vector<demo_info> demos;

	WIN32_FIND_DATA find_data;
	const auto find_handle = FindFirstFile("demos\\*.inf", &find_data);
	if (find_handle == INVALID_HANDLE_VALUE)
		return 0;

	do {
		read_demo(
				find_data.cFileName,
				std::inserter(demos, demos.end()));
	} while (FindNextFile(find_handle, &find_data));

	std::sort(demos.begin(), demos.end(), [](demo_info &a, demo_info &b)
	{
		// Sort by grade, then by level, then by time
		if (a.grade == b.grade) {
			if (a.level == b.level)
				return a.frames_played < b.frames_played;

			return a.level > b.level;
		}
		return a.grade > b.grade;
	});

	for (auto &info : demos) {
		const auto minutes = info.frames_played / 3600;
		const auto seconds = info.frames_played % 3600 / 60;
		const auto hundred = info.frames_played % 60 * 100 / 60;

		std::cout << info.filename;

		std::cout << std::setfill('0');
		std::cout << " Game #" << std::setw(2) << info.game_num;
		std::cout << "         ";

		std::cout << std::setw(2) << minutes << ":";
		std::cout << std::setw(2) << seconds << ":";
		std::cout << std::setw(2) << hundred;
		std::cout << std::setfill(' ');

		std::cout << std::setw(13) << (
				info.mode & 0x01 ? "Easy" :
				info.mode & 0x02 ? "Master" :
				info.mode & 0x20 ? "Shirase" :
				info.mode & 0x40 ? "Sakura" :
				"Unknown");

		std::cout << std::setw(13) << info.level;

		// Skip over Shirase exclusive grades
		if ((info.mode & 0x20) == 0 && info.grade > 17)
			std::cout << std::setw(13) << grades[info.grade + 4];
		else
			std::cout << std::setw(13) << grades[info.grade];

		std::cout << std::setw(13) << info.start_frame;

		std::cout << std::endl;
	}

	FindClose(find_handle);
	return 0;
}