// C++ exceptions (wasm exception handling): unwinding runs destructors, catch by type, a custom
// type, exception_ptr rethrown in another thread, libc++'s own throws, rethrow and nested
// exceptions.
//#ExpectedStdout: unwind level3
//#ExpectedStdout: unwind level2
//#ExpectedStdout: caught: boom 7
//#ExpectedStdout: custom 5
//#ExpectedStdout: unwind level3
//#ExpectedStdout: unwind level2
//#ExpectedStdout: rethrown in thread: boom 9
//#ExpectedStdout: out_of_range ok
//#ExpectedStdout: rethrow int 1
//#ExpectedStdout: nested outer/inner
//#ExpectedStdout: ok=6
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct Guard {
	const char *n;
	~Guard() { std::printf("unwind %s\n", n); }
};

[[gnu::noinline]] static void level3(int x)
{
	Guard g{"level3"};
	if (x > 0)
		throw std::runtime_error("boom " + std::to_string(x));
}

[[gnu::noinline]] static void level2(int x)
{
	Guard g{"level2"};
	level3(x);
}

struct Custom {
	int code;
};

int main()
{
	int ok = 0;
	try {
		level2(7);
	} catch (const std::exception &e) {
		std::printf("caught: %s\n", e.what());
		ok++;
	}
	try {
		throw Custom{5};
	} catch (Custom c) {
		std::printf("custom %d\n", c.code);
		ok++;
	}
	std::exception_ptr ep;
	try {
		level2(9);
	} catch (...) {
		ep = std::current_exception();
	}
	std::thread t([&] {
		try {
			std::rethrow_exception(ep);
		} catch (const std::runtime_error &e) {
			std::printf("rethrown in thread: %s\n", e.what());
			ok++;
		}
	});
	t.join();
	try {
		std::vector<int> v(2);
		(void)v.at(10);
	} catch (const std::out_of_range &) {
		std::printf("out_of_range ok\n");
		ok++;
	}
	try {
		try {
			throw 1;
		} catch (int) {
			throw;
		}
	} catch (int i) {
		std::printf("rethrow int %d\n", i);
		ok++;
	}
	auto nested = [] {
		try {
			throw std::logic_error("inner");
		} catch (...) {
			std::throw_with_nested(std::runtime_error("outer"));
		}
	};
	try {
		nested();
	} catch (const std::exception &e) {
		try {
			std::rethrow_if_nested(e);
		} catch (const std::logic_error &in) {
			std::printf("nested %s/%s\n", e.what(), in.what());
			ok++;
		}
	}
	std::printf("ok=%d\n", ok);
	return ok == 6 ? 0 : 1;
}
