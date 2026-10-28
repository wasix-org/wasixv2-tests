// Static construction and destruction: declaration order within a translation unit, a
// constructor with a priority before them, a function-local static on first use, and destructors
// in reverse order at exit.
//#ExpectedStdout: constructor(101) first
//#ExpectedStdout: init 3 order: 1 2 3 4
//#ExpectedStdout: dtor 4
//#ExpectedStdout: dtor 2
//#ExpectedStdout: dtor 1
#include <cstdio>
#include <string>
#include <vector>

static std::vector<int> &log()
{
	static std::vector<int> v;
	return v;
}

struct Rec {
	int id;
	Rec(int i) : id(i) { log().push_back(i); }
	~Rec() { std::printf("dtor %d\n", id); }
};

Rec a(1);
Rec b(2);
static std::string s = "init " + std::to_string(a.id + b.id);
struct Late {
	Late() { log().push_back(3); }
} late;

__attribute__((constructor(101))) static void early()
{
	std::printf("constructor(101) first\n");
}

int main()
{
	static Rec local(4);
	std::printf("%s order:", s.c_str());
	for (int i : log())
		std::printf(" %d", i);
	std::printf("\n");
	return log() == std::vector<int>{1, 2, 3, 4} ? 0 : 1;
}
