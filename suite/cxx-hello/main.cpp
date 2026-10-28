// libc++'s streams, strings and containers, built with wasix++ against the sysroot's libc++.
//#ExpectedStdout: hello from libc++ 42 3.25
//#ExpectedStderr: stderr ok
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main()
{
	std::vector<std::string> words{"hello", "from", "libc++"};
	std::ostringstream out;
	for (const auto &w : words)
		out << w << ' ';
	std::cout << out.str() << 42 << ' ' << 3.25 << std::endl;
	std::cerr << "stderr ok" << std::endl;
	return 0;
}
