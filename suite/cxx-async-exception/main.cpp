// An exception thrown in a std::async thread reaches the caller of get().
//#ExpectedStdout: caught from async
#include <cstdio>
#include <future>
#include <stdexcept>

int main()
{
	auto f = std::async(std::launch::async, []() -> int { throw std::runtime_error("from async"); });
	try {
		f.get();
	} catch (const std::exception &e) {
		std::printf("caught %s\n", e.what());
		return 0;
	}
	return 1;
}
