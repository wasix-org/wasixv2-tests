// std::filesystem and fstreams: create, write, copy, rename, walk and remove a tree.
//#ExpectedStdout: a.txt 5
//#ExpectedStdout: d.txt 5
//#ExpectedStdout: sub/
//#ExpectedStdout: sub/b.txt 6
//#ExpectedStdout: sub/deep/
//#ExpectedStdout: exists=1 removed=6
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main()
{
	fs::path base = fs::temp_directory_path() / "cxx-filesystem";
	std::error_code ec;
	fs::remove_all(base, ec);
	fs::create_directories(base / "sub" / "deep");
	std::ofstream(base / "a.txt") << "hello";
	std::ofstream(base / "sub" / "b.txt") << "world!";
	fs::copy_file(base / "a.txt", base / "c.txt");
	fs::rename(base / "c.txt", base / "d.txt");
	std::vector<std::string> names;
	for (auto &e : fs::recursive_directory_iterator(base))
		names.push_back(fs::relative(e.path(), base).string() +
				(e.is_directory() ? "/" : "") +
				(e.is_regular_file() ? " " + std::to_string(e.file_size()) : ""));
	std::sort(names.begin(), names.end());
	for (auto &n : names)
		std::printf("%s\n", n.c_str());
	int exists = fs::exists(base / "d.txt");
	auto removed = fs::remove_all(base);
	std::printf("exists=%d removed=%ju\n", exists, (uintmax_t)removed);
	return names.size() == 5 && removed == 6 && !fs::exists(base) ? 0 : 1;
}
