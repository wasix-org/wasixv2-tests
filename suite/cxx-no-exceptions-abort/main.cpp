// Without exceptions (the asyncify sysroot's libc++ is built with -fno-exceptions), where libc++
// would throw it prints the exception and aborts: SIGABRT, 128 + 6.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: before at()
//#ExpectedExitCode: 134
#include <cstdio>
#include <vector>

int main()
{
	std::vector<int> v(2);
	std::printf("before at()\n");
	std::fflush(stdout);
	return v.at(10);
}
