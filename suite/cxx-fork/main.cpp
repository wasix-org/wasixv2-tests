// fork in a C++ program (asyncify, without exceptions): the child sees the parent's objects and
// its own copy of the stream buffers.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: before fork
//#ExpectedStdout: child sees parent-data
//#ExpectedStdout: child exit 7
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
	std::string msg = "parent-data";
	std::cout << "before fork" << std::endl;
	pid_t pid = fork();
	if (pid == 0) {
		std::cout << "child sees " << msg << std::endl;
		_exit(7);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	std::cout << "child exit " << WEXITSTATUS(st) << std::endl;
	return WIFEXITED(st) && WEXITSTATUS(st) == 7 ? 0 : 1;
}
