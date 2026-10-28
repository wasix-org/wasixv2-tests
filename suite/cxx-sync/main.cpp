// C++20 synchronization (std::latch, std::binary_semaphore, on atomic waits), std::random_device
// and stream formatting.
//#BuildEnv: WASIXCC_COMPILER_FLAGS_CXX=-std=c++20
//#ExpectedStdout: random differs latch ok 1234.57
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <latch>
#include <random>
#include <semaphore>
#include <sstream>
#include <thread>

int main()
{
	std::random_device rd;
	unsigned a = rd(), b = rd(), c = rd();
	std::latch l(2);
	std::binary_semaphore sem(0);
	std::thread t([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		l.count_down();
		sem.release();
	});
	l.arrive_and_wait(); // waits until the thread counts down
	sem.acquire();
	t.join();
	std::ostringstream os;
	os << std::fixed << std::setprecision(2) << 1234.5678;
	bool differs = a != b || b != c;
	std::printf("random %s latch ok %s\n", differs ? "differs" : "same", os.str().c_str());
	return differs ? 0 : 1;
}
